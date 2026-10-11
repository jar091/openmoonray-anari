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
| Camera | `perspective` | SUPPORTED | `PerspectiveCamera` (`focal`/`film_width_aperture`/`pixel_aspect_ratio` derived from `fovy`/`aspect`) | — | `anari.triangle_matte`, `anari.camera_update` | `pynari.triangle` | `imageRegion` maps to the film offsets and a focal length/pixel aspect that make the aperture window the region (an off-axis frustum, the view direction is untouched; verified against crops of the full plane with `build/graph-scratch/moonray/test/region_test.cpp`); depth of field (`apertureRadius`, `focusDistance`, KHR_CAMERA_DEPTH_OF_FIELD) maps to `dof`/`dof_aperture` (the f-stop that gives the requested lens radius) and `dof_focus_distance` - verified with pynari `camera-depth-of-field.py` |
| Camera | `orthographic` | PARTIAL | `OrthographicCamera` (`film_width_aperture = height · aspect`) | — | `anari.orthographic_camera` | — | correct for the camera a scene is first rendered with; **moving it afterwards is wrong** (see the limitation below) |
| Geometry | `triangle` | PARTIAL | `RdlMeshGeometry`, `is_subd = false` | — | `anari.triangle_matte`, `anari.image_texture`, `anari.vertex_normals`, `anari.vertex_colors`, `unit.dirty_propagation` | `pynari.triangle`, `pynari.texture` | `vertex.position`, `primitive.index`, `vertex.normal`, `vertex.color`, `vertex.attribute0` (float/float2/float3/float4) → `uv_list`; `vertex.attribute0`..`vertex.attribute3` are also exported as the vec3f primitive attributes `anari_attribute0`..`anari_attribute3` (and `vertex.color` as the rgb `Cd`) for shading networks that name them (`rdla`); `vertex.tangent` (float3/float4) and the alpha of a four-component `vertex.color` are exported as the primitive attributes `anari_tangent` (vec3f), `anari_tangent_sign` (float) and `anari_color_alpha` (float) for shading networks that name them (MoonRay's own materials take their tangents from the texture coordinates); primitive-rate and face-varying data warn and are ignored, and an optional attribute this device cannot use is dropped rather than invalidating the geometry (`anari.unusable_attributes`); without `vertex.normal` the geometric normal is used (`smooth_normal = false`), as ANARI specifies |
| Geometry | `sphere` | PARTIAL | `RdlPointGeometry` (MoonRay's points *are* spheres) | — | `anari.sphere_geometry` | `pynari.spheres` | `vertex.position`, `vertex.radius`, `radius`, `primitive.index`, `vertex.color`, `primitive.color`, `vertex.attribute0` (for image1D); the other attributes warn |
| Geometry | `curve` | PARTIAL | `RdlCurveGeometry` with `curve_type` linear | — | `anari.curve_geometry` | — | `vertex.position`, `vertex.radius`, `radius`, `primitive.index` (segments merged back into polylines), `vertex.color`, `vertex.attribute0` (float/float2/float3/float4) → `uv_list` (the texture coordinates a hair shader looks the color of the scalp up with; without it MoonRay's own st of a curve stays, which runs along the curve and across it); `vertex.attribute0`..`vertex.attribute3` are also exported as the vec3f primitive attributes `anari_attribute0`..`anari_attribute3`, as on the triangle geometry; every curve geometry also carries what a hair shader asks its strand for, as primitive attributes for shading networks that name them (`rdla`): `anari_curve_intercept` (float per vertex: 0 at the first vertex of a curve, 1 at its last, by length along the curve), `anari_curve_length` (float per curve), `anari_curve_random` (float per curve, a random number in [0, 1)) and `anari_is_curve` (float, 1; missing on every other geometry, where an `AttributeMap` returns its default); rendered as round tubes (`curves_subtype = round`, not MoonRay's default ray-facing ribbons); the ends are rounded caps, so a segment renders one radius longer at each end |
| Geometry | `cylinder`, `cone` | PARTIAL | tessellated into an `RdlMeshGeometry` (64 segments, fewer for very many primitives) with analytic side normals and flat cap disks | — | — | — | `vertex.position`, `primitive.index` (uint2/uint64 pairs, default consecutive pairs), `primitive.radius`/`radius` (cylinder), `vertex.radius` (cone), `caps`, `vertex.cap`, `vertex.color`/`primitive.color`/`color`, `vertex.attribute0`/`primitive.attribute0`; verified with the pynari `geometry-cylinders*`/`geometry-cones*` samples, no repo test yet |
| Geometry | `quad` | PLANNED | — | — | — | — | not advertised |
| Material | `matte` | SUPPORTED | `UsdPreviewSurface` in its Lambertian configuration (`metallic 0`, `roughness 1`, `ior 1`) | — | `anari.triangle_matte`, `anari.image_texture`, `unit.commit_snapshot` | `pynari.triangle` | constant, sampler-valued, or `color`-attribute-valued `color`; other attribute names warn; `opacity` takes a constant or a sampler - from a sampler it is a cutout (at `alphaCutoff` in alphaMode `mask`, at 0.5 in `blend`, with a warning) |
| Material | `physicallyBased` | PARTIAL | `UsdPreviewSurface` (baseColor, metallic, roughness, ior, emissive, opacity, clearcoat) | — | `anari.physically_based` | `pynari.pbr` | sampler-valued `baseColor`, `emissive`, `metallic`, `roughness`, `clearcoat`, `clearcoatRoughness`, `opacity` and `normal` are bindings of the sampler's map (a scalar parameter reads the sampler's first component; `normal` is a normal map: the sampler returns the encoded color and the device decodes it, n = 2c - 1, after the sampler's own output transform; the tangent frame is x along dP/ds, green = +v - `vertex.tangent` is not used); a sampler-valued `opacity` is a cutout (at `alphaCutoff` in alphaMode `mask`, at 0.5 in `blend`, with a warning: UsdPreviewSurface has no partial presence); `ior` and `transmission` are constants only; verified with `build/graph-scratch/moonray/test/sampler_test.cpp`; `baseColor` also takes the `"color"` geometry attribute; `transmission` becomes the dielectric lobe's refraction weight (UsdPreviewSurface `opacity` = 1 - transmission with `opacityThreshold` 0; untinted); `occlusion`, `specular*`, `thickness`, `attenuation*`, `sheen*`, `iridescence*` have no UsdPreviewSurface counterpart and warn individually |
| Material | `rdla` | SUPPORTED | the `source` text (RDL2 ASCII) is loaded into the device's `SceneContext`; the `Material` named by `materialName` is assigned | — | — | — | vendor subtype for exporters that write MoonRay shading networks; every image2D sampler parameter is an image a string attribute of the fragment can name; a fragment that cannot be loaded is reported and renders magenta; reloaded only when the text or an image changes (`moonray.fragmentLoads`); needs the SceneClasses it names in the runtime (`Dwa*` and the pattern maps are moonshine); verified with `build/graph-scratch/moonray/test/rdla_test.cpp`, no repo test yet |
| Material | `emissionSampling` on `physicallyBased` and `rdla` | SUPPORTED | one `MeshLight` per emitting surface, on a mesh which is in no layer and holds the sampled sides of all its instances; the surface is shaded through a `RaySwitchMaterial` which only emits for camera and mirror rays | — | `anari.mesh_lights_noise`, `_sides`, `_reflecting`, `_textured`, `_rdla`, `_spaces`, `_updates`, `_limit`, `_visibility`, `_instances`, `_churn` | — | vendor parameter, see *Emitting surfaces sampled by lights* below: MoonRay does not sample emitting geometry, so a lamp is otherwise only found by a path which happens to hit it; `matte` has no emission |
| Light | `falloff.distance`, `falloff.color` on `point`, `spot` and `quad` | SUPPORTED | a radial `ColorRampLightFilter` in the `light_filters` of the light | — | — | — | vendor parameters: what the emission is multiplied with over the distance between the point of the light which emits and the point which is lit, linear between the distances, constant outside of them. For lights whose shader fades them out or changes their falloff |
| Sampler | `image2D` | PARTIAL | `ImageMap` bound to the material attribute, fed from a tiled+mipmapped texture served to OpenImageIO from memory | — | `anari.image_texture` | `pynari.texture` | float/UFIXED8 (incl. sRGB) images; `inAttribute` `attribute0` (the mesh's st, filtered) or `attribute1`..`attribute3` (triangle geometry only, through an `AttributeMap`, which MoonRay samples without MIP level selection, i.e. unfiltered when minified); `wrapMode` mapped to the single `wrap_around`; `filter = nearest` is emulated by writing every texel as a block of pixels (OpenImageIO has no nearest mode ImageMap can select); `inTransform` maps to ImageMap's scale/offset/rotation; `outTransform`/`outOffset` are applied to the linear value: a per-channel scale and offset, any combination of r, g, b for a scalar parameter, and the alpha channel alone go through ImageMap's own gain/offset on the shared texture, a matrix that mixes channels is baked into a copy of the texture that samplers with the same image and matrix share |
| Sampler | `image1D` | PARTIAL | `ImageMap` on an N×1 in-memory texture, sampling its `input_texture_coordinates`, which an `AttributeMap` reads from the geometry's exported `vertex.attribute0` | — | — | — | `inAttribute = attribute0` on triangle, sphere, cylinder and cone geometry, `attribute1`..`attribute3` on triangle geometry; `outTransform`/`outOffset` as for image2D; `filter` (nearest emulated as for image2D), `wrapMode` clamp/repeat, the 1D part of `inTransform`/`inOffset`; verified with pynari `geometry-spheres-with-sampler1D.py`, no repo test yet |
| Sampler | `transform` | PARTIAL | `AttributeMap` reading `Cd` or `anari_attributeN`, bound to the material attribute, whose own value carries the scale | — | — | — | for color parameters, `inAttribute` `color` or `attribute0`..`attribute3`, and an `outTransform` that scales each channel; an `outOffset`, mixed channels and scalar parameters are refused with a warning (MoonRay has no arithmetic on a geometry attribute without further maps) |
| Sampler | `image3D`, `primitive` | PLANNED | — | — | — | — | not advertised; creating one warns |
| Light | `directional` | SUPPORTED | `DistantLight`, `intensity = irradiance / π` with `normalized` | — | `anari.triangle_matte`, `anari.light_update`, `unit.dirty_propagation` | `pynari.triangle` | `radiance` warns and falls back to `irradiance` |
| Light | `point` | SUPPORTED | `SphereLight`, `intensity = I/(π r²)`, `normalized = false` | — | `anari.point_light` | — | MoonRay has no delta light: `radius = 0` becomes `1e-3` |
| Light | `quad` | SUPPORTED | `RectLight` (center/width/height/basis derived from position + edges), `sidedness` from `side` | — | `anari.quad_light` | — | `intensityDistribution` unsupported |
| Light | `hdri` | SUPPORTED | `EnvLight` + a lat-long in-memory texture; the node basis is derived from `up`/`direction` through MoonRay's fixed `sLocalOrientation` | — | `anari.hdri_light` | `pynari.hdri` | `visible` → `visible_in_camera`; a visible hdri is the backdrop (no background composited under it); the map is filtered bilinearly; while an hdri light is in the world the renderer's ambient light is not added (as in barney) |
| Surface / Group / Instance / World | — | SUPPORTED | RDL2 `GeometrySet` + `Layer` + `LightSet`; a singly-used group's transform goes on `node_xform`, a shared group becomes a prototype + `RdlInstancerGeometry` | — | `anari.instance_updates`, `anari.shared_group` | `pynari.instancing` | membership changes rebuild the `GeometrySet`/`Layer`/`LightSet`, so an object removed from the world stops rendering; a pure transform change does not (asserted through `moonray.layerRebuiltLastSync`); vendor parameters on the instance, all BOOL and default true: `visibility.camera`, `visibility.diffuse`, `visibility.glossy`, `visibility.transmission`, `visibility.volumeScatter`, `visibility.shadow` → the RDL2 geometry's `visible_*` attributes (camera; diffuse reflection; glossy + mirror reflection; diffuse + glossy + mirror transmission; volume; shadow). Instances of a shared group that differ in visibility get one `RdlInstancerGeometry` per distinct combination. Whether a *light* is reached is always a shadow-ray question in MoonRay, so an object hidden from glossy/diffuse/transmission rays still blocks lights along them while `visibility.shadow` is on; verified with `build/graph-scratch/moonray/test/visibility_test.cpp`, no repo test yet |
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
| `moonray.meshLights` | `ANARI_INT32` | `MeshLight` objects of the last sync, for the materials with `emissionSampling` |
| `moonray.meshLightsOverLimit` | `ANARI_INT32` | how many more lights it would have taken to sample every emitter that asked (`moonray.maxMeshLights`, or more triangles than the lights may have); their surfaces emit as before |
| `moonray.executionMode` | `ANARI_STRING` | which of MoonRay's execution modes actually ran (`scalar`, `vectorized`, `xpu`) — not always the one the device parameter of the same name asked for, since XPU falls back without a usable GPU |

## Vendor parameters

| Device parameter | Type | Meaning |
|---|---|---|
| `moonray.executionMode` | `ANARI_STRING` | `auto` (default), `scalar`, `vectorized` or `xpu`. MoonRay has one renderer per process and this lives on its global options, so the first device to render decides for the process (docs/adr/0005) |

| `moonray.maxMeshLights` | `ANARI_INT32` | how many `MeshLight` objects are created at most for materials with `emissionSampling`. Default 64. MoonRay samples every light at every hit, so each light costs a shadow ray per sample; the emitters with the most power (area times radiance) get the lights, all instances of a surface or none. The renderer parameter of the same name goes first |

| Material parameter | Type | Meaning |
|---|---|---|
| `emissionSampling` | `ANARI_STRING` | `none` (default), `front`, `back` or `both`: sample the emission of the material with lights on these sides of the surfaces which have it. On `physicallyBased` (the light takes `emissive`, constant or from a sampler) and on `rdla` |
| `emissionColor` | `ANARI_FLOAT32_VEC3` | `rdla` only: the radiance the lights emit; the device does not read it out of the fragment |
| `emissionMap` | `ANARI_STRING` | `rdla` only, optional: name of a `Map` of the fragment the radiance is multiplied with (the `map_shader` of the lights) |
| `sampledMaterialName` | `ANARI_STRING` | `rdla` only: name of the root `Material` of the fragment the surface is shaded with while lights sample its emission. It has to emit for camera and mirror rays only (a `RaySwitchMaterial`); without it the emission is not sampled |

| Light parameter | Type | Meaning |
|---|---|---|
| `falloff.distance` | `ANARI_ARRAY1D` of `ANARI_FLOAT32` | on `point`, `spot` and `quad`: distances from the point of the light which emits, increasing, two or more |
| `falloff.color` | `ANARI_ARRAY1D` of `ANARI_FLOAT32_VEC3` | what the emission is multiplied with at these distances; linear in between, constant before the first and after the last. Both become one `ColorRampLightFilter` (radial, without a transform of its own, so its distance is the one to the sampled point of the light) |

| Instance parameter | Type | Meaning |
|---|---|---|
| `visibility.camera`, `.diffuse`, `.glossy`, `.transmission`, `.volumeScatter`, `.shadow` | `ANARI_BOOL` | which rays see the instance (Cycles' object ray visibility). Default true |
| `objectRandom` | `ANARI_FLOAT32` | a random number of the object. Exported as the per-instance primitive attribute `anari_object_random` (float) for shading networks that name it (`rdla`, e.g. an `AttributeMap`); the instance's `color` and `id` are exported the same way as `anari_object_color` (rgb) and `anari_object_index` (float) |

| Renderer parameter | Type | Meaning |
|---|---|---|
| `moonray.lightSamples` | `ANARI_INT32` | MoonRay's `light_samples`: N×N light samples per light at every hit, and N×N `volume_illumination_samples` per volume scattering event. Default 1; MoonRay's own default, 2, makes every pixel sample about twice as expensive |
| `moonray.bsdfSamples` | `ANARI_INT32` | MoonRay's `bsdf_samples`: N×N BSDF samples per lobe at every hit. Default 1 (MoonRay's own: 2) |
| `moonray.maxMeshLights` | `ANARI_INT32` | the bound on the lights for emitting surfaces, see the device parameter of the same name, which counts when the renderer does not set this |
| `moonray.maxMeshLightTriangles` | `ANARI_INT32` | how many triangles the meshes of those lights may have together, both sides and all instances counted. Default 500 000. MoonRay builds the sampling structure of a mesh light again for every edit of it, in about a second per 250 000 triangles; an emitter which does not fit emits as a surface |
| `maxBounce` | `ANARI_INT32` | `max_depth`: bounces of any kind after the camera hit. Default 5 |
| `maxDiffuseBounce` | `ANARI_INT32` | `max_diffuse_depth`. Default 2. The counts are the same as Cycles': n allows n bounces after the camera hit (measured in an integrating sphere, `build/graph-scratch/moonray/test/bounce_test.cpp`) |
| `maxGlossyBounce` | `ANARI_INT32` | `max_glossy_depth`. Default 2. MoonRay limits a lobe type for reflection and transmission together, so this also limits glossy transmission |
| `maxTransmissionBounce` | `ANARI_INT32` | Default 3. `max_mirror_depth` is the larger of this and `maxGlossyBounce`: MoonRay's mirror lobes are both the perfect mirror and the refraction through clear glass |
| `maxVolumeBounce` | `ANARI_INT32` | `max_volume_depth` - 1 (MoonRay counts scattering orders). Default 0 |
| `maxTransparencyBounce` | `ANARI_INT32` | `max_presence_depth`: cutout surfaces a path passes through. Default 16 |
| `clampIndirect` | `ANARI_FLOAT32` | `sample_clamping_value`, applied from the first bounce on (`sample_clamping_depth` 1): the largest component of every direct lighting sample's contribution is limited to it. 0 for none. Default 10 |
| `clampDirect` | `ANARI_FLOAT32` | MoonRay has one clamping value: a `clampDirect` that is not above `clampIndirect` extends the clamping to the camera hit (`sample_clamping_depth` 0, with the indirect value); a looser one, or one without `clampIndirect`, has no effect. Default 0 |

## Emitting surfaces sampled by lights

MoonRay has no light sampling for geometry which emits: a surface with an
emission lights the scene only through paths which happen to hit it. A small
bright lamp in a closed room is then noise, and at a limit of n diffuse bounces
the scene gets one bounce less of its light than from a light. A material with
`emissionSampling` set to `front`, `back` or `both` is sampled by lights as
well:

- **One `MeshLight` per surface.** MoonRay refuses a geometry which is in the
  scene's layer as the shape of a light, and a `MeshLight` emits from the front
  of its faces only. The light therefore sits on a mesh of its own: the
  triangles of every instance of the surface, once for each sampled side, the
  back side with its winding turned around. One instance keeps its transform on
  the mesh, several are written in world space. One light for all of them
  because a light is not cheap in MoonRay: every hit samples every light and
  every sampled direction is intersected with every mesh light. A thousand
  instances of a lamp are one light (`anari.mesh_lights_instances`).
- **A light per instance** only when the map of the emission reads what the
  instances do not share: the space of the object or of the instance (a
  `TransformSpaceMap`, or a `space` of a map, set to one of those), or an
  `anari_object_...` attribute. The device finds that by walking the network
  of the map (`anari.mesh_lights_spaces`). Instances which other rays see
  (below) are separate lights as well.
- **No light** for an emission whose map reads the direction of view (the
  `observer direction` of an `AttributeMap`: a Fresnel or facing term) or the
  side which is hit (`TwoSidedMap`), found by the same walk: a light has
  neither, and the map would give it something else than the surface shows.
  Such a surface emits for all rays, as without `emissionSampling`, and the
  device says so in an info message (`anari.mesh_lights_spaces`).
- **The copies are moved off the surface** by a hair (1e-4 of the size of the
  mesh), each to the side it emits to: on the surface itself a ray which leaves
  to one side would find the copy of the other side at distance zero half of
  the time, and the surface would reflect its own emission
  (`anari.mesh_lights_reflecting`).
- **The radiance** is `emissive` for `physicallyBased`, with the map of its
  sampler as the `map_shader` of the light, and `emissionColor` times
  `emissionMap` for `rdla`. A map which reads the position needed two fixes in
  the MoonRay fork (`MeshLight.cc`, `MeshLight.ispc`): `eval()` evaluated the
  map at the point which is lit instead of the point of the light which emits,
  and the energy of the faces, which decides where the light is sampled, was
  taken from the map at positions in the local space of the light instead of
  the render space. With the second one a map which is zero there left every
  face without energy, and the light sampled its faces uniformly, those which
  face away included.
- **The surface** keeps its emission for camera rays and for mirror rays, which
  the lights are made invisible to: what the camera sees, directly, in a
  mirror or through clear glass, is the surface with its texture. For diffuse
  and glossy rays it is shaded without the emission, which the lights deliver
  with multiple importance sampling. For `physicallyBased` that is a copy of
  the material without the emission behind a `RaySwitchMaterial`; an `rdla`
  material brings the variant along (`sampledMaterialName`).
- **Ray visibility.** The light takes the `visibility.diffuse`, `.glossy`,
  `.transmission` and `.volumeScatter` of its instances, which is why
  instances with other flags cannot share a light; an instance none of these
  rays see is in no light.
- **The bound.** At most `moonray.maxMeshLights` lights (64) and
  `moonray.maxMeshLightTriangles` triangles (500 000) in their meshes together,
  both sides and all instances counted, given to the emitters with the most
  power. All instances of a surface or none: they share one material. What
  does not fit emits as a surface, as before (`moonray.meshLightsOverLimit`).
- **Updates.** The mesh of a light is named after the surface, not after its
  instances, and it is written again when the geometry, an instance or the set
  of instances changes; an application which makes all its instances anew for
  every edit gets the same RDL2 objects again. An emitter which leaves the
  scene gives its mesh back (reset; like every RDL2 object it cannot be
  deleted, and its assignment in MoonRay's mesh light layer stays). MoonRay
  builds that layer itself from the lights of the first frame only, so the
  device keeps it; the meshes are also in a geometry set of their own, without
  which MoonRay does not tessellate them and a light with a map crashes.
- **Materials behind a `RaySwitchMaterial`.** MoonRay creates the shading state
  of a material which is only referred to by another material for the first
  frame, but not for one which appears later: the first ray switched to it
  crashed the render. The device assigns every material of an `rdla` fragment
  and both variants of a sampled `physicallyBased` material to a part of an
  empty geometry in the mesh light layer, which makes them materials of a
  layer. This also covers an `rdla` material with a `RaySwitchMaterial` which
  is created or edited after the first frame.

Measured in a closed room (4 x 4 x 3 m, albedo 0.7, a lamp of 1 m and of
0.2 m with the same power, 16 samples, relative RMS against 400 samples): 0.108
with the light against 0.61 as a surface for the large lamp, 0.114 against 3.25
for the small one, with means within 3 % (`tests/integration/test_mesh_lights.cpp`).

What it costs and what is not covered:

- Every light is a shadow ray per light sample at every hit. A scene with a
  few emitters which matter little next to its sun renders slower for a small
  gain; `moonray.maxMeshLights` limits it, 0 switches it off.
- A light gives the scene one more bounce of the emission than the surface did
  at the same diffuse depth, and the clamp of indirect samples now applies to
  it: the same scene is a few percent brighter at depth 8 and a quarter at
  depth 2.
- Geometry other than triangles is not sampled. The emission of a side which
  is not sampled reaches the scene only through camera and mirror rays.
- An edit of a light, of its material or of its object makes MoonRay build
  the sampling structure of the mesh again: about a second per 250 000
  triangles (measured: a frame after an edit of a lamp of 131 072 triangles,
  both sides sampled, takes 1.1 s instead of 75 ms; for a lamp of two
  triangles nothing changes). Hence `moonray.maxMeshLightTriangles`.
- The sampling of a light with a map follows the map by face only (MoonRay
  takes the largest of five values of the map per face): a large face with a
  small bright spot is sampled as if it were bright all over.

## Shaders of this repository

`dso/` holds RDL2 shaders that MoonRay and moonshine do not have. They are built
against the installed MoonRay like moonshine is
(`scripts/build-moonray-windows.ps1 -Target anari_dso`), land in its `rdl2dso`
directory and are used like any other class, for example in the fragment of an
`rdla` material. They depend on nothing but MoonRay.

| Class | Type | What it does |
|---|---|---|
| `AnariBumpNormalMap` | `NormalMap` | Bump mapping: turns the height bound to `input_height` into a normal. The height is evaluated at the shading point and at two points `filter_width` (default 0.1) pixels away, and the normal - the shading normal, or the one of the normal map in `input_normal` - is tilted against the slope between them (Mikkelsen's surface gradient). `distance` is the length a height of 1 stands for, in object space (`object_space`, the default) or world space; `strength` blends between the given and the bumped normal; `invert` turns bumps into dents. The height is along the normal that faces the viewer, on both sides of a surface. For the two extra evaluations the map gets a copy of the shading state in which the position, the texture coordinates and the primitive attributes `anari_attribute0`..`3` are moved to the other point; what else a height reads (normals, the viewing direction, other attributes) is the same at all three points and gives no bump. Verified against the analytic normal of a sine height field in both execution modes, on scaled and instanced geometry (`build/graph-scratch/moonray/probe/bump_check.py`) |

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
- **A texture is a second copy of the image, in memory.** MoonRay samples
  through OpenImageIO's texture system and only ever takes a texture *name*,
  so the device registers the pixels of an ANARI array with OpenImageIO under
  a name of their own and serves them through a custom `ImageInput`
  (`texture/MemoryTexture.h`): tiled, with a MIP pyramid, as float (half
  float for 8-bit images). Nothing is written to disk. An image used by
  several samplers and materials is held once; updating it registers a new
  name, because MoonRay's texture cache is keyed on the name.
  `OPENMOONRAY_ANARI_TEXTURE_FILES=1` goes back to scratch `.exr` files
  written with OpenImageIO's `make_texture()` (a per-process directory under
  the system temp directory). The volume of a `transferFunction1D` is still
  written to a scratch `.vdb` file.
- **One frame renders at a time, process-wide.** MoonRay has a single global
  render driver, so `anariRenderFrame` calls queue behind each other
  (docs/adr/0005).
- **Debug configuration on Windows is not available for the device**: the
  MoonRay install is Release-only, and mixing CRTs would crash. The build
  refuses it explicitly rather than producing a broken library.
