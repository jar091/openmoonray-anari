# Dependencies

## How the pins are recorded

Upstream sources are **git submodules** under `external/` (see `.gitmodules`);
the submodule commits *are* the pins:

```bash
git clone --recursive https://github.com/jar091/openmoonray-anari.git
# or, in an existing clone:
git submodule update --init --recursive
```

| Submodule | Points at | Why |
|---|---|---|
| `external/openmoonray` | `github.com/jar091/openmoonray`, branch `windows-port` | our fork: the MoonRay libraries carry the Windows/MSVC port (docs/WINDOWS_PORT.md). Its own `.gitmodules` uses **absolute** https URLs: `scene_rdl2`, `moonray`, `moonshine`, `mcrt_denoise` and `cmake_modules` point at the `github.com/jar091` forks, everything else at `github.com/OpenMoonRay` |
| `external/anari-sdk` | `github.com/jar091/ANARI-SDK` | fork of `KhronosGroup/ANARI-SDK`; the pinned commit is the unmodified upstream release |
| `external/pynari` | `github.com/jar091/pynari`, branch `mjar/devel` | fork of `ingowald/pynari`; the one change this project needs (PYNARI.md) lives as a commit on that branch, so no build script patches the checkout. In the checkout `origin` is the fork and `upstream` the original |
| `external/vcpkg` | `github.com/microsoft/vcpkg` | unmodified upstream; the release tag must match `builtin-baseline` in `scripts/vcpkg/vcpkg.json` |

Everything else under `external/` is build output and is git-ignored:
`vcpkg-installed/`, `tools/` (prebuilt ISPC/OIDN/winflexbison),
`installs/` (MoonRay prefix), `install/` (ANARI-SDK prefix).

The Windows-port branches live in the `github.com/jar091` forks of the
upstream repositories and are published with a plain `git push`, innermost
repository first (`scripts/publish-all.ps1` / `publish-all.sh`);
`scripts/publish-windows-port.ps1` is only needed to mirror them to a GitLab
instance.

The version table below stays as the human-readable record; the machine
-readable source of truth is now the submodule state (`git submodule status`).

## Pinned baseline

Verified against authoritative upstream sources on **2026-08-16**
(decision record: [docs/adr/0001-openmoonray-version.md](docs/adr/0001-openmoonray-version.md)).
Machine-readable source of truth: [cmake/Dependencies.cmake](cmake/Dependencies.cmake).

| Dependency | Pin | Source |
|---|---|---|
| OpenMoonRay (top-level) | `v2026.29.1` (tag object `77d79ee990c20b74fe6bbb241ade45516978957f`, released 2026-07-17) | https://github.com/jar091/openmoonray (fork of https://github.com/OpenMoonRay/openmoonray; the port sits on top of the tag) |
| ↳ `moonray/scene_rdl2` | `d3da034` | submodule of the tag; port in https://github.com/jar091/scene_rdl2 |
| ↳ `moonray/moonray` | `eef67ae` | submodule of the tag; port in https://github.com/jar091/moonray |
| ↳ `moonray/moonshine` | `a3c8667` | submodule of the tag; port in https://github.com/jar091/moonshine |
| ↳ `moonray/materialx_shaders` | `9b83c00` | submodule of the tag (https://github.com/OpenMoonRay/materialx_shaders) |
| ↳ `moonray/mcrt_denoise` | `0050e72` | submodule of the tag; port in https://github.com/jar091/mcrt_denoise |
| ↳ `cmake_modules` | `6e4c94b` | submodule of the tag; port in https://github.com/jar091/cmake_modules |
| ANARI specification | 1.1 | https://registry.khronos.org/ANARI/ |
| ANARI-SDK | `v0.16.0` (released 2026-07-19) | https://github.com/jar091/ANARI-SDK (fork of https://github.com/KhronosGroup/ANARI-SDK) |
| PyNARI | branch `mjar/devel`, commit `de99e5064e506f374bc614d56bb9120c71db818c` (`v1.2.9-71-gde99e50`) | https://github.com/jar091/pynari (fork of https://github.com/ingowald/pynari) |

Full submodule inventory of the OpenMoonRay tag: [docs/BASELINE.md](docs/BASELINE.md).

## Windows third-party stack (Milestone 2)

Windows does not use the upstream superbuild; the MoonRay third-party set
comes from vcpkg, pinned by [scripts/vcpkg/vcpkg.json](scripts/vcpkg/vcpkg.json)
(manifest with `builtin-baseline` = vcpkg release tag `2026.07.29`) and
installed by
[scripts/build-moonray-deps-windows.ps1](scripts/build-moonray-deps-windows.ps1)
into `external/vcpkg-installed/x64-windows`. Lua is pinned to the 5.4 line
(upstream superbuild uses 5.4.4) via a manifest override; ISPC `v1.20.0` is a
prebuilt release zip in `external/tools`. Version-by-version comparison with
the Linux/macOS superbuild pins: [docs/WINDOWS_PORT.md](docs/WINDOWS_PORT.md).

**One vcpkg feature is not optional here: `embree[ray-mask]`.** The port does
not build `EMBREE_RAY_MASK` by default, and MoonRay's whole visibility system
runs on Embree ray masks — the per-geometry visibility flags, and the
separation of volume rays from surface rays. Without it every ray hits every
geometry: a volume's proxy geometry is hit by camera rays as if it were a
surface, which crashed the renderer and, once that was survivable, truncated
the volume integration to about one voxel. Anyone with an
`external/vcpkg-installed` from before 2026-08-19 has to re-run
`scripts/build-moonray-deps-windows.ps1` to pick it up.

## Optional GPU dependencies (Milestone 15)

MoonRay's XPU execution mode needs CUDA and the OptiX SDK; the CPU build needs
neither, and CPU remains the portability baseline. They are *not* vendored:

| Dependency | Version used | Where it comes from | Why |
|---|---|---|---|
| CUDA Toolkit | 12.8 | the NVIDIA installer (`nvcc` on `PATH`, or `%ProgramFiles%\NVIDIA GPU Computing Toolkit\CUDA\<version>`) | compiles `OptixGPUPrograms.cu` to PTX and provides the driver API MoonRay's GPU accelerator calls |
| OptiX SDK | 8.1.0 | the NVIDIA installer (`OPTIX_ROOT`, or `%ProgramData%\NVIDIA Corporation\OptiX SDK <version>`) | header-only; MoonRay's code was written against OptiX 7, and the three API changes are version-guarded on `OPTIX_VERSION` (docs/WINDOWS_PORT.md) |
| An NVIDIA GPU | sm_75 or newer | - | `CUDA_ARCHITECTURES 75` is what `lib/rendering/rt/CMakeLists.txt` builds for |

`scripts/build-moonray-windows.ps1 -Xpu` finds both and turns
`MOONRAY_USE_OPTIX` on. At run time the PTX is loaded from
`$REZ_MOONRAY_ROOT/shaders/OptixGPUPrograms.ptx`, so that variable has to point
at the MoonRay install (the test suite and the packaging script both set it).
Without any of this, asking the device for `moonray.executionMode = "xpu"`
falls back to `vectorized` and says so through the property of the same name.

## Version rules

0. The pins are the submodule commits; `cmake/Dependencies.cmake`, the fetch
   scripts and the table above are documentation that must be updated with
   them (a submodule bump that leaves them stale is an incomplete change).
1. Mandatory CI never builds against floating branches — only the pins above.
2. Newer upstream releases are adopted deliberately (inspect changes first),
   never because they are merely newer.
3. Mandatory CI stays CPU-capable and independent of XPU hardware.
4. Changes to the baseline update `cmake/Dependencies.cmake`, this file,
   `docs/BASELINE.md`, and the ADR.

## Dependency strategy

Two supported workflows (master prompt §24):

1. **Reproducible bundled/developer mode** — pinned OpenMoonRay/ANARI-SDK/
   PyNARI fetched by the project (submodules / ExternalProject; mechanism
   selected in Milestone 1 when the minimal graph is proven). MoonRay's own
   dependency ecosystem is reused, not reinvented.
2. **Existing installation mode** — already-built OpenMoonRay and ANARI-SDK
   via `CMAKE_PREFIX_PATH`, `OpenMoonRay_DIR`, `scene_rdl2_DIR`, `anari_DIR`.

## Minimal dependency graph

For the first ANARI CPU milestone the goal is the smallest graph that renders
an RDL2 scene in-process:

```text
scene_rdl2 -> moonray rendering core -> required DSOs -> minimal materials -> framebuffer
```

Likely non-mandatory (to be verified in Milestone 1): Arras, distributed
rendering, Hydra/hdMoonray, GUI, Houdini integration, optional XPU
dependencies. The proven graph will be documented in
[docs/MINIMAL_MOONRAY_DEPENDENCIES.md](docs/MINIMAL_MOONRAY_DEPENDENCIES.md).
