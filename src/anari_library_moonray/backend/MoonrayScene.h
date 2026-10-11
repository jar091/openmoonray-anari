// Copyright 2026 openmoonray-anari contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "MoonrayBackend.h"
#include "MoonrayMath.h"
#include "material/Material.h"
#include "renderer/Renderer.h"
// helium
#include "helium/BaseObject.h"
// std
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#if defined(__linux__) || defined(__APPLE__)
// scene_rdl2 specializes std::atomic<float> where the standard library has no C++20
// floating point atomics (libstdc++ in C++17 mode, libc++ of Apple clang). The
// specialization has to be declared before std::atomic<float> is used below: GCC and
// clang reject it otherwise in every file which includes this header before a MoonRay
// header ("specialization of 'std::atomic<float>' after instantiation").
#include <scene_rdl2/render/util/AtomicFloat.h>
#endif

namespace moonray {
namespace rndr {
class RenderContext;
} // namespace rndr
} // namespace moonray

namespace scene_rdl2 {
namespace rdl2 {
class Geometry;
class Light;
class Material;
class SceneContext;
class SceneObject;
class VolumeShader;
} // namespace rdl2
} // namespace scene_rdl2

namespace moonray_anari {

struct Camera;
struct Geometry;
struct Group;
struct Instance;
struct MoonrayDeviceGlobalState;
struct Renderer;
struct Surface;
struct World;

// The device's MoonRay scene: one RDL2 SceneContext (owned by one
// RenderContext) that the ANARI scene graph is translated into, plus the
// render call itself.
//
// Everything here runs on the MoonRay backend thread - the class never touches
// MoonRay outside a MoonrayBackend::execute() call (docs/adr/0005).
struct MoonrayScene
{
  MoonrayScene(MoonrayDeviceGlobalState *state);
  ~MoonrayScene();

  // False when the MoonRay driver could not be initialized; error() says why.
  bool valid() const;
  const std::string &error() const;

  // Where MoonRay looks for its RDL2 shader DSOs (empty if uninitialized).
  const std::string &dsoPath() const;

  // MoonRay's execution mode: "auto", "scalar", "vectorized" or "xpu". Has to
  // be set before the first render, because the RenderContext reads it when it
  // initializes, and it lives on the *process-global* RenderOptions - MoonRay
  // has one renderer per process (docs/adr/0005), so the first device to
  // render decides for all of them. Reported back through the device property
  // `moonray.executionMode`, which is what MoonRay actually chose: asking for
  // xpu on a machine without a usable GPU falls back to vectorized.
  void setExecutionMode(const std::string &mode);
  const std::string &executionMode() const;

  // Translate `world`/`camera`/`renderer` into RDL2 and render one frame of
  // `size` pixels. Blocking. `color` receives width*height RGBA pixels with
  // the ANARI convention (first pixel = lower left), straight from MoonRay's
  // beauty buffer - no background compositing, which is the Frame's job.
  //
  // Returns false and fills `error` if the render failed.
  // `depth`, when non-null, asks for the depth channel as well: it receives
  // width*height values, MoonRay's camera-space z distance (see
  // docs/adr/0007 for what that means and what the Frame does with it).
  //
  // `accumulate` says the caller averages consecutive renders of an unchanged
  // scene (KHR_FRAME_ACCUMULATION): such a render then draws new samples
  // instead of repeating the previous ones. `sceneChanged` reports whether
  // anything the image depends on was re-translated since the last render,
  // i.e. whether an accumulation has to start over.
  bool render(World *world,
      Camera *camera,
      Renderer *renderer,
      uint2 size,
      std::vector<float4> &color,
      std::vector<float> *depth,
      bool accumulate,
      bool &sceneChanged,
      std::string &error);

  // Ask an in-flight render to stop early (anariDiscardFrame). Safe to call
  // from any thread.
  void requestStop();

  // What the last scene sync actually had to do. Exposed through the device
  // properties `moonray.objectsUpdatedLastSync`,
  // `moonray.objectsUnchangedLastSync` and `moonray.layerRebuiltLastSync`, so
  // "an unchanged scene is not re-translated" is an assertion an application
  // (and the update tests) can make, not a claim in a comment.
  int objectsUpdatedLastSync() const;
  int objectsUnchangedLastSync() const;
  bool layerRebuiltLastSync() const;
  // How long the last translation took, in milliseconds - the sync alone,
  // without the render it precedes.
  float syncMillisecondsLastFrame() const;

  // `rdla` materials (Material::Fragment), exposed through the device
  // properties `moonray.fragmentLoads`, `moonray.fragmentObjects` and
  // `moonray.fragmentObjectsRetired`: how many times a fragment's text has
  // been loaded since the device was created, how many RDL2 objects the
  // loaded fragments consist of right now, and how many objects of earlier
  // fragments are sitting retired (reset to their defaults).
  int fragmentLoads() const;
  int fragmentObjects() const;
  int fragmentObjectsRetired() const;

  // Emitting surfaces sampled by lights (Material::Emitter), exposed through
  // the device properties `moonray.meshLights` and
  // `moonray.meshLightsOverLimit`: the MeshLight objects of the last sync, and
  // how many more it would have taken to sample every emitter that asked.
  int meshLights() const;
  int meshLightsOverLimit() const;
  // The bound on the number of mesh lights for a renderer which sets none
  // (the device parameter `moonray.maxMeshLights`).
  void setMaxMeshLights(int count);

 private:
  // An RDL2 object plus the ANARI-side version its attributes were written
  // from. The version is the maximum lastUpdated() of every ANARI object that
  // feeds it (a geometry's transform comes from its instance, for example),
  // so an unchanged input means the attributes are already correct.
  struct SyncedObject
  {
    scene_rdl2::rdl2::SceneObject *object{nullptr};
    helium::TimeStamp version{0};
    bool written{false};
  };

  // One layer assignment: a geometry plus what shades it - a material for a
  // surface, a volume shader for a volume (rdl2::Layer::assign).
  //
  // `retired` marks a geometry that has left the scene. Its slot stays in the
  // layer, because MoonRay keeps the primitives it has already built and their
  // baked assignment IDs would otherwise point past the end of a shrunken
  // layer; it is made invisible to every ray type instead of being dropped.
  struct LayerAssignment
  {
    scene_rdl2::rdl2::Geometry *geometry{nullptr};
    scene_rdl2::rdl2::Material *material{nullptr};
    scene_rdl2::rdl2::VolumeShader *volumeShader{nullptr};
    bool retired{false};
    // The geometry's RDL2 name, so retiring it can also release the objects
    // named under it.
    std::string name;
    // The rays that see the geometry while it is in the scene (a mask of
    // `visibility` bits, world/Instance.h), from the ANARI instance it
    // belongs to.
    uint32_t visibility{0x1ffu};
    // What the RDL2 object's visibility attributes hold right now; they are
    // only written when this differs from what is wanted. An RDL2 geometry
    // starts out visible to everything.
    uint32_t appliedVisibility{0x1ffu};
  };

  void ensureContext();
  void syncScene(World *world,
      Camera *camera,
      Renderer *renderer,
      uint2 size,
      bool wantsDepth,
      bool accumulate);
  void syncSurfaces(World *world,
      scene_rdl2::rdl2::SceneObject *lightSet,
      scene_rdl2::rdl2::SceneObject *geometrySet,
      scene_rdl2::rdl2::SceneObject *layer);

  scene_rdl2::rdl2::SceneObject *createObject(
      const char *className, const std::string &name);

  // Reset `name` and everything named under it to RDL2's defaults and forget
  // them, so the memory goes back and a later revival writes them afresh.
  void retireObject(const std::string &name);

  // --- `rdla` materials ---------------------------------------------------
  // What the device knows about one ANARI material whose RDL2 objects come
  // from a text fragment (Material::Fragment) rather than from syncRdl2().
  struct FragmentState
  {
    // Material::Fragment::version the objects were loaded from; 0 = never.
    uint64_t version{0};
    // The images the objects were loaded with, which is also what keeps
    // their textures alive for as long as MoonRay may sample them.
    std::vector<Material::FragmentImage> images;
    // The fragment's root, or null when it could not be loaded and the
    // material renders with the error material.
    scene_rdl2::rdl2::Material *root{nullptr};
    // Names of the RDL2 objects the loaded text consists of.
    std::vector<std::string> objects;
    // Whether a surface of the scene being synced uses the material.
    bool used{true};
    // Whether one of the objects sets a presence (a cutout).
    bool usesPresence{false};
    // m_fragmentsRetired when the fragment was loaded. A fragment that failed
    // is tried again once another one has left the scene since: the names
    // that one held may have been what was in the way.
    uint64_t retiredAtLoad{0};
    // Whether the application was told that the fragment does not have what
    // its emitter parameters name; once per load.
    bool emitterReported{false};
    // emissionReads() of this load for the map of that name.
    bool readsChecked{false};
    std::string readsCheckedMap;
    unsigned reads{0};
  };

  // Returns the RDL2 material for an `rdla` ANARI material, (re)loading its
  // fragment when its text or one of its images changed. Never null: a
  // fragment that cannot be loaded is reported through the material and
  // yields errorMaterial().
  scene_rdl2::rdl2::Material *syncFragment(
      const Material *material, const Material::Fragment &fragment);
  // Resets the fragment's objects to RDL2's defaults and marks them retired.
  void retireFragmentObjects(FragmentState &state);
  // Retires the fragments whose `used` flag is not set.
  void retireUnusedFragments();
  // The objects of the loaded fragment which the emitter parameters of an
  // `rdla` material name: the root that emits for camera and mirror rays
  // only, and the map of the emission (null without one). False, with a
  // warning, when the fragment does not have them.
  bool fragmentEmitter(const Material *material,
      const Material::Emitter &emitter,
      scene_rdl2::rdl2::Material *&sampledRoot,
      scene_rdl2::rdl2::SceneObject *&emissionMap);
  // What the map of the emission of a loaded `rdla` material reads that a
  // light on a mesh for all instances does not have. ReadsInstance:
  // something every instance has of its own - the space of the object, or
  // the color, the random number or the index of the instance; the light of
  // such an emitter is one per instance. ReadsView: the direction of view or
  // the side which is hit, which no light has; such an emitter is not
  // sampled by a light.
  enum : unsigned { ReadsInstance = 1u, ReadsView = 2u };
  unsigned emissionReads(
      const Material *material, const Material::Emitter &emitter);
  // What a surface is shaded with when its material cannot be: an
  // unmistakable magenta.
  scene_rdl2::rdl2::Material *errorMaterial();
  // A material which rays only reach through another one - what a
  // RaySwitchMaterial switches to - has to be known to MoonRay as a material
  // of a layer: MoonRay creates the shading state of the materials a layer
  // refers to, directly or through others, for the first frame, but after
  // that only of the ones a layer assigns, and a ray that is switched to a
  // material without that state crashes the render. Such a material is
  // therefore assigned to a part of an empty geometry in the mesh light
  // layer, which renders nothing. Once per material.
  void holdMaterial(scene_rdl2::rdl2::SceneObject *material);

  // Writes `geometry` into `obj`, including the `UserData` objects its
  // per-vertex attributes need - those are scene objects of their own, so the
  // geometry describes them and this builds them (see
  // Geometry::PrimitiveAttribute). `name` is the geometry's own RDL2 name,
  // which the UserData objects are named after.
  // Adds what a shading network may read of the instances - the color
  // (`anari_object_color`), the random number (`anari_object_random`) and
  // the id (`anari_object_index`) - to the primitive attributes of `obj`: of
  // a geometry with its one instance, or of an instancer with one value per
  // instance.
  void appendObjectInfo(const std::vector<Instance *> &instances,
      scene_rdl2::rdl2::SceneObject *obj,
      const std::string &name,
      bool instancer);
  void writeGeometry(const Geometry *geometry,
      scene_rdl2::rdl2::SceneObject *obj,
      const std::string &name);

  // Look up (creating on first use) the RDL2 object for `name` and report
  // whether it has to be written: `needsWrite` is true only when this
  // version has not been written yet.
  scene_rdl2::rdl2::SceneObject *syncedObject(const char *className,
      const std::string &name,
      helium::TimeStamp version,
      bool &needsWrite,
      bool force = false);

  MoonrayDeviceGlobalState *m_state{nullptr};
  std::shared_ptr<MoonrayBackend> m_backend;
  std::string m_error;

  // Owned by the backend thread; only read elsewhere through m_ctxMutex.
  std::unique_ptr<moonray::rndr::RenderContext> m_ctx;
  std::mutex m_ctxMutex;

  // Backend-thread state: the RDL2 objects this device owns, keyed by their
  // RDL2 name, and the membership of the containers built from them.
  std::string m_requestedExecutionMode{"auto"};
  std::string m_executionMode;

  std::unordered_map<std::string, SyncedObject> m_objects;
  // Every geometry ever assigned, in the order it was first assigned: a
  // geometry keeps its assignment ID for the life of the device.
  std::vector<LayerAssignment> m_layerAssignments;
  std::vector<const void *> m_containerSignature;

  // --- emitters sampled by lights (Material::Emitter) ---------------------
  // The light of an emitting surface: a mesh which is in no layer of the
  // scene, with the sampled sides of all instances of the surface which the
  // same rays see (of one instance, when the map of the emission reads what
  // instances have of their own), and the MeshLight on it. MoonRay loads such a geometry
  // through its own "MeshLightLayer", in which - as in the scene's layer - an
  // assignment once made stays; `live` says whether the last sync used it.
  // Keyed by the name of the mesh.
  struct EmitterLight
  {
    std::string lightName;
    bool live{false};
    // The instances and sides the mesh was built from (Object::syncId() and
    // the sides): an instance which left changes no timestamp of the others.
    std::vector<uint64_t> signature;
  };
  std::unordered_map<std::string, EmitterLight> m_emitterLights;
  // The names of the materials holdMaterial() has assigned.
  std::unordered_set<std::string> m_heldMaterials;
  // The MeshLight objects of the last syncSurfaces(), which syncScene() adds
  // to the light set.
  std::vector<scene_rdl2::rdl2::Light *> m_meshLights;
  int m_maxMeshLights{64};
  int m_deviceMaxMeshLights{64};
  size_t m_maxMeshLightTriangles{500000};
  int m_meshLightsOverLimit{0};
  uint2 m_lastSize{0u, 0u};

  // `rdla` materials by Object::syncId(), and who each object a fragment ever
  // created belongs to: the syncId of the material whose fragment it is part
  // of, or 0 once it is retired. RDL2 objects are never deleted, so a retired
  // name stays known - a later fragment may take it over, while a name that
  // belongs to another material or to the device's own objects is refused.
  std::unordered_map<uint64_t, FragmentState> m_fragments;
  std::unordered_map<std::string, uint64_t> m_fragmentObjectOwner;
  // The SceneContext fragments are loaded into first, to be checked
  // (inspectFragment() in the .cpp). Never rendered, empty between loads.
  std::unique_ptr<scene_rdl2::rdl2::SceneContext> m_fragmentScratch;
  scene_rdl2::rdl2::Material *m_errorMaterial{nullptr};

  // Whether a material of the scene being synced can be absent at a point
  // (Material::usesPresence(), or a fragment that sets a presence), and
  // whether MoonRay's shadow rays currently honour that
  // (`enable_presence_shadows`): on only while it is needed, because it makes
  // every shadow ray more expensive.
  bool m_needPresenceShadows{false};
  bool m_presenceShadows{false};
  // The renderer's path depths and clamping as last handed to MoonRay; a
  // change is a change of the image.
  Renderer::Integrator m_integrator;
  // How many fragments have left the scene so far.
  uint64_t m_fragmentsRetired{0};

  // Accumulating renders of an unchanged scene must not reproduce the
  // previous image: MoonRay seeds its sampling from the frame number, so
  // every such render advances it. A changed scene keeps the current number,
  // because a new frame number also makes MoonRay reload every geometry.
  bool m_hasRendered{false};
  bool m_sceneChanged{true};
  const void *m_lastCamera{nullptr};
  float m_frameNumber{0.f};

  std::atomic<float> m_syncMilliseconds{0.f};
  std::atomic<int> m_objectsUpdated{0};
  std::atomic<int> m_objectsUnchanged{0};
  std::atomic<bool> m_layerRebuilt{false};
  std::atomic<int> m_fragmentLoads{0};
  std::atomic<int> m_fragmentObjects{0};
  std::atomic<int> m_fragmentObjectsRetired{0};

  std::atomic<bool> m_rendering{false};
};

} // namespace moonray_anari
