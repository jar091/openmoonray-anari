"""Render the Milestone 7 triangle with MoonRay through PyNARI.

The scene is built entirely through ANARI (PyNARI never touches MoonRay
directly), rendered by MoonRay, and written out as a PPM so the example needs
nothing but numpy.

    python examples/pynari/01_triangle.py [-o triangle.ppm]

The loader environment (where anari_library_moonray, MoonRay and the RDL2
shader DSOs live) is described in TESTING.md; the pynari.* CTest tests set it
up automatically.
"""

import math
import os
import sys

import numpy as np

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

WIDTH = 256
HEIGHT = 256


def main(argv) -> int:
    out_file = "triangle.ppm"
    if len(argv) >= 2 and argv[0] == "-o":
        out_file = argv[1]

    device = anari.newDevice("moonray")

    camera = device.newCamera("perspective")
    camera.setParameter("position", anari.float3, (0.0, 0.0, 3.0))
    camera.setParameter("direction", anari.float3, (0.0, 0.0, -1.0))
    camera.setParameter("up", anari.float3, (0.0, 1.0, 0.0))
    camera.setParameter("fovy", anari.FLOAT32, math.pi / 3.0)
    camera.setParameter("aspect", anari.FLOAT32, WIDTH / HEIGHT)
    camera.commitParameters()

    vertices = np.array(
        [-1.0, -1.0, 0.0, 1.0, -1.0, 0.0, 0.0, 1.0, 0.0], dtype=np.float32
    )
    indices = np.array([0, 1, 2], dtype=np.uint32)

    mesh = device.newGeometry("triangle")
    mesh.setParameter(
        "vertex.position", anari.ARRAY1D, device.newArray1D(anari.FLOAT32_VEC3, vertices)
    )
    mesh.setParameter(
        "primitive.index", anari.ARRAY1D, device.newArray1D(anari.UINT32_VEC3, indices)
    )
    mesh.commitParameters()

    material = device.newMaterial("matte")
    material.setParameter("color", anari.float3, (0.8, 0.2, 0.2))
    material.commitParameters()

    surface = device.newSurface()
    surface.setParameter("geometry", anari.GEOMETRY, mesh)
    surface.setParameter("material", anari.MATERIAL, material)
    surface.commitParameters()

    light = device.newLight("directional")
    light.setParameter("direction", anari.float3, (-0.3, -0.4, -1.0))
    light.setParameter("irradiance", anari.FLOAT32, math.pi)
    light.commitParameters()

    world = device.newWorld()
    world.setParameterArray1D("surface", anari.SURFACE, [surface])
    world.setParameterArray1D("light", anari.LIGHT, [light])
    world.commitParameters()

    renderer = device.newRenderer("default")
    renderer.setParameter("background", anari.float4, (0.05, 0.05, 0.08, 1.0))
    renderer.setParameter("pixelSamples", anari.INT32, 64)
    renderer.commitParameters()

    frame = device.newFrame()
    frame.setParameter("size", anari.uint2, [WIDTH, HEIGHT])
    frame.setParameter("channel.color", anari.DATA_TYPE, anari.UFIXED8_RGBA_SRGB)
    frame.setParameter("renderer", anari.RENDERER, renderer)
    frame.setParameter("camera", anari.CAMERA, camera)
    frame.setParameter("world", anari.WORLD, world)
    frame.commitParameters()

    frame.render()
    pixels = np.array(frame.get("channel.color"))

    if pixels.shape != (HEIGHT, WIDTH, 4):
        print(f"unexpected frame shape {pixels.shape}")
        return 1
    if not pixels[:, :, 0].any():
        print("the frame is empty - nothing was rendered")
        return 1

    # ANARI's first pixel is the lower left; PPM starts at the top.
    rgb = pixels[::-1, :, :3]
    with open(out_file, "wb") as f:
        f.write(f"P6\n{WIDTH} {HEIGHT}\n255\n".encode("ascii"))
        f.write(rgb.tobytes())

    print(f"wrote {out_file} ({WIDTH}x{HEIGHT})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
