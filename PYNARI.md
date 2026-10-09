# PyNARI integration

PyNARI is a first-class requirement: the same application-level Python code
must work on Windows, Linux and macOS:

```python
import pynari as anari

device = anari.newDevice("moonray")
```

## Pinned version

- Repository: https://github.com/jar091/pynari, branch `mjar/devel` — a fork of
  https://github.com/ingowald/pynari, so the change below is carried as a
  commit. In the checkout `origin` is the fork and `upstream` is the original,
  the same convention the openmoonray copies use.
- Pinned commit: `de99e5064e506f374bc614d56bb9120c71db818c`
  (`v1.2.9-71-gde99e50`), a git submodule (`external/pynari`).

## Rules

- PyNARI remains a normal consumer of the ANARI C API — no MoonRay-specific
  rendering logic is ever added to PyNARI.
- PyNARI is not modified unless a genuine wrapper bug is found; in that case:
  prove the ANARI C backend correct first, isolate the issue, document it,
  prepare a minimal upstreamable fix.
- Examples never import MoonRay directly; scenes are built entirely through
  ANARI; every example returns nonzero on failure.

## The one PyNARI change this project carries

The fork reorders the `setParameter` overload registrations in
`pynari/bindings.cpp` so the integer ones come before the float ones. It lives
in the pinned commit, not in a patch step: the build scripts never modify the
checkout.

**Why it is needed rather than nice to have.** pybind11 tries overloads in
registration order, and — measured on pybind11 2.13 with a four-overload probe
(`float`, `tuple<float,float>`, `unsigned`, `tuple<unsigned,unsigned>`) — its
float caster accepts a Python `int` even in the first, no-conversion pass.
With the float overloads registered first, every integer-typed ANARI
parameter reaches `set_float`/`set_float2`, which reject it:

```text
RuntimeError: pynari::Object::set_float2 unsupported type ANARI_UINT32_VEC2
```

That includes a frame's `size`, so *no* scene can be rendered at all. Adding
`py::arg().noconvert()` does not help (verified with the same probe); moving
the integer overloads first does, and Python floats still reach the float
overloads because the integer casters do reject them in the first pass.

**Same trap, one row further down.** The pinned commit has no `set_bool`
overload. If one is added it belongs in the integer-first group: a Python
`bool` is a subclass of `int`, so with pybind11 ≥ 2.13 a `set_bool` registered
after the float overloads is swallowed by `set_float` exactly like an `int`
is, and `setParameter(name, ANARI_BOOL, True)` would raise "unsupported type
ANARI_BOOL". (Not verified by a test here — this project's device has no
`ANARI_BOOL` parameter yet — but it is the same mechanism, measured for `int`
and `tuple<int,int>`.)

**Upstream status.** The change is minimal and self-contained (one block moved,
plus a comment explaining the ordering constraint), and is the commit to offer
upstream. It is not MoonRay-specific: any device with an `ANARI_INT32`
or `ANARI_UINT32_VEC2` parameter hits it when PyNARI is built against
pybind11 ≥ 2.13 — which is what Python 3.13 requires, since PyNARI's vendored
pybind11 is 2.12.

## Examples (`examples/pynari/`)

| Example | Status |
|---|---|
| `00_device_info.py` | **works** (Milestone 7, Windows + macOS) — loads the device, lists the implemented object subtypes |
| `01_triangle.py` | **works** (Milestone 7, Windows + macOS) — renders the triangle and writes a PPM |
| `02_pbr_spheres.py` | planned, Milestone 9 |
| `03_textured_quad.py` | planned, Milestone 9 |
| `04_instancing.py` | planned, Milestone 8 |
| `05_dynamic_updates.py` | planned, Milestone 8 |
| `06_depth.py` | planned, Milestone 10 |
| `07_curves.py` | planned, Milestone 11 |
| `08_volume.py` | planned, Milestone 12 (only if volumes are supported) |

On Windows the examples register the ANARI/MoonRay DLL directories from
`OPENMOONRAY_ANARI_DLL_DIRS` (separated by `|`) with `os.add_dll_directory()`:
Python 3.8+ ignores `PATH` when resolving an extension module's dependencies.
TESTING.md lists the directories the CTest wiring uses.

## Tests (`tests/pynari/`, run through CTest)

| Test | Status |
|---|---|
| `pynari.load_device` | **passing** — `anari.newDevice("moonray")` plus one factory call |
| `pynari.triangle` | **passing** — the Milestone 7 scene, with the same derived expectations as the C++ test (`tests/integration/test_render.cpp`): the lit triangle's color is the material color to four decimals, the corner is the background |
| `pynari.instancing` | **passing** (M8) — one group instanced three times, checked at the columns the camera geometry puts them in, with background in the gaps |
| `pynari.dynamic_updates` | **passing** (M8) — one device, one frame, three renders with the scene changed between them and nothing recreated; the derived radiance follows |
| `pynari.pbr` | **passing** (M9) — `physicallyBased` reduced to the Lambertian case renders the base color; with the light off it renders exactly its `emissive` value |
| `pynari.texture` | **passing** (M9) — a 2×2 `image2D` on a UV quad; each quadrant shows its own texel |
| `pynari.hdri` | **passing** (M10) — a constant environment renders `albedo·L`, and flipping `up` moves the lit half of the map out of sight |
| `pynari.spheres` | **passing** (M11) — three spheres with per-sphere radii and colors, shaded through the `"color"` geometry attribute; each renders the color and cosine its position implies |
| `pynari.depth` | planned with the remaining frame channels |

The Python tests use NumPy for both the scene data (`vertex.position`,
`primitive.index`, `vertex.attribute0`, the sampler's `image`) and the image
assertions, which is how they stay as specific as their C counterparts rather
than degenerating into "it rendered".

Tests run in separate processes to isolate ANARI library and MoonRay runtime
state. Enable with `-DOPENMOONRAY_ANARI_BUILD_PYNARI_TESTS=ON`, after building
PyNARI with `scripts/build-pynari-windows.ps1` or
`scripts/build-pynari-macos.sh`.

## Build

PyNARI is built separately from the project (it needs a Python environment the
project does not otherwise require):

```powershell
.\scripts\build-pynari-windows.ps1    # .venv + numpy + pybind11, then the module
cmake --preset windows-msvc-release -DOPENMOONRAY_ANARI_BUILD_PYNARI_TESTS=ON
```

```bash
./scripts/build-pynari-macos.sh
cmake --preset macos-clang-release -DOPENMOONRAY_ANARI_BUILD_PYNARI_TESTS=ON
```

Python comes from the project-local `.venv`, never a system interpreter, and
pybind11 from that venv rather than PyNARI's vendored 2.12 (which predates
Python 3.13). The module lands in `build/pynari`; the CTest wiring points
`PYTHONPATH` at it.

## Status

Milestone 7 complete on **Windows and macOS**: `pynari.load_device` and
`pynari.triangle` pass on both, and both examples run on both. Linux needs a
host; its build script is the same shape as the macOS one.

The six M8–M11 tests above pass on **Windows** (session 8); macOS has not run
them yet. No PyNARI change was needed for any of them — instancing, retained
updates, `physicallyBased`, `image2D`, the `hdri` light and sphere geometry
with per-vertex colors all work through the wrapper as it stands, so the single
patch below is still the only one this project carries.

On macOS the module must be built against the project `.venv`, which needs
`PYBIND11_FINDPYTHON=ON`: without it pybind11 falls back to CMake's deprecated
`FindPythonInterp`/`FindPythonLibs`, which ignore `Python_EXECUTABLE` and pick
whatever `python3` comes first — here Homebrew's 3.9, producing a
`pynari.cpython-39-darwin.so` the venv interpreter cannot import.
`scripts/build-pynari-macos.sh` sets it, drops a build tree configured against
another interpreter, and verifies the built module's ABI suffix against the
venv rather than letting the mismatch surface as an import error in CTest.
