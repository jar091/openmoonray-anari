"""pynari.spheres — sphere geometry and vertex colors through PyNARI (M11).

Three spheres in a row, each with its own color, and a matte material whose
`color` names the geometry's "color" attribute. Two things are derived rather
than remembered:

  * where each sphere lands. The camera is 3 units in front of the z = 0 plane
    with a 60 degree vertical field of view, so that plane spans
    2*3*tan(30) = 3.4641 world units and one world unit is SIZE/3.4641 pixels;
  * how bright each one is. With irradiance pi from the camera's direction, a
    Lambertian surface renders albedo*cos(theta), and the point of a sphere at
    x that faces the camera has cos(theta) = 3/sqrt(9 + x*x) - exactly 1 for
    the sphere on the axis.
"""

import math
import sys

import numpy as np

import _dll_path  # noqa: F401  (must precede the pynari import)
import pynari as anari

SIZE = 64
DISTANCE = 3.0
BACKGROUND = (0.0, 0.0, 1.0, 1.0)

CENTERS = np.array(
    [[-1.0, 0.0, 0.0], [0.0, 0.0, 0.0], [1.0, 0.0, 0.0]], dtype=np.float32
)
RADII = np.array([0.25, 0.5, 0.25], dtype=np.float32)
COLORS = np.array(
    [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]], dtype=np.float32
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
    camera.setParameter("position", anari.float3, (0.0, 0.0, DISTANCE))
    camera.setParameter("direction", anari.float3, (0.0, 0.0, -1.0))
    camera.setParameter("up", anari.float3, (0.0, 1.0, 0.0))
    camera.setParameter("fovy", anari.FLOAT32, math.pi / 3.0)
    camera.setParameter("aspect", anari.FLOAT32, 1.0)
    camera.commitParameters()

    spheres = device.newGeometry("sphere")
    spheres.setParameter(
        "vertex.position", anari.ARRAY1D, device.newArray1D(anari.FLOAT32_VEC3, CENTERS)
    )
    spheres.setParameter(
        "vertex.radius", anari.ARRAY1D, device.newArray1D(anari.FLOAT32, RADII)
    )
    spheres.setParameter(
        "vertex.color", anari.ARRAY1D, device.newArray1D(anari.FLOAT32_VEC3, COLORS)
    )
    spheres.commitParameters()

    material = device.newMaterial("matte")
    material.setParameter("color", anari.STRING, "color")
    material.commitParameters()

    surface = device.newSurface()
    surface.setParameter("geometry", anari.GEOMETRY, spheres)
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
    image = np.array(frame.get("channel.color"))

    pixels_per_unit = SIZE / (2.0 * DISTANCE * math.tan(math.pi / 6.0))
    for i, (center, color) in enumerate(zip(CENTERS, COLORS)):
        x = int(SIZE / 2 + center[0] * pixels_per_unit)
        pixel = image[SIZE // 2, x]
        cosine = DISTANCE / math.sqrt(DISTANCE**2 + float(center[0]) ** 2)
        expected = color * cosine
        print(
            f"sphere {i} at x={center[0]:+.1f} -> column {x}: "
            f"{pixel[:3]} (expected {expected})"
        )
        for c in range(3):
            check(
                abs(float(pixel[c]) - float(expected[c])) <= 0.06,
                f"sphere {i} channel {c} is {pixel[c]}, expected {expected[c]}",
            )

    # The gaps between the spheres stay background: they are three spheres,
    # not one blob. The middle sphere's edge is at 0.5 and its neighbours' at
    # 0.75, so 0.625 is between them.
    gap = image[SIZE // 2, int(SIZE / 2 + 0.625 * pixels_per_unit)]
    print(f"gap pixel {gap[:3]} (expected the blue background)")
    check(
        float(gap[0]) < 0.05 and float(gap[1]) < 0.05,
        "the gap between two spheres is not background",
    )

    if failures:
        print(f"{failures} check(s) FAILED")
        return 1

    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
