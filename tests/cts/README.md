# The ANARI CTS against this device

Milestone 13. The Khronos ANARI Conformance Test Suite (`anariCts`, built from
the SDK with `scripts/build-anari-sdk-windows.ps1 -Cts`) drives a device
through ~330 scene cases. This directory records what it says about the
MoonRay device, so that a change which breaks something the CTS exercises shows
up as a difference against a file rather than as nothing at all.

**What the CTS proves here, and what it does not.** Two of its three commands
are absolute: `query-features` and `check-properties` ask the device what it
supports, and their answers are either truthful or not. The third, `run`,
scores rendered images against **ground truth produced by another device** -
`helide`, the SDK's reference ray caster. MoonRay is a path tracer: it has
shadows, indirect light and a different tone response, so a low PSNR against
helide is the expected outcome for a correct render, not a defect. The recorded
baseline is therefore a *regression* record, not a conformance score, and the
interesting number in it is not "how many passed" but "which cases changed".

## What is recorded

| file | what it is |
|---|---|
| `expected-features.txt` | `anariCts query-features moonray` - the extensions the *library* advertises. Checked by `cts.query_features`. |
| `expected-properties.txt` | `anariCts check-properties moonray` - which of the catalog's tests the device can run and which it skips, with the extension each skip is missing. Checked by `cts.check_properties`. |
| `expected-baseline.json` | the per-case, per-channel outcome of a full `anariCts run`. Not a CTest: the run takes about ten minutes. |

The first two are CTests because they are fast and exact. The image run is a
manual step, below.

## Running the image comparison

```powershell
$cts = "external/install/anari-sdk/bin/anariCts.exe"
& $cts generate --workdir build/cts --device helide --width 128 --height 128
& $cts run      moonray --workdir build/cts --width 128 --height 128
& $cts report   build/cts --html build/cts/report.html
```

The device library and MoonRay must be on `PATH`, and `RDL2_DSO_PATH` must
point at MoonRay's `rdl2dso` - the same environment `ctest` sets up.

Recorded on Windows, 2026-08-19, 128x128: **5 passed, 101 failed, 223
skipped, of 329**, and two consecutive runs produce identical results.

## Why the failures fail

Every failing case was looked at rather than counted. They fall into four
groups.

**1. The reference device's camera default (13 cases).** `camera/perspective`,
`camera/orthographic` and `camera/camera_general` place the camera at
`(0.5, 0.5, -1)` and leave `direction` unset. The ANARI specification's default
is `(0, 0, -1)` (`code_gen/api/khr_camera_perspective.json`), which points away
from the geometry at `z = 0`; helide uses `(0, 0, 1)`
(`src/devices/helide/camera/Camera.cpp`), which points at it. This device
follows the specification, so it renders the empty background the specification
asks for and helide's ground truth shows the triangles. The one camera case
that sets `direction` explicitly - `camera_general/0.20601-0-0.97855_none_none`
- passes.

**2. Not implemented, and said so (4 cases).** `primitive.color` on triangles,
spheres and curves. The device warns (`'primitive.color' is set but not
implemented by this device's geometry`) and the material's attribute binding
finds nothing, which renders black by design rather than white by accident.
SUPPORTED_FEATURES.md lists it as unimplemented.

**3. A black ambient light is black (2 cases).** `renderer_ambient` with
`ambientColor = (0,0,0)` or `ambientRadiance = 0` has no light in the scene at
all; this device renders what that means.

**4. Path tracing is not ray casting (the rest).** The silhouettes agree - the
geometry is where it should be, which is what the `instance`, `geometry` and
`volume` cases show - and the shading does not. These sit around PSNR 8-16
against a threshold of 20.

## What running it found

The CTS is the only thing in this project that drives ~200 scenes through one
device in one process, and that is where it earned its keep:

- `MoonrayLibrary::getDeviceExtensions()` returned `nullptr`, so anything that
  asked the *library* what the device supports - the CTS, `anariInfo` - was
  told "nothing".
- An unusable optional attribute (a scalar `vertex.attribute0`, which the
  specification allows) discarded the whole geometry instead of the attribute.
  Now `anari.unusable_attributes`.
- A world that becomes empty crashed the process: RDL2 cannot delete a
  SceneObject, MoonRay keeps the primitives it built from one, and shrinking
  the layer left their assignment IDs pointing past its end - `IndexError` on a
  render thread, `std::terminate`, and no message at all. Now
  `anari.scene_churn`, and the layer no longer shrinks.
- A new ANARI object allocated at a destroyed one's address inherited its RDL2
  object, and the frame came back with no geometry in it - intermittently, in
  about one generation in ten. RDL2 objects are now named after
  `Object::syncId()`, which is never reused. Now `anari.scene_generations`.

`OPENMOONRAY_ANARI_CRASH_TRACE=1` (src/anari_library_moonray/Diagnostics.cpp)
is what made the third of those findable: on Windows both `abort()` and the
CRT's invalid-parameter handler end in `__fastfail`, which bypasses every
handler and kills the process silently.
