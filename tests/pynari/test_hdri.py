"""pynari.hdri — the hdri light through PyNARI (Milestone 10).

Two of the derived checks the C++ `anari.hdri_light` test makes, from Python:

  * an environment of constant radiance L illuminates a surface with
    E = pi*L, so a Lambertian surface of albedo a renders a*L;
  * a map that is lit only above its equator renders the same a*L when `up`
    is the surface normal, and goes black when `up` is flipped - the lit half
    of the map is then behind the surface.
"""

import math
import sys

import numpy as np

import _dll_path  # noqa: F401  (must precede the pynari import)
import pynari as anari

SIZE = 64
ALBEDO = (0.8, 0.2, 0.2)
BACKGROUND = (0.0, 0.0, 1.0, 1.0)
MAP_W, MAP_H = 32, 16
RADIANCE = 0.5

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

    light = device.newLight("hdri")
    light.setParameter("up", anari.float3, (0.0, 0.0, 1.0))
    light.setParameter("direction", anari.float3, (1.0, 0.0, 0.0))

    # The array's first row is the *lower* edge of the environment.
    constant = np.full((MAP_H, MAP_W, 3), RADIANCE, dtype=np.float32)
    light.setParameter(
        "radiance", anari.ARRAY2D, device.newArray2D(anari.FLOAT32_VEC3, constant)
    )
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

    def render_center():
        frame.render()
        return np.array(frame.get("channel.color"))[SIZE // 2, SIZE // 2]

    lit = ALBEDO[0] * RADIANCE
    center = render_center()
    print(f"constant environment: {center[0]:.4f} (expected {lit:.4f})")
    check(
        abs(float(center[0]) - lit) <= 0.04 * lit + 0.005,
        f"a constant environment of radiance {RADIANCE} must render {lit}",
    )

    # Only the upper half of the map emits. The triangle's normal is +Z, so
    # with up = +Z that half is exactly the hemisphere it can see.
    upper = np.zeros((MAP_H, MAP_W, 3), dtype=np.float32)
    upper[MAP_H // 2 :, :, :] = RADIANCE
    light.setParameter(
        "radiance", anari.ARRAY2D, device.newArray2D(anari.FLOAT32_VEC3, upper)
    )
    light.commitParameters()

    center = render_center()
    print(f"upper half lit, up = +Z: {center[0]:.4f} (expected {lit:.4f})")
    check(
        abs(float(center[0]) - lit) <= 0.06 * lit + 0.005,
        "the top of the map must be the +up hemisphere",
    )

    light.setParameter("up", anari.float3, (0.0, 0.0, -1.0))
    light.commitParameters()

    center = render_center()
    print(f"upper half lit, up = -Z: {center[0]:.4f} (expected ~0)")
    check(
        float(center[0]) < 0.1 * lit,
        "flipping 'up' must move the lit half of the map out of sight",
    )

    if failures:
        print(f"{failures} check(s) FAILED")
        return 1

    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
