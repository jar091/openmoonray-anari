// Copyright 2026 openmoonray-anari contributors
// SPDX-License-Identifier: Apache-2.0

#include "Diagnostics.h"

#include <cstdlib>

#if defined(_WIN32)

#include <windows.h>
// dbghelp.h must follow windows.h
#include <dbghelp.h>

#include <csignal>
#include <cstdio>
#include <cstring>
#include <exception>

#pragma comment(lib, "dbghelp.lib")

namespace moonray_anari {

namespace {

// The last C++ exception thrown on this thread, remembered so that a
// terminate() can name what was in flight. An exception that escapes into a
// noexcept frame unwinds through the runtime and aborts, and by then the
// throwing frames - and the exception object itself - are gone: the stack at
// the abort() is nothing but the unwinder. This is the only place the type is
// still visible.
thread_local char t_lastThrow[256] = "";

// One symbolized frame per line, on stderr: stdout may be a pipe the parent
// process never gets to read once the CRT fast-fails.
void printStack(const char *what, unsigned skip)
{
  static LONG reported = 0;
  if (InterlockedIncrement(&reported) > 1)
    return; // every thread hits the same wall; one stack is the information

  std::fprintf(stderr, "\n=== %s ===\n", what);
  std::fflush(stderr);

  // Capture and print the raw stack *before* touching dbghelp: symbolization
  // loads symbols for every module in the process and is the part most likely
  // to fail or take longer than the dying process has. Module name plus
  // offset is already enough to place a frame.
  void *frames[62];
  const USHORT count =
      CaptureStackBackTrace(skip + 1, 62 - (skip + 1), frames, nullptr);

  for (USHORT i = 0; i < count; ++i) {
    HMODULE owner = nullptr;
    char moduleName[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            static_cast<LPCSTR>(frames[i]),
            &owner)
        && GetModuleFileNameA(owner, moduleName, MAX_PATH)) {
      const char *base = std::strrchr(moduleName, '\\');
      std::fprintf(stderr,
          "  %2u  %s+0x%llx\n",
          i,
          base ? base + 1 : moduleName,
          (unsigned long long)(reinterpret_cast<uintptr_t>(frames[i])
              - reinterpret_cast<uintptr_t>(owner)));
    } else {
      std::fprintf(stderr, "  %2u  %p\n", i, frames[i]);
    }
    std::fflush(stderr);
  }

  HANDLE process = GetCurrentProcess();
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
  if (!SymInitialize(process, nullptr, TRUE))
    return;

  std::fprintf(stderr, "--- with symbols ---\n");
  std::fflush(stderr);

  alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 512];
  auto *symbol = reinterpret_cast<SYMBOL_INFO *>(buffer);

  for (USHORT i = 0; i < count; ++i) {
    const DWORD64 address = reinterpret_cast<DWORD64>(frames[i]);

    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 511;
    DWORD64 displacement = 0;
    const char *name = "<unknown>";
    if (SymFromAddr(process, address, &displacement, symbol))
      name = symbol->Name;

    char moduleName[MAX_PATH] = "?";
    IMAGEHLP_MODULE64 module{};
    module.SizeOfStruct = sizeof(module);
    if (SymGetModuleInfo64(process, address, &module))
      std::snprintf(moduleName, sizeof(moduleName), "%s", module.ModuleName);

    IMAGEHLP_LINE64 line{};
    line.SizeOfStruct = sizeof(line);
    DWORD lineDisplacement = 0;
    if (SymGetLineFromAddr64(process, address, &lineDisplacement, &line)) {
      std::fprintf(stderr,
          "  %2u  %s!%s  (%s:%lu)\n",
          i,
          moduleName,
          name,
          line.FileName,
          (unsigned long)line.LineNumber);
    } else {
      std::fprintf(stderr,
          "  %2u  %s!%s  (+0x%llx)\n",
          i,
          moduleName,
          name,
          (unsigned long long)displacement);
    }
    std::fflush(stderr);
  }
}

// The CRT calls this instead of fast-failing, for a bad file descriptor, a
// bad handle, a null buffer - anything its parameter validation rejects.
void invalidParameterHandler(const wchar_t *expression,
    const wchar_t *function,
    const wchar_t *file,
    unsigned int line,
    uintptr_t)
{
  char what[512];
  std::snprintf(what,
      sizeof(what),
      "CRT invalid parameter in %ls (%ls:%u): %ls",
      function ? function : L"<unknown function>",
      file ? file : L"<unknown file>",
      line,
      expression ? expression : L"<no expression>");
  printStack(what, 1);
}

void abortHandler(int)
{
  char what[320];
  if (t_lastThrow[0]) {
    std::snprintf(what,
        sizeof(what),
        "abort() - the last exception thrown on this thread was %s",
        t_lastThrow);
  } else {
    std::snprintf(what, sizeof(what), "abort()");
  }
  printStack(what, 1);
}

void terminateHandler()
{
  const char *detail = "std::terminate()";
  char what[512];
  if (auto current = std::current_exception()) {
    try {
      std::rethrow_exception(current);
    } catch (const std::exception &e) {
      std::snprintf(what,
          sizeof(what),
          "std::terminate() on an escaping exception: %s",
          e.what());
      detail = what;
    } catch (...) {
      detail = "std::terminate() on an escaping exception of unknown type";
    }
  }
  printStack(detail, 1);
  std::abort();
}

constexpr DWORD kMsvcCppException = 0xE06D7363;

// The MSVC throw record, as vcruntime's ehdata.h lays it out. On x64 every
// pointer inside it is a 32-bit offset from the module base, which arrives as
// the fourth exception parameter.
struct MsvcTypeDescriptor
{
  const void *vftable;
  void *spare;
  char name[1];
};

struct MsvcCatchableType
{
  unsigned properties;
  int pType;
};

struct MsvcCatchableTypeArray
{
  int count;
  int types[1];
};

struct MsvcThrowInfo
{
  unsigned attributes;
  int pmfnUnwind;
  int pForwardCompat;
  int pCatchableTypeArray;
};

void rememberThrow(const EXCEPTION_RECORD &record)
{
  t_lastThrow[0] = '\0';
  if (record.NumberParameters < 4)
    return;

  const auto base = static_cast<const char *>(
      reinterpret_cast<const void *>(record.ExceptionInformation[3]));
  const auto *throwInfo = reinterpret_cast<const MsvcThrowInfo *>(
      record.ExceptionInformation[2]);
  if (!base || !throwInfo || !throwInfo->pCatchableTypeArray)
    return;

  const auto *types = reinterpret_cast<const MsvcCatchableTypeArray *>(
      base + throwInfo->pCatchableTypeArray);
  if (types->count < 1)
    return;

  // The first catchable type is the thrown type itself; the rest are its
  // bases.
  const auto *type =
      reinterpret_cast<const MsvcCatchableType *>(base + types->types[0]);
  const auto *descriptor =
      reinterpret_cast<const MsvcTypeDescriptor *>(base + type->pType);

  char demangled[256];
  if (UnDecorateSymbolName(
          descriptor->name + 1, demangled, sizeof(demangled), UNDNAME_NAME_ONLY)
      != 0) {
    std::snprintf(t_lastThrow, sizeof(t_lastThrow), "%s", demangled);
  } else {
    std::snprintf(t_lastThrow, sizeof(t_lastThrow), "%s", descriptor->name);
  }
}

LONG WINAPI exceptionHandler(EXCEPTION_POINTERS *info)
{
  const DWORD code = info->ExceptionRecord->ExceptionCode;

  if (code == kMsvcCppException) {
    rememberThrow(*info->ExceptionRecord);
    return EXCEPTION_CONTINUE_SEARCH; // it is very probably handled
  }

  if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_STACK_OVERFLOW
      && code != EXCEPTION_ILLEGAL_INSTRUCTION
      && code != EXCEPTION_INT_DIVIDE_BY_ZERO) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  char what[128];
  std::snprintf(what,
      sizeof(what),
      "structured exception 0x%08lx at %p",
      (unsigned long)code,
      info->ExceptionRecord->ExceptionAddress);
  printStack(what, 1);
  return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void installCrashDiagnostics()
{
  if (!std::getenv("OPENMOONRAY_ANARI_CRASH_TRACE"))
    return;

  static bool installed = false;
  if (installed)
    return;
  installed = true;

  _set_invalid_parameter_handler(invalidParameterHandler);
  std::signal(SIGABRT, abortHandler);
  std::set_terminate(terminateHandler);
  AddVectoredExceptionHandler(/*first=*/1, exceptionHandler);
}

} // namespace moonray_anari

#else

namespace moonray_anari {

void installCrashDiagnostics()
{
  // MoonRay installs a POSIX signal handler that already prints a call stack.
}

} // namespace moonray_anari

#endif
