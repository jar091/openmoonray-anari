# Supported ANARI features

Status values: `SUPPORTED` (real implementation + automated tests),
`PARTIAL`, `PLANNED`, `UNSUPPORTED`.

A feature is never marked `SUPPORTED` without automated tests, and no subtype
is advertised whose MoonRay implementation is stubbed or unavailable. What the
device advertises through introspection is
`src/anari_library_moonray/MoonrayDefinitions.json`; every entry in it appears
below with a test, and everything else is deliberately *not* advertised.

The same list is what the *library* answers with — `anariCts query-features
moonray` and `anariInfo` ask `MoonrayLibrary::getDeviceExtensions()` rather
than the device, and `cts.query_features` compares its answer against
`tests/cts/expected-features.txt` so the two can never drift apart again.

Verified on **Windows** (2026-08-19, session 8, 69/69). macOS last verified at
27/27 (session 7, before M8–M16); Linux re-verification of the device is
pending a host.

| ANARI object | Subtype | Status | MoonRay mapping | Native test | ANARI test | PyNARI test | Notes |
|---|---|---|---|---|---|---|---|
| Device | `default` | SUPPORTED | helium-based device driving a MoonRay `RenderContext` | `native.smoke_render` | `anari.load_library`, `sdk.anari_info`, `install.load_library` | `pynari.load_device` | one process-global MoonRay driver, shared by all devices (docs/adr/0005) |
| Camera | `perspective` | SUPPORTED | `PerspectiveCamera` (`focal`/`film_width_aperture`/`pixel_aspect_ratio` derived from `fovy`/`aspect`) | — | `anari.triangle_matte`, `anari.camera_update` | `pynari.triangle` | `imageRegion` not implemented; depth of field (`apertureRadius`, `focusDistance`, KHR_CAMERA_DEPTH_OF_FIELD) maps to `dof`/`dof_aperture` (the f-stop that gives the requested lens radius) and `dof_focus_distance` - verified with pynari `camera-depth-of-field.py` |
| Camera | `orthographic` | PARTIAL | `OrthographicCamera` (`film_width_aperture = height · aspect`) | — | `anari.orthographic_camera` | — | correct for the camera a scene is first rendered with; **moving it afterwards is wrong** (see the limitation below) |
| Geometry | `triangle` | PARTIAL | `RdlMeshGeometry`, `is_subd = false` | — | `anari.triangle_matte`, `anari.image_texture`, `anari.vertex_normals`, `anari.vertex_colors`, `unit.dirty_propagation` | `pynari.triangle`, `pynari.texture` | `vertex.position`, `primitive.index`, `vertex.normal`, `vertex.color`, `vertex.attribute0` (float/float2/float3/float4) → `uv_list`; the other attribute slots, primitive-rate and face-varying data warn and are ignored, and an optional attribute this device cannot use is dropped rather than invalidating the geometry (`anari.unusable_attributes`); without `vertex.normal` the geometric normal is used (`smooth_normal = false`), as ANARI specifies |
| Geometry | `sphere` | PARTIAL | `RdlPointGeometry` (MoonRay's points *are* spheres) | — | `anari.sphere_geometry` | `pynari.spheres` | `vertex.position`, `vertex.radius`, `radius`, `primitive.index`, `vertex.color`, `primitive.color`, `vertex.attribute0` (for image1D); the other attributes warn |
| Geometry | `curve` | PARTIAL | `RdlCurveGeometry` with `curve_type` linear | — | `anari.curve_geometry` | — | `vertex.position`, `vertex.radius`, `radius`, `primitive.index` (segments merged back into polylines), `vertex.color`; rendered as round tubes (`curves_subtype = round`, not MoonRay's default ray-facing ribbons); the ends are rounded caps, so a segment renders one radius longer at each end |
| Geometry | `cylinder`, `cone` | PARTIAL | tessellated into an `RdlMeshGeometry` (64 segments, fewer for very many primitives) with analytic side normals and flat cap disks | — | — | — | `vertex.position`, `primitive.index` (uint2/uint64 pairs, default consecutive pairs), `primitive.radius`/`radius` (cylinder), `vertex.radius` (cone), `caps`, `vertex.cap`, `vertex.color`/`primitive.color`/`color`, `vertex.attribute0`/`primitive.attribute0`; verified with the pynari `geometry-cylinders*`/`geometry-cones*` samples, no repo test yet |
| Geometry | `quad` | PLANNED | — | — | — | — | not advertised |
| Material | `matte` | SUPPORTED | `UsdPreviewSurface` in its Lambertian configuration (`metallic 0`, `roughness 1`, `ior 1`) | — | `anari.triangle_matte`, `anari.image_texture`, `unit.commit_snapshot` | `pynari.triangle` | constant, sampler-valued, or `color`-attribute-valued `color`; other attribute names warn |
| Material | `physicallyBased` | PARTIAL | `UsdPreviewSurface` (baseColor, metallic, roughness, ior, emissive, opacity, clearcoat) | — | `anari.physically_based` | `pynari.pbr` | `baseColor` also takes the `"color"` geometry attribute; `transmission` becomes the dielectric lobe's refraction weight (UsdPreviewSurface `opacity` = 1 - transmission with `opacityThreshold` 0; untinted); `normal`, `occlusion`, `specular*`, `thickness`, `attenuation*`, `sheen*`, `iridescence*` have no UsdPreviewSurface counterpart and warn individually |
| Sampler | `image2D` | PARTIAL | `ImageMap` bound to the material attribute, fed from a tiled+mipmapped scratch texture written with OpenImageIO's `make_texture` | — | `anari.image_texture` | `pynari.texture` | float/UFIXED8 (incl. sRGB) images, `inAttribute = attribute0`, `wrapMode` mapped to the single `wrap_around`; `filter = nearest` is emulated by writing every texel as a block of pixels (OpenImageIO has no nearest mode ImageMap can select); `inTransform` maps to ImageMap's scale/offset/rotation; `outTransform` unsupported |
| Sampler | `image1D` | PARTIAL | `ImageMap` on an N×1 scratch texture, sampling its `input_texture_coordinates`, which an `AttributeMap` reads from the geometry's exported `vertex.attribute0` | — | — | — | `inAttribute = attribute0` on triangle, sphere, cylinder and cone geometry; `filter` (nearest emulated as for image2D), `wrapMode` clamp/repeat, the 1D part of `inTransform`/`inOffset`; verified with pynari `geometry-spheres-with-sampler1D.py`, no repo test yet |
| Sampler | `image3D`, `primitive`, `transform` | PLANNED | — | — | — | — | not advertised; creating one warns |
| Light | `directional` | SUPPORTED | `DistantLight`, `intensity = irradiance / π` with `normalized` | — | `anari.triangle_matte`, `anari.light_update`, `unit.dirty_propagation` | `pynari.triangle` | `radiance` warns and falls back to `irradiance` |
| Light | `point` | SUPPORTED | `SphereLight`, `intensity = I/(π r²)`, `normalized = false` | — | `anari.point_light` | — | MoonRay has no delta light: `radius = 0` becomes `1e-3` |
| Light | `quad` | SUPPORTED | `RectLight` (center/width/height/basis derived from position + edges), `sidedness` from `side` | — | `anari.quad_light` | — | `intensityDistribution` unsupported |
| Light | `hdri` | SUPPORTED | `EnvLight` + a lat-long scratch texture; the node basis is derived from `up`/`direction` through MoonRay's fixed `sLocalOrientation` | — | `anari.hdri_light` | `pynari.hdri` | `visible` → `visible_in_camera`; a visible hdri is the backdrop (no background composited under it); the map is filtered bilinearly; while an hdri light is in the world the renderer's ambient light is not added (as in barney) |
| Surface / Group / Instance / World | — | SUPPORTED | RDL2 `GeometrySet` + `Layer` + `LightSet`; a singly-used group's transform goes on `node_xform`, a shared group becomes a prototype + `RdlInstancerGeometry` | — | `anari.instance_updates`, `anari.shared_group` | `pynari.instancing` | membership changes rebuild the `GeometrySet`/`Layer`/`LightSet`, so an object removed from the world stops rendering; a pure transform change does not (asserted through `moonray.layerRebuiltLastSync`) |
| Renderer | `default` | SUPPORTED | MoonRay batch CPU render (`RenderContext`, `RenderMode::BATCH`) | `native.smoke_render` | `anari.triangle_matte` | `pynari.triangle` | `background` (color, or an ARRAY2D image stretched over the frame, KHR_RENDERER_BACKGROUND_IMAGE), `pixelSamples`, `ambientColor`, `ambientRadiance`; MoonRay's per-hit splitting is set to one light and one BSDF sample (see the vendor parameters) |
| Frame | `channel.color` (`FLOAT32_VEC4`, `UFIXED8_VEC4`, `UFIXED8_RGBA_SRGB`) | SUPPORTED | beauty buffer (`snapshotRenderBuffer`, primary AOV) | `native.reference_*` | `anari.frame_color_float`, `anari.frame_resize` | `pynari.triangle` | background composited under the render; exact sRGB encoding; resizing rewrites the camera, whose film back depends on the frame's aspect; `accumulation` (KHR_FRAME_ACCUMULATION) averages consecutive renders of an unchanged scene, each with new noise (MoonRay's frame number advances); without it a repeated render reproduces the previous image |
| Frame | `channel.depth` (`FLOAT32`) | SUPPORTED | a depth `RenderOutput`, converted from MoonRay's camera-space z to the ANARI ray distance | — | `anari.frame_depth` | — | a pixel that hits nothing reports `+inf` |
| Frame | `channel.normal/albedo/objectId/...` | PLANNED | MoonRay AOVs | — | — | — | Milestone 11+ |
| Volume | `transferFunction1D` | SUPPORTED | a `VdbGeometry` reading an OpenVDB file the device bakes, plus a `VdbVolume` shader: ANARI's opacity becomes the density grid (÷ `unitDistance`), its color the scattering albedo | — | `anari.volume_absorption`, `anari.volume_transfer_function` | — | the transfer function is applied per voxel and MoonRay interpolates the result, where ANARI interpolates the field first — exact at voxel centers, for a linear transfer function and for `filter = "nearest"`; fields under 64 voxels per axis are resampled trilinearly onto a grid of about 64 first, so the difference stays small. The opacity is `opacity` × the alpha of `color`. Needs `embree[ray-mask]` |
| Spatial field | `structuredRegular` | SUPPORTED | the density grid's values and its linear transform (`origin` + `spacing`) | — | `anari.volume_absorption`, `anari.volume_transfer_function` | — | UFIXED8/16/32, FIXED16, FLOAT32/64 data; non-uniform `spacing` warns and uses the x spacing |

## Vendor properties

Queryable with `anariGetProperty` on the device. They exist so the update tests
can assert that a change was *incremental* rather than a full rebuild; they
describe the last `anariRenderFrame`.

| Property | Type | Meaning |
|---|---|---|
| `moonray.objectsUpdatedLastSync` | `ANARI_INT32` | RDL2 objects rewritten during the last sync |
| `moonray.objectsUnchangedLastSync` | `ANARI_INT32` | RDL2 objects the sync skipped because their ANARI version had not moved |
| `moonray.layerRebuiltLastSync` | `ANARI_BOOL` | whether the `Layer`/`GeometrySet`/`LightSet` membership had to be rewritten |
| `moonray.syncMillisecondsLastFrame` | `ANARI_FLOAT32` | how long the last scene translation took, without the render that followed it - the adapter's own cost, which whole-frame timings cannot show (docs/PERFORMANCE.md) |
| `moonray.executionMode` | `ANARI_STRING` | which of MoonRay's execution modes actually ran (`scalar`, `vectorized`, `xpu`) — not always the one the device parameter of the same name asked for, since XPU falls back without a usable GPU |

## Vendor parameters

| Device parameter | Type | Meaning |
|---|---|---|
| `moonray.executionMode` | `ANARI_STRING` | `auto` (default), `scalar`, `vectorized` or `xpu`. MoonRay has one renderer per process and this lives on its global options, so the first device to render decides for the process (docs/adr/0005) |

| Renderer parameter | Type | Meaning |
|---|---|---|
| `moonray.lightSamples` | `ANARI_INT32` | MoonRay's `light_samples`: N×N light samples per light at every hit, and N×N `volume_illumination_samples` per volume scattering event. Default 1; MoonRay's own default, 2, makes every pixel sample about twice as expensive |
| `moonray.bsdfSamples` | `ANARI_INT32` | MoonRay's `bsdf_samples`: N×N BSDF samples per lobe at every hit. Default 1 (MoonRay's own: 2) |

## Known limitations

- **An orthographic camera cannot be moved after a scene's first render.** A
  sideways move shifts the image several times too far, and a move along the
  view axis empties the frame; the same position is correct when it is the one
  the scene is first rendered with. MoonRay fixes its render space to the
  camera transform in force when the camera *object* changes
  (`pbr::Scene::updateActiveCamera`; `RenderContext` notes that "a camera
  change requires a geometry rebuild, our definition of render space depends on
  this"), and the orthographic ray generation evidently assumes the camera sits
  at that origin. Perspective cameras move correctly
  (`anari.camera_update`), so this is not worked around — it is recorded.
- **A texture is written to a file.** MoonRay samples through OpenImageIO's
  texture system, whose `accept_untiled`/`accept_unmipped` MoonRay turns off,
  so an ANARI `image2D` array is written to a scratch `.exr` produced by
  OpenImageIO's `make_texture()` (a per-process directory under the system temp
  directory, cleaned up when the sampler is released). Updating the image
  writes a new file, because MoonRay's texture cache is keyed on the path.
- **One frame renders at a time, process-wide.** MoonRay has a single global
  render driver, so `anariRenderFrame` calls queue behind each other
  (docs/adr/0005).
- **Debug configuration on Windows is not available for the device**: the
  MoonRay install is Release-only, and mixing CRTs would crash. The build
  refuses it explicitly rather than producing a broken library.
