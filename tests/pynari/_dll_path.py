"""Make the ANARI/MoonRay shared libraries findable before importing pynari.

Python 3.8+ on Windows no longer searches PATH when loading an extension
module's dependencies, so the directories holding anari.dll,
anari_library_moonray.dll and the MoonRay runtime have to be registered
explicitly with os.add_dll_directory(). The CTest wiring passes them in
OPENMOONRAY_ANARI_DLL_DIRS, separated by '|' (';' is both CMake's list
separator and the Windows path separator); on Linux/macOS the loader
environment already covers this and the module below does nothing.

Import this before `import pynari`.
"""

import os
import sys

_handles = []  # keep the directory cookies alive for the process lifetime


def _add_dll_directories() -> None:
    if sys.platform != "win32":
        return
    dirs = os.environ.get("OPENMOONRAY_ANARI_DLL_DIRS", "")
    for entry in dirs.split("|"):
        if entry and os.path.isdir(entry):
            _handles.append(os.add_dll_directory(entry))


_add_dll_directories()
