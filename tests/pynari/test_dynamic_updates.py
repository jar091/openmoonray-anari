"""pynari.dynamic_updates — retained-mode updates from Python (Milestone 8).

One device, one frame object, three renders: the scene is changed between
them and the image has to follow. Nothing is recreated - which is the whole
point of a retained API, and the reason this is a test rather than a comment.

The expectations are the derived ones the C++ tests use: a Lambertian surface
of albedo a lit head-on by irradiance E has radiance a*E/pi, so with E = pi
the lit pixel *is* the material color, and doubling E doubles it.
"""

import math
import sys

import numpy as np

import _dll_path  # noqa: F401  (must precede the pynari import)
import pynari as anari

SIZE = 64
ALBEDO = (0.8, 0.2, 0.2)
NEW_COLOR = (0.1, 0.7, 0.3)
BACKGROUND = (0.0, 0.0, 1.0, 1.0)

failures = 0


def check(condition: bool, message: str) -> None:
    global failures
    if not condition:
        print(f"FAIL: {message}")
        failures += 1


def main() -> int:
    device = anari.newDevice("moonray")

    camera = device.newCamera("perspective")
    camera.setParameter("position", anari.float3, (0.0, 0.0, 3.0))
    camera.setParameter("direction", anari.float3, (0.0, 0.0, -1.0))
    camera.setParameter("up", anari.float3, (0.0, 1.0, 0.0))
    camera.setParameter("fovy", anari.FLOAT32, math.pi / 3.0)
    camera.setParameter("aspect", anari.FLOAT32, 1.0)
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
    material.setParameter("color", anari.float3, ALBEDO)
    material.commitParameters()

    surface = device.newSurface()
    surface.setParameter("geometry", anari.GEOMETRY, mesh)
    surface.setParameter("material", anari.MATERIAL, material)
    surface.commitParameters()

    light = device.newLight("directional")
    light.setParameter("direction", anari.float3, (0.0, 0.0, -1.0))
    light.setParameter("irradiance", anari.FLOAT32, math.pi)
    light.commitParameters()

    world = device.newWorld()
    world.setParameterArray1D("surface", anari.SURFACE, [surface])
    world.setParameterArray1D("light", anari.LIGHT, [light])
    world.commitParameters()

    renderer = device.newRenderer("default")
    renderer.setParameter("background", anari.float4, BACKGROUND)
    renderer.setParameter("pixelSamples", anari.INT32, 16)
    renderer.commitParameters()

    frame = device.newFrame()
    frame.setParameter("size", anari.uint2, [SIZE, SIZE])
    frame.setParameter("channel.color", anari.DATA_TYPE, anari.FLOAT32_VEC4)
    frame.setParameter("renderer", anari.RENDERER, renderer)
    frame.setParameter("camera", anari.CAMERA, camera)
    frame.setParameter("world", anari.WORLD, world)
    frame.commitParameters()

    def render():
        frame.render()
        return np.array(frame.get("channel.color"))

    def coverage(pixels):
        return int(np.count_nonzero(pixels[:, :, 1] > 0.05))

    pixels = render()
    center = pixels[SIZE // 2, SIZE // 2]
    before = coverage(pixels)
    print(f"first render: center {center[:3]}, coverage {before}")
    for c in range(3):
        check(
            abs(float(center[c]) - ALBEDO[c]) <= 0.05,
            f"first render channel {c} is {center[c]}, expected {ALBEDO[c]}",
        )

    # 1. material color
    material.setParameter("color", anari.float3, NEW_COLOR)
    material.commitParameters()
    center = render()[SIZE // 2, SIZE // 2]
    print(f"after the material change: {center[:3]}")
    for c in range(3):
        check(
            abs(float(center[c]) - NEW_COLOR[c]) <= 0.05,
            f"material update channel {c} is {center[c]}, expected {NEW_COLOR[c]}",
        )

    # 2. light intensity: doubling the irradiance doubles the radiance
    light.setParameter("irradiance", anari.FLOAT32, 2.0 * math.pi)
    light.commitParameters()
    center = render()[SIZE // 2, SIZE // 2]
    print(f"after doubling the irradiance: {center[:3]}")
    for c in range(3):
        check(
            abs(float(center[c]) - 2.0 * NEW_COLOR[c]) <= 0.05,
            f"light update channel {c} is {center[c]}, expected {2.0 * NEW_COLOR[c]}",
        )

    # 3. camera: moving closer makes the triangle cover more of the frame
    camera.setParameter("position", anari.float3, (0.0, 0.0, 1.8))
    camera.commitParameters()
    after = coverage(render())
    print(f"coverage {before} -> {after} after moving the camera closer")
    check(after > 1.5 * before, "moving the camera closer must enlarge the triangle")

    if failures:
        print(f"{failures} check(s) FAILED")
        return 1

    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
