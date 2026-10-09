# Testing

CTest is the single test entry point. Every test has a specific name, a
deterministic failure condition, and requires no manual image inspection.

## Test layers

| Layer | Directory | Prefix | Exists since |
|---|---|---|---|
| Skeleton smoke | `tests/smoke/` | `smoke.` | Milestone 0 |
| scene_rdl2 portability | `tests/scene_rdl2/` (registers the upstream CppUnit suites) | `scene_rdl2.` | Milestone 2 |
| Native MoonRay (no ANARI) | `tests/moonray_native/` | `native.` | Milestone 1 |
| Adapter unit tests | `tests/unit/` | `unit.` | Milestone 6 |
| ANARI integration (via `anariLoadLibrary`) | `tests/integration/` | `anari.` | Milestone 5 |
| ANARI debug layer | `tests/integration/` | `debug.` | Milestone 6 |
| Render regression metrics | `tests/render/` | `render.` | Milestone 7 |
| Retained-mode update tests | `tests/integration/test_updates.cpp` | `anari.` | Milestone 8 |
| Adapter cost / update incrementality | `tests/bench/` | `bench.` | Milestone 16 |
| PyNARI | `tests/pynari/` | `pynari.` | Milestone 7 |
| ANARI-SDK tools | — | `sdk.` | Milestone 5 |
| CTS | `tests/cts/` | `cts.` | Milestone 13 |

## Labels

```text
portability scene_rdl2 native unit integration render pynari sdk cts bench slow gpu smoke
```

Usage:

```bash
ctest -L native      # native MoonRay tests only
ctest -L pynari      # PyNARI tests only
ctest -LE slow       # everything except slow tests
```

## Current tests

| Test | What it proves |
|---|---|
| `smoke.version_info` | toolchain + CTest wiring + pinned-baseline plumbing |
| `smoke.pointer_width` | 64-bit build (LLP64/LP64 sanity baseline) |
| `unit.bits` | portable clz/ctz/popcount incl. the historical `clz(0)` bug (must be 32, not 31) |
| `unit.endian` | byte-exact big-endian conversion for RDL2 length prefixes |
| `unit.aligned_memory` | `alignedMalloc`/`alignedFree` pairing and alignment guarantees |
| `unit.sincos` | portable `sincos` terminates (historical version recursed) and matches `std::sin/cos` |
| `unit.dynamic_library` | DSO loader abstraction: valid plugin, missing plugin, missing symbol, move semantics |
| `scene_rdl2.dso_registration` | SceneClass DSO error paths (missing class, non-PE image, missing `rdl2_declare`/`rdl2_create`, unresolved dependency DLL, paths with spaces, multi-entry search path with drive letters); every case must throw with a diagnosable message, never return null |
| `native.reference_{64,128}_{scalar,vectorized}` | in-process render compared against `tests/references/*.exr` by RMSE, max-abs-diff **and image mean**; each first self-checks that the local machine reproduces its own render. The mean is the tight cross-platform bar — see "What the cross-platform comparison can assert" below |
| `unit.varint` | byte-level RDL2-compatible LEB128+zig-zag codec (LLP64 regression suite, golden vectors) |
| `anari.load_library` | `anariLoadLibrary("moonray")` + device/object creation through the real ANARI loader |
| `sdk.anari_info` | the SDK's `anariInfo` tool enumerates the `default` device of the moonray library |
| `install.tree` | `cmake --install` into a test-local prefix succeeds (fixture for install tests) |
| `install.load_library` | the device loads from the **installed** tree (install dirs win the loader search) |
| `native.smoke_render` | (built only when MoonRay is found) in-process native CPU render of a triangle, metric-validated |
| `unit.parameter_store` | set/unset/commit across types via the public API; `valid` property distinguishes known/unknown subtypes |
| `unit.array_lifetime` | shared-with-deleter/managed/parameter-held array ownership; deleter timing is asserted exactly |
| `unit.object_references` | retain/release pairing keeps handles usable; device destruction reports leaked handles |
| `unit.commit_snapshot` | a parameter change has **no** effect until `anariCommitParameters()` on that object — judged by re-rendering, which is the only externally visible answer |
| `unit.dirty_propagation` | committing a material, a geometry or a light **alone** reaches the frame (color changes, coverage shrinks, the image goes black) without re-committing surface/world/frame |
| `anari.triangle_matte` | the Milestone 7 scene through the public C API: the lit triangle's color equals the material color (derived: `L = albedo · E / pi`, rendered with `E = pi`), the corners are the renderer's background, alpha is 1 on the triangle, `duration` is reported |
| `anari.frame_color_float` | `channel.color` in `FLOAT32_VEC4` is the reference; the `UFIXED8_RGBA_SRGB` channel must be that image sRGB-encoded to within one quantization step |
| `anari.frame_depth` | `channel.depth` is the distance **along the ray**, not MoonRay's camera-space z: the triangle sits in a plane 3 units from the camera, so the on-axis pixel reads 3.0 and the most off-axis lit pixel reads `3·sqrt(1+u²+v²)`; a pixel that hits nothing must read `+inf`, because 0 would put the background at the camera |
| `anari.camera_update` | moving the camera between renders moves the image, without re-committing anything else |
| `anari.material_update` | committing a new material color changes the rendered color and rewrites exactly one RDL2 object |
| `anari.light_update` | changing the light's irradiance/direction changes the image, and the new radiance is the derived one |
| `anari.instance_updates` | moving an instance moves the triangle in the image without changing how much of it is visible, rewrites **one** RDL2 object (`moonray.objectsUpdatedLastSync`) and does **not** rebuild the layer |
| `anari.shared_group` | one group instanced three times covers ~3× what one does, the gaps between them stay background (columns computed from the camera, not eyeballed), and moving one copy rewrites only the instancer and its prototype |
| `anari.physically_based` | `physicallyBased` driven to `ior 1`/`metallic 0`/`roughness 1` reproduces `matte` **exactly**; `metallic 1` renders measurably differently; an unlit surface with `emissive` set renders exactly its emissive value |
| `anari.image_texture` | a checkerboard `image2D` bound to the material's `color` renders its texel values through `vertex.attribute0` — the white texel must come out 1.0, which is what catches MoonRay multiplying the bound map by the attribute |
| `anari.orthographic_camera` | size does not change with distance (the same scene three times further away covers the same pixels) and `height = 4` puts the 2×2 triangle on 1/8 of the frame |
| `anari.point_light` | the inverse-square law: the lit pixel is `albedo·I/(π d²)` and doubling the distance quarters it |
| `anari.quad_light` | `E = L·A·cos/d²` to 5%, and a one-sided quad turned around stops illuminating the surface |
| `anari.hdri_light` | the irradiance from a uniform environment is `πL`, so a constant map renders `albedo·L`; a map lit only above its equator renders the same with `up` along the surface normal and goes black when `up` is flipped; and rotating `direction` by 90° moves the lit half of the map out of the visible hemisphere — the assertion that pins down the azimuth handedness the ANARI spec leaves open |
| `anari.frame_resize` | the fraction of the frame the triangle covers survives a resize to 96² and back to 64², and the center still renders the material color (the camera's film back is derived from the frame's aspect, so a resize has to rewrite it) |
| `anari.sphere_geometry` | a sphere of radius r at distance d covers the disc its projection predicts — `π·(r/sqrt(d²−r²)/tan(fovy/2)·W/2)²` pixels, measured 0.2942 of the frame against 0.2945 derived — the point facing the camera renders the albedo exactly, and three spheres with per-sphere radii are three separate discs of the right sizes with background in the gaps |
| `anari.curve_geometry` | a linear curve is `2·radius` thick and runs one radius past each end vertex (MoonRay's linear curves are capsules); halving the radius halves the thickness; and two disjoint `primitive.index` segments leave the middle of the frame background instead of joining into one polyline |
| `anari.vertex_normals` | shading normals tilted 60° from the light dim the surface to `albedo·cos(60)` = 0.4000, against 0.8000 for the same triangle without normals |
| `anari.vertex_colors` | with `color` naming the `"color"` attribute, the rendered color is the barycentric interpolation of the three vertex colors: 1/3 each at the centroid, and 0.785/0.108/0.107 where the weights are 0.8/0.1/0.1 |
| `pynari.load_device` | `anari.newDevice("moonray")` through PyNARI, plus one object factory call |
| `pynari.triangle` | the same scene as `anari.triangle_matte`, built in Python, with the same derived expectations — the C API and the Python path must produce the same image |
| `pynari.instancing` | the `anari.shared_group` scene from Python: three instances of one group, each landing in the column the camera geometry says it should, with background in the gaps |
| `pynari.dynamic_updates` | one device, one frame, three renders with the scene changed between them and nothing recreated; the derived radiance follows (`E = π` gives the material color, doubling `E` doubles it) |
| `pynari.pbr` | `physicallyBased` reduced to the Lambertian case renders the base color, and with the light off it renders exactly its `emissive` value |
| `pynari.texture` | a 2×2 texture on a UV quad: each quadrant shows its own texel, probed at the pixels the camera geometry puts the texel centers at |
| `pynari.hdri` | the constant-environment and flip-the-pole halves of `anari.hdri_light`, with the map built as a NumPy array |
| `pynari.spheres` | three spheres with per-sphere radii and colors, and a material reading the `"color"` attribute: each renders its own color dimmed by the cosine its position implies (`3/sqrt(9+x²)`), and the gaps stay background |
| `install.pynari_triangle` | the Milestone 7 scene in Python against the **installed** device rather than the build tree — the packaging acceptance criterion, next to `install.tree` and `install.load_library` for the C API |
| `anari.volume_absorption` | Beer-Lambert: a purely absorbing volume transmits `exp(-σL)` of the background, doubling the opacity squares that, and the *same* volume at twice the voxel resolution transmits the same — the assertion that catches an extinction applied per voxel instead of per unit length |
| `anari.volume_transfer_function` | one half of the grid opaque and the other transparent: the background comes through untouched on one side and attenuated by `exp(-2·L)` on the other, which is what pins down where the field lands in world space |
| `anari.debug_layer` | the SDK's debug device wraps this one, checks every call against the specification while the Milestone 7 scene renders through it, and must report no violation — and the image must be the same one the device renders without it |
| `anari.malformed_input` | unknown subtypes for every object class, float2 positions on a triangle mesh, a geometry with no positions, an out-of-range index, a negative sphere radius, a quad light with parallel edges: each must be a *warning* and leave the object invalid, never an error and never a crash, and a correct scene must still render correctly afterwards |
| `anari.advertised_subtypes` | every subtype `MoonrayDefinitions.json` advertises, given a minimal correct parameter set, reports itself valid — "no false capability declarations" as a test rather than a promise |
| `anari.execution_mode_{scalar,vectorized,xpu}` | the same scene in each of MoonRay's execution modes (one process per mode, since the mode is process-global): scalar and vectorized must be honoured exactly, XPU reports its fallback when the build or the machine has no GPU, and all of them must render the same derived image |
| `anari.unusable_attributes` | an attribute this device cannot use costs the scene that attribute, not the geometry: a scalar `vertex.attribute0` (which ANARI allows and which the device reads as the u coordinate), a `float4` `vertex.normal`, a too-short `vertex.color` and a `float2` `vertex.radius` each leave the surface rendering its albedo, where before the geometry vanished from the frame |
| `anari.scene_churn` | one device, three worlds in turn, the middle one with no surfaces at all: the empty world renders the background and the scene after it renders its surface again. RDL2 cannot delete a SceneObject, so this is the case that used to leave MoonRay's already-built primitives pointing past the end of a shrunken layer - `IndexError` on a render thread, and the process gone without a message |
| `anari.device_lifecycle` | 64 devices created, rendered through and destroyed in one process, each required to render the derived albedo - the CTS makes one device per test case, and nothing in ANARI says a device may only be made once |
| `anari.scene_generations` | 64 scenes through **one** device, same assertion. This is what caught the recycled addresses: a new ANARI object allocated where a destroyed one had been used to inherit its RDL2 object, and about one frame in ten came back with no geometry in it |
| `cts.query_features`, `cts.check_properties` | the ANARI CTS's introspection commands compared against recorded files (`tests/cts/expected-features.txt`, `expected-properties.txt`): what the *library* advertises, and which of the CTS's 329 cases the device can run rather than skip. Both are exact - a new extension or a new skip fails the test until the recorded file is updated deliberately |
| `anari.shared_group_attributes` | a shared group whose material reads a geometry attribute, with one instance moved between the renders: at the centroid of a triangle with red, green and blue vertices every barycentric weight is 1/3, so each channel renders 0.333 - in every instance, before and after the move. This is the case that makes the prototype/instancer split delicate, and what protects the optimization that stopped rewriting the prototype |
| `bench.update_cost_large_prototype` | the same benchmark with an 80 000-triangle prototype shared by 8 instances, where a copy is visible in the time and not only in the count: moving one instance must keep the translation within a small factor of a sync with nothing to do (a ratio, so the assertion means the same on any machine) |
| `bench.update_cost_8`, `bench.update_cost_64` | the adapter's own cost: a row of instances rendered four ways (first frame, unchanged, one instance moved, material changed). The timings are printed, not asserted — what is asserted is what must not grow with the scene: 0 RDL2 objects rewritten when nothing changed, ≤ 2 for an instance move, exactly 1 for a material change, at both sizes (docs/PERFORMANCE.md) |
| `scene_rdl2.math`, `.math_ispc`, `.simd`, `.fb_util`, `.rec_time`, `.render_cache`, `.render_util`, `.scene_context` | the **upstream** scene_rdl2 CppUnit suites (built by `scripts/build-scene-rdl2-windows.ps1 -Tests` or `scripts/build-moonray-macos.sh --tests`), registered into this project's CTest by `tests/scene_rdl2/` — this is how the port's own coverage is reported. `scene_rdl2.scene_context` includes the real DSO load/proxy/declare-throws paths |
| `scene_rdl2.grid_util` | the upstream `common_grid_util` suite. POSIX-only (sockets, shared memory, NUMA), so it is registered on Linux/macOS and has no Windows counterpart |

`sdk.anari_cat` is deferred: `anariCat` is a GUI capability-analysis tool built
on `anari_viewer` (glfw/OpenGL) and cannot run headless; it is not built in the
pinned SDK configuration (`BUILD_CAT=OFF`).

## Running the PyNARI layer

PyNARI is built separately (it needs a Python environment the project does not
otherwise require), then the tests are enabled explicitly:

```powershell
.\scripts\build-pynari-windows.ps1
cmake --preset windows-msvc-release -DOPENMOONRAY_ANARI_BUILD_PYNARI_TESTS=ON
ctest --preset windows-msvc-release -L pynari
```

The CTest wiring sets `PYTHONPATH` to the built module and, on Windows, passes
the ANARI/MoonRay DLL directories in `OPENMOONRAY_ANARI_DLL_DIRS` (separated by
`|`) because Python 3.8+ ignores `PATH` when resolving an extension module's
dependencies; `tests/pynari/_dll_path.py` registers them with
`os.add_dll_directory()`.

## Runtime environment of the render tests

Anything that renders needs two things, and the CTest wiring
(`tests/CMakeLists.txt`) provides both, so no manual PATH setup is required:

- the shared libraries: the device and ANARI loader from the build tree, plus
  MoonRay's `bin/` and the vcpkg runtime — MoonRay has no RPATH on Windows.
  The prebuilt OIDN directory is deliberately **not** on that list: it ships
  its own `tbb12.dll`, and letting MoonRay load that one instead of the oneTBB
  it was built against fails with `STATUS_ENTRYPOINT_NOT_FOUND` (0xc0000139);
- `RDL2_DSO_PATH`, pointing at the installed `rdl2dso` directory, so
  scene_rdl2 finds the shader DSOs instead of looking next to the test
  executable.

On macOS/Linux the third-party stack lives in `external/installs/lib` rather
than beside MoonRay, and it is on that list too: the build-tree device finds
it through CMake's build RPATH, but the *installed* one does not, and
`install.load_library` failed with `Library not loaded:
@rpath/libosdGPU.3.5.0.dylib` until it was added.

## What the cross-platform comparison can assert

`native.reference_*` compares against references produced on Windows. Measured
on macOS (arm64/AppleClang) against them:

| mode | rmse | max | mean \|diff\| |
|---|---|---|---|
| scalar | 5.7e-3 | 2.6e-2 | 2.3e-5 … 7.7e-5 |
| vectorized | 5.6e-3 | 2.5e-2 | 2.6e-6 … 6.0e-5 |

The per-pixel spread is the same in both modes and covers ~96% of the pixels,
while each platform reproduces itself bit-identically (scalar) or to ~1 ULP
(vectorized). So the cross-platform difference is not the arithmetic or the
SIMD width but the **sample sequence** — Monte Carlo noise at 2×2 samples per
pixel. Noise is zero-mean, which is why the image means agree to five decimal
places while individual pixels are 10% apart.

Consequently the per-pixel tolerances are the same for both modes and only
catch gross divergence, and `--mean-tol` is the assertion that actually binds:
a wrong albedo, a wrong light intensity or a missing term moves the mean
immediately. On failure the tool prints how many pixels differ, how many
exceed the tolerance, and the worst pixel's coordinates and both values.

## Running the scene_rdl2 portability layer

The `scene_rdl2.*` tests execute the upstream CppUnit suites from a scene_rdl2
build tree; they are registered only when that tree contains them:

```powershell
.\scripts\build-scene-rdl2-windows.ps1 -Config Release -Tests   # builds + runs them directly
cmake --preset windows-msvc-release                             # registers scene_rdl2.* (8 suites)
ctest --preset windows-msvc-release -L scene_rdl2
```

```bash
./scripts/build-moonray-macos.sh --tests    # builds + runs them directly
cmake --preset macos-clang-release          # registers scene_rdl2.* (9 suites)
ctest --preset macos-clang-release -L scene_rdl2
```

Point `OPENMOONRAY_ANARI_SCENE_RDL2_BUILD_DIR` at a different build tree when
needed (default `build/scene_rdl2`). The loader environment (build-tree DLLs +
vcpkg runtime) is set per test, so no PATH setup is required.

## Asserting that an update was incremental

"The image changed" does not prove the device updated the scene rather than
rebuilding it, and a full rebuild would pass every M8 test while defeating the
point of a retained API. The device therefore reports what the last sync did,
and the update tests assert it:

```c
int32_t updated = 0, unchanged = 0;
int8_t layerRebuilt = 0;   /* ANARI_BOOL is int8_t, not int */
anariGetProperty(d, d, "moonray.objectsUpdatedLastSync",
    ANARI_INT32, &updated, sizeof(updated), ANARI_WAIT);
anariGetProperty(d, d, "moonray.layerRebuiltLastSync",
    ANARI_BOOL, &layerRebuilt, sizeof(layerRebuilt), ANARI_WAIT);
```

`anari.instance_updates` requires `updated == 1` and `!layerRebuilt` after
moving an instance; `anari.shared_group` requires `updated == 2` (the instancer
and its prototype — MoonRay only carries the material's attribute set for
geometries whose own assignment changed, so the prototype has to be touched
deliberately).

## When the renderer crashes on Windows

A Windows access violation is a structured exception, not a signal, so
MoonRay's own `stackTraceHandler` never runs and CTest reports
`Exception: SegFault` and nothing else. Two things in the tree fix that, and
they are what found the Milestone 12 bugs:

```powershell
.\scripts\build-moonray-windows.ps1 -Target moonray -WithDebugInfo
```

builds MoonRay with the same optimization settings plus `/Zi` and `/DEBUG`, so
the installed DLLs have PDBs; and `tests/common/CrashTrace.h` installs a
vectored exception handler that walks the *faulting* context with dbghelp and
prints a symbolized stack (`test_crash::install()` — the volume, compliance and
execution-mode tests call it). Without symbols the stack is a list of module
offsets; with them it names the function and the source line.

**A crash with no output at all** is the other Windows failure mode, and the
one the CTS ran into: `abort()` and the CRT's invalid-parameter handler both
end in `__fastfail`, which bypasses vectored handlers, `SetUnhandledException-
Filter` and everything else - the process simply disappears with status
`0xC0000409`. Setting `OPENMOONRAY_ANARI_CRASH_TRACE=1` makes the device
install handlers for those paths (src/anari_library_moonray/Diagnostics.cpp):
they print a symbolized stack, and for an exception that escaped into a
`noexcept` frame they name the type that was in flight - which is how
`scene_rdl2::except::IndexError` was found on a MoonRay render thread. It is
opt-in because it takes over process-global handlers that belong to the
application.

`OPENMOONRAY_ANARI_DEBUG_LAYER=1` prints, per scene sync, how many layer
assignments are live, how many slots the layer has, how many are retired and
whether the layer was rebuilt - which is how "the geometry is missing" is
separated from "the geometry is invisible".

The other half of that toolkit is `OPENMOONRAY_ANARI_DUMP_RDLA=<path>`, which
writes the translated scene as an `.rdla`. That is what makes it possible to
say whether a problem is in this device's translation or in MoonRay: the same
file can be rendered with `moonray -in scene.rdla`. (One caveat found while
using it: the RDLA writer does not escape backslashes, so a Windows path in a
`texture`/`model` attribute has to be turned into forward slashes before
MoonRay can read the file back.)

## Principles

- Native MoonRay render tests (`native.*`) must pass on Windows **before** any
  ANARI rendering work starts — they separate Windows portability problems
  from ANARI adapter problems.
- Integration tests load the device with `anariLoadLibrary("moonray", ...)`;
  they never link internal adapter classes.
- Render regression uses metric-based checks (finite values, coverage,
  luminance bounds, RMSE/relational assertions), not byte-identical golden
  images across MSVC/GCC/AppleClang.
- Whenever a Windows fix changes shared code, the equivalent Linux/macOS tests
  must run.
- No mandatory test is skipped merely because one OS implementation is broken.
- A test must not be able to pass when its fixtures are missing:
  `scene_rdl2.dso_registration` now verifies that its malformed helper
  plugins exist and that each case's message names *its own* failure mode
  (`rdl2_declare`, `rdl2_create`, "failed to open it"). Before that, a
  mis-pointed stub directory turned every case into the generic "couldn't find
  DSO" and the suite passed while testing nothing.

## Windows note: hard-error dialogs block CTest

`scene_rdl2.dso_registration` deliberately loads a file that is not a PE
image. Windows answers that with a *hard error*, and when the process has no
interactive desktop — which is the case under CTest — the loader sits in an
LPC wait to csrss instead of failing: the test took 110-210 s, or never
finished at all. The process-wide `SetErrorMode()` is not enough; the loader
consults the **thread's** error mode, so the call is wrapped in
`SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX)`. With
that in place the test runs in 0.2 s. Any future test that deliberately loads
a broken image needs the same treatment.
