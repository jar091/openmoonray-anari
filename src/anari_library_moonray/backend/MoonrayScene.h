// Copyright 2026 openmoonray-anari contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "MoonrayBackend.h"
#include "MoonrayMath.h"
// helium
#include "helium/BaseObject.h"
// std
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace moonray {
namespace rndr {
class RenderContext;
} // namespace rndr
} // namespace moonray

namespace scene_rdl2 {
namespace rdl2 {
class Geometry;
class Material;
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

  // Writes `geometry` into `obj`, including the `UserData` objects its
  // per-vertex attributes need - those are scene objects of their own, so the
  // geometry describes them and this builds them (see
  // Geometry::PrimitiveAttribute). `name` is the geometry's own RDL2 name,
  // which the UserData objects are named after.
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
  uint2 m_lastSize{0u, 0u};

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

  std::atomic<bool> m_rendering{false};
};

} // namespace moonray_anari
