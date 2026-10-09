"""pynari.pbr — the physicallyBased material through PyNARI (Milestone 9).

Two derived checks, the same ones the C++ test makes:

  * physicallyBased with metallic 0, roughness 1 and ior 1 is the Lambertian
    material, so with irradiance pi the lit pixel is the base color;
  * emission is added to the outgoing radiance, so with the light switched off
    the surface renders exactly its emissive value.
"""

import math
import sys

import numpy as np

import _dll_path  # noqa: F401  (must precede the pynari import)
import pynari as anari

SIZE = 64
BASE_COLOR = (0.8, 0.2, 0.2)
EMISSIVE = (0.3, 0.4, 0.5)
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

    material = device.newMaterial("physicallyBased")
    material.setParameter("baseColor", anari.float3, BASE_COLOR)
    material.setParameter("metallic", anari.FLOAT32, 0.0)
    material.setParameter("roughness", anari.FLOAT32, 1.0)
    material.setParameter("ior", anari.FLOAT32, 1.0)
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

    def render_center():
        frame.render()
        return np.array(frame.get("channel.color"))[SIZE // 2, SIZE // 2]

    center = render_center()
    print(f"lambertian physicallyBased: {center[:3]} (expected {BASE_COLOR})")
    for c in range(3):
        check(
            abs(float(center[c]) - BASE_COLOR[c]) <= 0.05,
            f"channel {c} is {center[c]}, expected {BASE_COLOR[c]}",
        )

    # Emission with the light switched off.
    light.setParameter("color", anari.float3, (0.0, 0.0, 0.0))
    light.commitParameters()
    material.setParameter("emissive", anari.float3, EMISSIVE)
    material.commitParameters()

    center = render_center()
    print(f"unlit emissive: {center[:3]} (expected {EMISSIVE})")
    for c in range(3):
        check(
            abs(float(center[c]) - EMISSIVE[c]) <= 0.03,
            f"emissive channel {c} is {center[c]}, expected {EMISSIVE[c]}",
        )

    if failures:
        print(f"{failures} check(s) FAILED")
        return 1

    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
