"""pynari.load_device — PyNARI can load the MoonRay device (Milestone 7).

Nothing renders here: this separates "PyNARI + the device library load" from
"the render is correct", so a failure points at one or the other.
"""

import sys

import _dll_path  # noqa: F401  (must precede the pynari import)
import pynari as anari


def main() -> int:
    device = anari.newDevice("moonray")
    if device is None:
        print("FAIL: anari.newDevice('moonray') returned None")
        return 1

    # A device that cannot create objects is not usable, so exercise one
    # factory call as well.
    camera = device.newCamera("perspective")
    if camera is None:
        print("FAIL: device.newCamera('perspective') returned None")
        return 1

    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
