# Architecture

This document describes the architecture. Sections marked **[implemented]**
describe what the code does today (Milestone 7: the device renders through
MoonRay on Windows); sections marked **[target]** are still design intent.

## 1. Layering [implemented]

```text
                  ANARI application / PyNARI
                            |
                            v
                       ANARI loader
                            |
                            v
                  anari_library_moonray
                            |
                            v
                   MoonRay ANARI adapter
                            |
                            v
                   OpenMoonRay core APIs
                            |
                +-----------+-----------+
                |           |           |
             Windows      Linux       macOS
```

Principles:

- **One shared adapter.** There is no per-OS adapter; the adapter must not know
  which OS the renderer runs on except for unavoidable loader/install details.
- **No Hydra in the runtime path.** ANARI talks to `scene_rdl2` / MoonRay native
  APIs directly. `hdMoonray` is a mapping reference only.
- **Platform code lives below the adapter**: in `scene_rdl2` portability code,
  MoonRay low-level helpers, build/dependency discovery, DSO loading
  abstraction, and install/runtime lookup.

## 2. ANARI object model and RDL2 translation [implemented]

ANARI objects are **not** equated 1:1 with RDL2 objects. The adapter keeps its
own retained state and translates on commit:

```text
ANARI application
      |
      v
ANARI retained object model        (helium-based: parameter staging,
      |                             commit snapshots, lifetime tracking)
      | commit / dirty propagation
      v
MoonRaySceneAdapter
      |
      +-------------------+
      | object cache      |
      | RDL2 scene state  |
      | dependency graph  |
      +-------------------+
                |
                v
          SceneContext (scene_rdl2)
                |
                v
          MoonRay renderer
```

Rationale: lifetimes differ, commit semantics differ, one ANARI object may map
to several RDL2 objects, and RDL2 objects sometimes must be reconstructed
rather than mutated.

As implemented (Milestone 7), the translation lives in
`src/anari_library_moonray/backend/`:

- **`MoonrayBackend`** is the process-global MoonRay driver. MoonRay allows
  one renderer per process and requires `initGlobalDriver()`,
  `RenderContext` construction, `startFrame()` and `stopFrame()` on the *same*
  thread, so the backend owns a dedicated thread and every call into MoonRay
  goes through `execute()`. It is refcounted and shared by all devices in the
  process (docs/adr/0005).
- **`MoonrayScene`** owns one `RenderContext` (and therefore one RDL2
  `SceneContext`) per device, translates the ANARI scene into it, and runs the
  render. Each ANARI object maps to an RDL2 object named
  `/anari/<kind>/<SceneClass>/<address>`, so the same ANARI object maps to the
  same RDL2 object across frames — the objects are *reused*, not recreated.
- Each ANARI object subtype knows its own RDL2 target: `rdl2ClassName()` plus
  `syncRdl2()`, called by `MoonrayScene` on the backend thread inside an
  `UpdateGuard`. That keeps every parameter mapping next to the parameters it
  maps (docs/ANARI_MOONRAY_MAPPING.md).

Dirty state is currently coarse: the scene is re-synced on every frame and
`RenderContext::setSceneUpdated()` is called, which is correct (helium
guarantees only committed parameter snapshots are visible) but does more work
than necessary. Explicit version counters and per-object dirty bits are
Milestone 8/16 — the point at which they can be *measured* rather than
assumed.

## 3. Source layout [implemented]

```text
src/anari_library_moonray/
  MoonrayLibrary.cpp        ANARI library entry points
  MoonrayDevice.*           the device: object factories, queries, properties
  MoonrayDeviceGlobalState  device-wide state (owns the MoonrayScene)
  Object.*                  object base (validity on top of helium::BaseObject)
  backend/                  MoonrayBackend (driver thread), MoonrayScene (RDL2)
  array/                    helium arrays (aliased)
  camera/                   Camera base + Perspective
  geometry/                 Geometry base + Triangle
  material/                 Material base + Matte
  light/                    Light base + Directional
  renderer/                 the default renderer
  frame/                    frame, channel conversion, background compositing
  surface/, world/          surface, group, instance, world
  sampler/, spatial_field/, volume/   not implemented yet (unknown subtypes)
  MoonrayDefinitions.json   introspection input (what the device advertises)
src/portable/               project-owned portability helpers (unit-tested)
src/diagnostics/, src/version/
```

The layout follows the ANARI-SDK `helium`-based device conventions
(docs/adr/0004). An unknown subtype instantiates the *base* class of its
category, which reports `isValid() == false`, so the scene translation skips
it and no vtable of a different class is ever called through a base pointer.

## 4. Windows portability architecture [target]

```text
portable standard C++
       |
platform abstraction only where needed
       |
Win32/POSIX implementation details
```

- Prefer `std::filesystem`, `std::chrono`, `std::thread`, fixed-width integers
  over per-platform reimplementations — without churning proven renderer code.
- RDL2 binary serialization uses explicit widths; the on-disk format is never
  silently changed (LP64 vs LLP64).
- Narrow, single-purpose macros: `_WIN32` (platform),
  `MOONRAY_STATIC_DSO_REGISTRY` (DSO strategy), `MOONRAY_ENABLE_XPU`
  (acceleration). Never one umbrella macro like the historical `WITH_BLENDER`.
- Logging and assertions are never globally disabled on Windows.

## 5. DSO / SceneClass strategy [target]

Two staged modes (decision record: `docs/adr/0003-dso-strategy.md` when written):

1. **Bootstrap:** optional static registration of a minimal, *fully functional*
   SceneClass subset (`OPENMOONRAY_ANARI_ENABLE_STATIC_DSO_REGISTRY`).
   Historical precedent used static registration with stub implementations —
   this project requires full implementations.
2. **Production:** clean dynamic-library abstraction
   (`dlopen`/`dlsym`/`dlclose` vs `LoadLibraryExW`/`GetProcAddress`/`FreeLibrary`)
   with tested error paths (missing plugin, missing symbol, wrong architecture,
   missing dependency DLL, paths with spaces).

## 6. Frame rendering flow [implemented]

```text
anariRenderFrame
  -> Frame::renderFrame()            (application thread)
       flush helium's commit buffer
       refuse an incomplete frame with an ANARI error, never a blank image
       start an async worker
  -> MoonrayScene::render()          (worker thread)
       MoonrayBackend::execute(...)  (serialized onto the backend thread)
         create the RenderContext on first use
         sync camera / lights / geometry+material / layer / scene variables
         setSceneUpdated(); initialize() once; startFrame()
         poll isFrameComplete(); stopFrame()
         snapshotRenderBuffer() into a device-owned float4 buffer
  -> Frame::writePixels()            (worker thread)
       composite the renderer's background under the beauty buffer
       convert to the requested channel.color format
  -> anariFrameReady / anariMapFrame (application thread)
```

`anariDiscardFrame` forwards to `RenderContext::requestStop()`, which is the
same global cancel flag MoonRay's own signal handling uses, and then waits.

Frame channels use real MoonRay buffers. `channel.color` is the beauty AOV;
`channel.depth` is **not** implemented and `anariMapFrame` says so (null, size
0, `ANARI_UNKNOWN`) rather than synthesizing anything.
