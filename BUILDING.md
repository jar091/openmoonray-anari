# Building openmoonray-anari

> **State (Milestone 7):** on Windows *and* macOS the whole chain builds from
> a clean clone — MoonRay, the ANARI device, the tests and PyNARI — and
> renders. The steps below are the ones that were actually run; nothing here
> is aspirational. Linux still needs a host.

## Prerequisites

All platforms:

- CMake ≥ 3.25 (presets v6)
- Git
- Python 3.10+ (for PyNARI tests in later milestones)

Per platform:

| Platform | Toolchain |
|---|---|
| Windows 11 | Visual Studio 2022 (Desktop development with C++), MSVC x64 |
| Linux | GCC (preset default) or Clang, Make |
| macOS | Xcode command line tools (AppleClang), Apple Silicon supported |

## Windows (primary development platform)

The pinned sources are git submodules, so clone recursively (or run
`git submodule update --init` in an existing clone — `bootstrap-windows.ps1`
does it for you):

```powershell
git clone --recursive https://github.com/jar091/openmoonray-anari.git
cd openmoonray-anari

.\scripts\bootstrap-windows.ps1              # toolchain check
.\scripts\fetch-dependencies.ps1             # pinned submodule checkouts

.\scripts\build-moonray-deps-windows.ps1     # vcpkg stack + ISPC + OIDN + flex/bison (long)
.\scripts\build-scene-rdl2-windows.ps1       # scene_rdl2  -> external/installs/openmoonray
.\scripts\build-moonray-windows.ps1          # mcrt_denoise + moonray, same prefix
.\scripts\build-anari-sdk-windows.ps1        # ANARI-SDK   -> external/install/anari-sdk

.\scripts\build-windows.ps1 -Config Release  # the project itself
.\scripts\test-windows.ps1                   # ctest
```

Optional, for the PyNARI layer:

```powershell
.\scripts\build-pynari-windows.ps1
cmake --preset windows-msvc-release -DOPENMOONRAY_ANARI_BUILD_PYNARI_TESTS=ON
ctest --preset windows-msvc-release -L pynari
```

The Windows presets use **Ninja**, the same single-config generator the
MoonRay builds use. That is deliberate: the ANARI device links MoonRay, whose
install tree is Release-only, and a multi-config build tree cannot express
"Release only" — a Debug build of the device would link Release MoonRay and
mix CRTs. `cmake/OpenMoonRay.cmake` refuses that combination explicitly
instead of producing a library that crashes. Ninja needs `cl` on PATH, so
`scripts/build-windows.ps1` enters the VS2022 x64 developer environment for
you; to run `cmake`/`ctest` by hand, use a Developer PowerShell (or
`scripts\dev-shell-build.ps1 -Command "..."`).

No bash, WSL, MSYS or Unix tools are required for the Windows workflow.

### Windows: scene_rdl2 native build (Milestone 2)

The MoonRay third-party stack comes from vcpkg (pinned; manifest
`scripts/vcpkg/vcpkg.json`) plus a prebuilt ISPC; scene_rdl2 builds
standalone with Ninja inside a VS2022 dev environment (the Visual Studio
generator cannot drive CMake's first-class ISPC language):

```powershell
.\scripts\build-moonray-deps-windows.ps1              # vcpkg packages + ISPC + OIDN (long)
.\scripts\build-scene-rdl2-windows.ps1                # Release (default), installs
.\scripts\build-scene-rdl2-windows.ps1 -Config Debug  # Debug build, not installed
.\scripts\build-scene-rdl2-windows.ps1 -Tests         # + upstream CppUnit suites, runs ctest
```

Everything installs into `external/` (`vcpkg-installed`, `tools`,
`installs/openmoonray`); nothing touches system paths. The scene_rdl2
submodule is pinned to the fork's `windows-port` branch, which carries the
changes described in `docs/WINDOWS_PORT.md`.

With `-Tests` the upstream suites are also picked up by this project's CTest
as the `scene_rdl2.*` layer after a reconfigure — see TESTING.md.

> Do not run two builds of the same checkout at the same time: parallel
> linkers collide on `mspdbsrv` (LNK1201/LNK1168) and a concurrent test run
> can load half-rebuilt DLLs.

### Windows: MoonRay core (Milestone 3, complete)

```powershell
.\scripts\build-moonray-windows.ps1                     # mcrt_denoise + moonray, Release
.\scripts\build-moonray-windows.ps1 -Target mcrt_denoise
```

CPU-only (no OptiX/Metal), standalone projects, Ninja + first-class ISPC,
installing into the same `external/installs/openmoonray` prefix as scene_rdl2
(30 DLLs, 9 executables, 68 shader DSOs + proxies). The port is documented
change by change in docs/WINDOWS_PORT.md.

`-Target moonshine` builds the shader library (the Dwa materials and the
pattern maps) and `-Target anari_dso` the shaders of this repository (`dso/`,
see SUPPORTED_FEATURES.md) against that installed MoonRay; neither is part of
the default target.

### Windows: ANARI-SDK

```powershell
.\scripts\build-anari-sdk-windows.ps1 [-Config Release|Debug]
```

Installs the pinned SDK (v0.16.0) into `external/install/anari-sdk`, which the
top-level CMakeLists puts on `CMAKE_PREFIX_PATH` automatically.
`BUILD_EXAMPLES=ON` is required: it is what builds `anariInfo`, which the
`sdk.anari_info` test runs against the device.

## Linux

```bash
./scripts/bootstrap-linux.sh
cmake --preset linux-gcc-release
cmake --build --preset linux-gcc-release
ctest --preset linux-gcc-release --output-on-failure
```

## macOS

```bash
./scripts/bootstrap-macos.sh
./scripts/fetch-dependencies.sh

./scripts/build-moonray-deps-macos.sh   # third-party chain (long; no USD/Qt)
./scripts/build-moonray-macos.sh        # scene_rdl2 + mcrt_denoise + moonray
./scripts/build-anari-sdk-macos.sh      # ANARI-SDK -> external/install/anari-sdk

cmake --preset macos-clang-release
cmake --build --preset macos-clang-release
ctest --preset macos-clang-release --output-on-failure
```

`build-moonray-macos.sh --tests` additionally builds and runs the upstream
scene_rdl2 CppUnit suites (the counterpart of `-Tests` on Windows); a
reconfigure then registers them as this project's `scene_rdl2.*` layer.

Optional, for the PyNARI layer:

```bash
./scripts/build-pynari-macos.sh
cmake --preset macos-clang-release -DOPENMOONRAY_ANARI_BUILD_PYNARI_TESTS=ON
ctest --preset macos-clang-release -L pynari
```

All of the above has been run on macOS (Apple silicon, AppleClang 21): the
suite is 36/36 there — the 27 tests Windows runs, plus the 9 upstream
scene_rdl2 suites `--tests` builds.

Python tooling (ANARI-SDK code generation, later PyNARI tests) uses a
project-local virtualenv — create it once and pass it to CMake:

```bash
python3 -m venv .venv
cmake --preset macos-clang-release -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
```

### macOS: MoonRay native baseline (Milestone 1)

The ANARI-SDK is built into `external/install/anari-sdk` (see below); the
MoonRay dependency stack and MoonRay itself build via the upstream superbuild
wrapped by two project scripts (everything installs into `external/installs`,
nothing touches `$HOME` or system paths; OpenSSL and CppUnit prerequisites
come from Homebrew where noted in the scripts):

```bash
./scripts/build-moonray-deps-macos.sh   # third-party chain (long; no USD/Qt)
./scripts/build-moonray-macos.sh        # scene_rdl2 + mcrt_denoise + moonray, standalone
```

After that, reconfigure the project — `tests/moonray_native/` finds the
MoonRay install automatically and enables `native.smoke_render`.

## Install

```bash
cmake --install build/<preset> --prefix <install-prefix>
```

The install tree carries the diagnostics tool, the license notices and the
ANARI device library (`bin/anari_library_moonray.dll`, `lib/` on Unix). It is
exercised by the `install.tree` + `install.load_library` tests, which load the
device from the installed prefix rather than the build tree.

MoonRay itself is **not** copied into this project's install tree: it is a
separate installation (`external/installs/openmoonray` by default), and the
device finds it the way any consumer does — through the loader search path,
plus `RDL2_DSO_PATH` for the shader DSOs. Packaging the two together is
Milestone 14.

## Project options

| Option | Default | Meaning |
|---|---|---|
| `OPENMOONRAY_ANARI_BUILD_TESTS` | ON | Build tests (CTest) |
| `OPENMOONRAY_ANARI_BUILD_EXAMPLES` | ON | Build C++ examples (later milestone) |
| `OPENMOONRAY_ANARI_BUILD_PYNARI_TESTS` | OFF | PyNARI tests (needs Python + pynari) |
| `OPENMOONRAY_ANARI_BUILD_CTS` | OFF | ANARI CTS integration |
| `OPENMOONRAY_ANARI_FETCH_DEPENDENCIES` | OFF | Fetch pinned dependencies (later milestone) |
| `OPENMOONRAY_ANARI_ENABLE_ASAN` | OFF | AddressSanitizer |
| `OPENMOONRAY_ANARI_ENABLE_UBSAN` | OFF | UBSan (not on MSVC) |
| `OPENMOONRAY_ANARI_WARNINGS_AS_ERRORS` | OFF | `/WX` / `-Werror` on project targets |
| `OPENMOONRAY_ANARI_ENABLE_STATIC_DSO_REGISTRY` | OFF | Bootstrap static SceneClass registration |
| `OPENMOONRAY_ANARI_ENABLE_XPU` | OFF | Optional XPU acceleration (late milestone) |

Machine-local configuration belongs in `CMakeUserPresets.json` (git-ignored) —
never commit absolute local paths.

## Using an existing OpenMoonRay / ANARI-SDK build

Planned mechanism (activated once the project links against them):

```text
CMAKE_PREFIX_PATH
OpenMoonRay_DIR
scene_rdl2_DIR
anari_DIR
```
