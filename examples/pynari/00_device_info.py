"""Load the MoonRay ANARI device through PyNARI and report what it offers.

Nothing renders here - this is the "is the installation sane?" example.

Run it with the project's virtualenv and the loader environment the tests use
(see TESTING.md), e.g. on Windows:

    ctest --preset windows-msvc-release -R pynari       # the same wiring
    python examples/pynari/00_device_info.py            # if PATH/PYTHONPATH are set
"""

import os
import sys

# Windows: Python 3.8+ ignores PATH when resolving an extension module's
# dependent DLLs, so the directories holding anari.dll,
# anari_library_moonray.dll and the MoonRay runtime have to be registered
# explicitly. Point OPENMOONRAY_ANARI_DLL_DIRS at them, separated by '|'
# (TESTING.md shows the values the CTest wiring uses). No-op elsewhere.
_dll_dir_handles = []
if sys.platform == "win32":
    for _entry in os.environ.get("OPENMOONRAY_ANARI_DLL_DIRS", "").split("|"):
        if _entry and os.path.isdir(_entry):
            _dll_dir_handles.append(os.add_dll_directory(_entry))


import pynari as anari


def main() -> int:
    device = anari.newDevice("moonray")
    if device is None:
        print("could not create the 'moonray' device")
        return 1

    print("device 'moonray' created")

    # Object factories the Milestone 7 device implements. Creating one is the
    # honest test of "supported": the device returns an object that reports
    # itself invalid for anything it does not implement.
    for kind, subtype in (
        ("camera", "perspective"),
        ("geometry", "triangle"),
        ("material", "matte"),
        ("light", "directional"),
        ("renderer", "default"),
    ):
        factory = getattr(device, "new" + kind.capitalize())
        obj = factory(subtype)
        print(f"  {kind:9s} {subtype:12s} -> {'ok' if obj is not None else 'MISSING'}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
