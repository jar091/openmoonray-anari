"""pynari.instancing — one group, three instances, through PyNARI (Milestone 8).

The same scene as the C++ `anari.shared_group` test: a single triangle in a
group, instanced three times side by side. The geometry data exists once; only
the transforms differ, and the device turns that into MoonRay-native
instancing (docs/ANARI_MOONRAY_MAPPING.md).

Where the instances land is arithmetic, not guesswork: with the camera 8 units
back and a 60 degree vertical field of view the frame spans +-8*tan(30) =
+-4.62 world units, so each triangle's base (x-1 .. x+1) is 2*64/9.24 = 13.9
pixels wide and the three at -2.5, 0 and 2.5 leave gaps around x = -1.25 and
x = 1.25.
"""

import math
import sys

import numpy as np

import _dll_path  # noqa: F401  (must precede the pynari import)
import pynari as anari

SIZE = 64
CAMERA_DISTANCE = 8.0
ALBEDO = (0.8, 0.2, 0.2)
BACKGROUND = (0.0, 0.0, 1.0, 1.0)
OFFSETS = (-2.5, 0.0, 2.5)

failures = 0


def check(condition: bool, message: str) -> None:
    global failures
    if not condition:
        print(f"FAIL: {message}")
        failures += 1


def translation(x: float, y: float = 0.0):
    """A column-major ANARI mat4 that translates by (x, y, 0)."""
    return (
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        x, y, 0.0, 1.0,
    )


def build(device, offsets):
    camera = device.newCamera("perspective")
    camera.setParameter("position", anari.float3, (0.0, 0.0, CAMERA_DISTANCE))
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

    group = device.newGroup([surface])

    instances = []
    for x in offsets:
        instance = device.newInstance("transform")
        instance.setParameter("group", anari.GROUP, group)
        instance.setParameter("transform", anari.FLOAT32_MAT4, translation(x))
        instance.commitParameters()
        instances.append(instance)

    light = device.newLight("directional")
    light.setParameter("direction", anari.float3, (0.0, 0.0, -1.0))
    light.setParameter("irradiance", anari.FLOAT32, math.pi)
    light.commitParameters()

    world = device.newWorld()
    world.setParameterArray1D("instance", anari.INSTANCE, instances)
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

    return frame, instances


def render(frame):
    frame.render()
    return np.array(frame.get("channel.color"))


def coverage(pixels) -> int:
    # The background has no green, so the green channel is the geometry mask.
    return int(np.count_nonzero(pixels[:, :, 1] > 0.05))


def main() -> int:
    device = anari.newDevice("moonray")

    single_frame, _ = build(device, (0.0,))
    one = coverage(render(single_frame))
    check(one > 0, "nothing was rendered for a single instance")

    frame, instances = build(device, OFFSETS)
    pixels = render(frame)
    three = coverage(pixels)
    print(f"coverage: {one} for one instance, {three} for three")
    check(three > 2.5 * one, "three instances must cover about three times as much")

    pixels_per_unit = SIZE / (2.0 * CAMERA_DISTANCE * math.tan(math.pi / 6.0))

    def lit_column(x_world: float) -> bool:
        column = int(SIZE / 2.0 + x_world * pixels_per_unit)
        return bool((pixels[:, column, 1] > 0.05).any())

    for x in OFFSETS:
        check(lit_column(x), f"the instance at x={x} is missing")
    for x in (-1.25, 1.25):
        check(not lit_column(x), f"the gap at x={x} is filled")

    # Moving one instance moves only that copy - the other two stay.
    instances[0].setParameter("transform", anari.FLOAT32_MAT4, translation(-2.5, 2.0))
    instances[0].commitParameters()
    moved = render(frame)
    print(f"coverage after moving one instance: {coverage(moved)}")
    check(coverage(moved) > 2.5 * one, "moving one instance lost the others")

    if failures:
        print(f"{failures} check(s) FAILED")
        return 1

    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
