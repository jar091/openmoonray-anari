# openmoonray-anari

A [Khronos ANARI](https://registry.khronos.org/ANARI/) rendering device backed by the
[OpenMoonRay](https://github.com/OpenMoonRay/openmoonray) production renderer, together
with the Windows portability work required to make the OpenMoonRay core usable on
**Windows, Linux, and macOS** from one codebase.

> **Project shape:** cross-platform OpenMoonRay core + one shared ANARI backend —
> *not* a Windows-only MoonRay fork.

## Status

**Milestones 1-13, 15 and 16 are complete on Windows (2026-08-19)**, with 14
done except for its non-Windows half. The device
renders triangles, spheres, curves and volumes, with matte and physically based
materials, image textures, four light types including an environment map,
retained-mode updates with MoonRay-native instancing, colour and depth
channels — from C and from PyNARI — and on the CPU or, when the build has it,
on the GPU through MoonRay's XPU mode. Every expectation is *derived* (a
Lambertian surface under irradiance π renders its albedo; a volume transmits
`exp(-σL)`), not eyeballed. The project suite is **69/69** on Windows, and the
ANARI CTS runs end to end with its result recorded in `tests/cts/`.

macOS is at Milestone 7 state and needs a run of everything since; Linux still
needs a host. See [STATUS.md](STATUS.md), [MILESTONES.md](MILESTONES.md) and
[SUPPORTED_FEATURES.md](SUPPORTED_FEATURES.md) — the last one is the honest
list of what is implemented and what is deliberately not.

## Goal

A dynamically loadable ANARI device named `moonray`:

```cpp
ANARILibrary lib = anariLoadLibrary("moonray", statusCallback, userPtr);
ANARIDevice device = anariNewDevice(lib, "default");
```

usable from any ANARI C/C++ application and from [PyNARI](https://github.com/ingowald/pynari):

```python
import pynari as anari

device = anari.newDevice("moonray")
```

Key properties:

- the real OpenMoonRay renderer is the backend — no stubs, no fake images;
- CPU rendering first on all three platforms; XPU/GPU is optional and, on
  Windows, working (`moonray.executionMode = "xpu"`);
- Windows is a normal third platform, developed with Visual Studio / MSVC;
- upstream Linux and macOS behavior is preserved;
- portability changes are kept clean and plausibly upstreamable.

## Pinned baseline

| Dependency | Version / revision |
|---|---|
| OpenMoonRay | `v2026.29.1` |
| ANARI specification | 1.1 |
| ANARI-SDK | `v0.16.0` |
| PyNARI | `mjar/devel` @ `de99e5064e506f374bc614d56bb9120c71db818c` |

Machine-readable source of truth: [cmake/Dependencies.cmake](cmake/Dependencies.cmake).
Details and submodule revisions: [docs/BASELINE.md](docs/BASELINE.md),
[DEPENDENCIES.md](DEPENDENCIES.md).

## Building

See [BUILDING.md](BUILDING.md). Windows quick start:

```powershell
.\scripts\bootstrap-windows.ps1
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug --output-on-failure
```

## Documentation map

| Document | Content |
|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | Layering, ANARI object model, RDL2 translation design |
| [MILESTONES.md](MILESTONES.md) | Roadmap and per-milestone acceptance criteria |
| [STATUS.md](STATUS.md) | Current concrete state, updated every work session |
| [SUPPORTED_FEATURES.md](SUPPORTED_FEATURES.md) | Truthful ANARI feature matrix |
| [TESTING.md](TESTING.md) | Test layers, naming, labels |
| [PYNARI.md](PYNARI.md) | PyNARI integration and examples |
| [DEPENDENCIES.md](DEPENDENCIES.md) | Dependency strategy and pins |
| [docs/HISTORICAL_WINDOWS_PORT_AUDIT.md](docs/HISTORICAL_WINDOWS_PORT_AUDIT.md) | Audit of the prior Windows/Blender port work |
| [docs/WINDOWS_PORT.md](docs/WINDOWS_PORT.md) | Windows portability changes and upstream readiness |
| [docs/adr/](docs/adr/) | Architecture decision records |

## Development

AI agent harnesses were used for selected development, testing, and optimization tasks

## License

Apache-2.0, matching upstream OpenMoonRay. See [LICENSE](LICENSE) and
[THIRD_PARTY.md](THIRD_PARTY.md).
