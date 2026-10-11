// A stack trace for the crash you did not expect.
//
// MoonRay installs its own handler for POSIX signals, but a Windows access
// violation is a structured exception, not a signal, so a crash inside the
// renderer shows up as "Exception: SegFault" from CTest and nothing else -
// which says where it happened only if you already know. This header installs
// a vectored exception handler that symbolizes the call stack with dbghelp
// and prints it, so the failure names functions instead of offsets.
//
// Symbols come from the PDBs next to the DLLs. MoonRay's own build produces
// none by default; build it with
//   scripts/build-moonray-windows.ps1 -WithDebugInfo
// to get them (same optimization settings, ABI-compatible).
//
// On other platforms this is a no-op: MoonRay's signal handler already prints
// a callstack there.

#pragma once

#if defined(_WIN32)

#include <windows.h>
// dbghelp.h must follow windows.h
#include <dbghelp.h>

#include <cstdio>

#pragma comment(lib, "dbghelp.lib")

namespace test_crash {

inline LONG WINAPI handler(EXCEPTION_POINTERS *info)
{
  const DWORD code = info->ExceptionRecord->ExceptionCode;
  // Only the fatal ones: C++ exceptions and breakpoints pass through here too.
  if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_STACK_OVERFLOW
      && code != EXCEPTION_ILLEGAL_INSTRUCTION
      && code != EXCEPTION_INT_DIVIDE_BY_ZERO) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  // The same fault fires on every thread that retries the work, and a
  // hundred copies of one stack is not more information than one.
  static LONG reported = 0;
  if (InterlockedIncrement(&reported) > 1)
    return EXCEPTION_CONTINUE_SEARCH;

  std::printf("\n=== crash: exception 0x%08lx at %p ===\n",
      (unsigned long)code,
      info->ExceptionRecord->ExceptionAddress);
  if (code == EXCEPTION_ACCESS_VIOLATION
      && info->ExceptionRecord->NumberParameters >= 2) {
    // What was read or written: an address near zero is a null pointer plus
    // the offset of a member.
    std::printf("    %s address 0x%llx\n",
        info->ExceptionRecord->ExceptionInformation[0] == 0 ? "reading"
                                                            : "writing",
        (unsigned long long)info->ExceptionRecord->ExceptionInformation[1]);
  }
  std::fflush(stdout); // the process may not survive the walk below

  HANDLE process = GetCurrentProcess();
  HANDLE thread = GetCurrentThread();
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
  SymInitialize(process, nullptr, TRUE);

  alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 512];
  auto *symbol = reinterpret_cast<SYMBOL_INFO *>(buffer);

  auto describe = [&](unsigned index, DWORD64 address) {
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
      std::printf("  %2u  %s!%s  (%s:%lu)\n",
          index,
          moduleName,
          name,
          line.FileName,
          (unsigned long)line.LineNumber);
    } else {
      std::printf("  %2u  %s!%s  (+0x%llx)\n",
          index,
          moduleName,
          name,
          (unsigned long long)displacement);
    }
  };

  // Walk the *faulting* context, not this handler's own stack.
  CONTEXT context = *info->ContextRecord;
  STACKFRAME64 frame{};
  frame.AddrPC.Offset = context.Rip;
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrFrame.Offset = context.Rbp;
  frame.AddrFrame.Mode = AddrModeFlat;
  frame.AddrStack.Offset = context.Rsp;
  frame.AddrStack.Mode = AddrModeFlat;

  for (unsigned i = 0; i < 40; ++i) {
    if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64,
            process,
            thread,
            &frame,
            &context,
            nullptr,
            SymFunctionTableAccess64,
            SymGetModuleBase64,
            nullptr)) {
      break;
    }
    if (frame.AddrPC.Offset == 0)
      break;
    describe(i, frame.AddrPC.Offset);
    std::fflush(stdout);
  }

  // Let the process die as it would have: the trace is the point, not
  // recovery.
  return EXCEPTION_CONTINUE_SEARCH;
}

inline void install()
{
  AddVectoredExceptionHandler(/*first=*/1, handler);
}

} // namespace test_crash

#else

namespace test_crash {
inline void install() {}
} // namespace test_crash

#endif
