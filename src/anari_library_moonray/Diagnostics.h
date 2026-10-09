// Copyright 2026 openmoonray-anari contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace moonray_anari {

// Makes a silent Windows crash say where it happened.
//
// Two of the ways this stack of software dies leave no trace at all on
// Windows: the CRT's invalid-parameter handler and abort() both end in
// __fastfail, which bypasses every exception handler, so the process
// disappears with status 0xC0000409 and not one line of output. That is what
// an application embedding the device sees, and it is what the ANARI CTS saw.
//
// Setting OPENMOONRAY_ANARI_CRASH_TRACE in the environment installs handlers
// for those paths - plus the structured exceptions MoonRay's own POSIX signal
// handler cannot catch here - which print a symbolized call stack first. It is
// opt-in because it takes over process-global handlers that belong to the
// application, and a no-op off Windows, where MoonRay already prints a stack.
//
// Symbols come from the PDBs next to the DLLs; build MoonRay with
// scripts/build-moonray-windows.ps1 -WithDebugInfo to get them.
void installCrashDiagnostics();

} // namespace moonray_anari
