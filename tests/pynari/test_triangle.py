"""pynari.triangle — the Milestone 7 scene rendered through PyNARI.

This is deliberately the *same* scene as the C++ test
(tests/integration/test_render.cpp) with the same derived expectations, so
the two together show that the Python path and the C API path produce the
same image:

    A Lambertian surface of albedo a lit head-on by a directional light of
    irradiance E has outgoing radiance L = a * E / pi. With E = pi the lit
    triangle's color is exactly the material color.
"""

import math
import sys

import numpy as np

import _dll_path  # noqa: F401  (must precede the pynari import)
import pynari as anari

WIDTH = 64
HEIGHT = 64
ALBEDO = (0.8, 0.2, 0.2)
BACKGROUND = (0.0, 0.0, 1.0, 1.0)

failures = 0


def check(condition: bool, message: str) -> None:
    global failures
    if not condition:
        print(f"FAIL: {message}")
        failures += 1


def main() -> int:
    device = anari.newDevice("moonray")
    check(device is not None, "anari.newDevice('moonray') returned None")
    if device is None:
        return 1

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
        "vertex.position",
        anari.ARRAY1D,
        device.newArray1D(anari.FLOAT32_VEC3, vertices),
    )
    mesh.setParameter(
        "primitive.index",
        anari.ARRAY1D,
        device.newArray1D(anari.UINT32_VEC3, indices),
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
    light.setParameter("color", anari.float3, (1.0, 1.0, 1.0))
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
    frame.setParameter("size", anari.uint2, [WIDTH, HEIGHT])
    frame.setParameter("channel.color", anari.DATA_TYPE, anari.FLOAT32_VEC4)
    frame.setParameter("renderer", anari.RENDERER, renderer)
    frame.setParameter("camera", anari.CAMERA, camera)
    frame.setParameter("world", anari.WORLD, world)
    frame.commitParameters()

    frame.render()
    pixels = np.array(frame.get("channel.color"))

    check(pixels.shape == (HEIGHT, WIDTH, 4), f"unexpected frame shape {pixels.shape}")
    check(bool(np.all(np.isfinite(pixels))), "frame contains non-finite values")

    lit = int(np.count_nonzero(pixels[:, :, 0] > 0.01))
    check(lit > WIDTH * HEIGHT // 10, "the triangle covers almost none of the frame")

    center = pixels[HEIGHT // 2, WIDTH // 2]
    print(
        f"center pixel: {center[0]:.4f} {center[1]:.4f} {center[2]:.4f} "
        f"{center[3]:.4f} (expected {ALBEDO[0]} {ALBEDO[1]} {ALBEDO[2]} 1)"
    )
    for c in range(3):
        check(
            abs(float(center[c]) - ALBEDO[c]) <= 0.05,
            f"center pixel channel {c} is {center[c]}, expected {ALBEDO[c]}",
        )
    check(abs(float(center[3]) - 1.0) <= 0.02, "center pixel alpha")

    # PyNARI hands back row 0 = ANARI row 0 = the bottom of the image, so the
    # last row is the top of the frame - outside the triangle either way.
    corner = pixels[HEIGHT - 1, 0]
    print(
        f"corner pixel: {corner[0]:.4f} {corner[1]:.4f} {corner[2]:.4f} "
        f"{corner[3]:.4f} (expected {BACKGROUND})"
    )
    for c in range(4):
        check(
            abs(float(corner[c]) - BACKGROUND[c]) <= 0.01,
            f"corner pixel channel {c} is {corner[c]}, expected {BACKGROUND[c]}",
        )

    if failures:
        print(f"{failures} check(s) FAILED")
        return 1

    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
