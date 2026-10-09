"""pynari.texture — an image2D sampler through PyNARI (Milestone 9).

A quad with UVs in [0,1] and a 2x2 texture: each quadrant of the quad has to
show its own texel. Where to probe is arithmetic - with the camera 3 units
back and a 60 degree vertical field of view the frame spans +-1.732, so the
quad occupies the frame fraction 0.2113 .. 0.7887 and the texel centers
(u = 0.25, 0.75) land at 0.356 and 0.644.
"""

import math
import sys

import numpy as np

import _dll_path  # noqa: F401  (must precede the pynari import)
import pynari as anari

SIZE = 64
BACKGROUND = (0.0, 0.0, 1.0, 1.0)

# Row-major from the lower left: 0 lower left, 1 lower right, 2 upper left,
# 3 upper right.
TEXELS = np.array(
    [[[1.0, 0.0, 0.0], [0.0, 1.0, 0.0]], [[0.0, 0.0, 1.0], [1.0, 1.0, 0.0]]],
    dtype=np.float32,
)

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
        [-1.0, -1.0, 0.0, 1.0, -1.0, 0.0, 1.0, 1.0, 0.0, -1.0, 1.0, 0.0],
        dtype=np.float32,
    )
    uvs = np.array([0.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 1.0], dtype=np.float32)
    indices = np.array([0, 1, 2, 0, 2, 3], dtype=np.uint32)

    mesh = device.newGeometry("triangle")
    mesh.setParameter(
        "vertex.position", anari.ARRAY1D, device.newArray1D(anari.FLOAT32_VEC3, vertices)
    )
    mesh.setParameter(
        "vertex.attribute0", anari.ARRAY1D, device.newArray1D(anari.FLOAT32_VEC2, uvs)
    )
    mesh.setParameter(
        "primitive.index", anari.ARRAY1D, device.newArray1D(anari.UINT32_VEC3, indices)
    )
    mesh.commitParameters()

    sampler = device.newSampler("image2D")
    sampler.setParameter("image", anari.ARRAY2D, device.newArray2D(anari.FLOAT32_VEC3, TEXELS))
    sampler.setParameter("inAttribute", anari.STRING, "attribute0")
    sampler.commitParameters()

    material = device.newMaterial("matte")
    material.setParameter("color", anari.SAMPLER, sampler)
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

    frame.render()
    pixels = np.array(frame.get("channel.color"))

    probes = (
        ("lower left", 0.356, 0.356, TEXELS[0][0]),
        ("lower right", 0.644, 0.356, TEXELS[0][1]),
        ("upper left", 0.356, 0.644, TEXELS[1][0]),
        ("upper right", 0.644, 0.644, TEXELS[1][1]),
    )

    for name, fx, fy, expected in probes:
        # pynari hands back row 0 = the bottom of the frame, like ANARI.
        c = pixels[int(fy * SIZE), int(fx * SIZE)]
        print(f"{name:12s} {c[0]:.3f} {c[1]:.3f} {c[2]:.3f} (expected {expected})")
        for ch in range(3):
            check(
                abs(float(c[ch]) - float(expected[ch])) <= 0.1,
                f"{name} channel {ch} is {c[ch]}, expected {expected[ch]}",
            )

    if failures:
        print(f"{failures} check(s) FAILED")
        return 1

    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
