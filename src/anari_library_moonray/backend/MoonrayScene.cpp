// Copyright 2026 openmoonray-anari contributors
// SPDX-License-Identifier: Apache-2.0

#include "MoonrayScene.h"

#include "camera/Camera.h"
#include "geometry/Geometry.h"
#include "light/HDRI.h"
#include "light/Light.h"
#include "material/Material.h"
#include "renderer/Renderer.h"
#include "sampler/Sampler.h"
#include "volume/TransferFunction1D.h"
#include "world/World.h"

// moonray
#include <moonray/rendering/rndr/RenderContext.h>
#include <moonray/rendering/rndr/RenderOptions.h>
#include <moonray/rendering/rndr/RenderOutputDriver.h>
// scene_rdl2
#include <scene_rdl2/common/fb_util/FbTypes.h>
#include <scene_rdl2/scene/rdl2/rdl2.h>
// std
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <thread>

namespace moonray_anari {

namespace rdl2 = scene_rdl2::rdl2;

namespace {

// RDL2 object names are the device's own namespace inside the SceneContext.
// They embed the SceneClass and the ANARI objects' sync IDs so that the same
// ANARI object maps to the same RDL2 object across frames, which is what makes
// re-rendering cheap.
//
// The ID rather than the address: addresses are recycled, and an RDL2 object
// carries every attribute anyone ever wrote into it. A new ANARI geometry
// allocated where a destroyed one used to be would inherit its RDL2 object -
// its uv_list, its normal_list, its layer assignment - and render as a mixture
// of the two (Object::syncId).
std::string objectName(const char *kind, const char *className,
    const Object *a, const Object *b = nullptr)
{
  char buf[160];
  if (b) {
    std::snprintf(buf,
        sizeof(buf),
        "/anari/%s/%s/%llu_%llu",
        kind,
        className,
        (unsigned long long)a->syncId(),
        (unsigned long long)b->syncId());
  } else {
    std::snprintf(buf,
        sizeof(buf),
        "/anari/%s/%s/%llu",
        kind,
        className,
        (unsigned long long)a->syncId());
  }
  return buf;
}

// ANARI mat4 is column-major and applied to column vectors; RDL2 Mat4 is
// applied to row vectors. The RDL2 rows are therefore the ANARI columns.
rdl2::Mat4d toRdl2(const mat4 &m)
{
  return rdl2::Mat4d(
      rdl2::Vec4d(m[0].x, m[0].y, m[0].z, m[0].w),
      rdl2::Vec4d(m[1].x, m[1].y, m[1].z, m[1].w),
      rdl2::Vec4d(m[2].x, m[2].y, m[2].z, m[2].w),
      rdl2::Vec4d(m[3].x, m[3].y, m[3].z, m[3].w));
}

// An attribute that refers to another scene object.
void setObject(
    rdl2::SceneObject *obj, const char *attribute, rdl2::SceneObject *value)
{
  obj->set(
      obj->getSceneClass().getAttributeKey<rdl2::SceneObject *>(attribute),
      value);
}

helium::TimeStamp newest(helium::TimeStamp a, helium::TimeStamp b)
{
  return a > b ? a : b;
}

// What a network of maps reads that the mesh of a light does not have. Bit 1
// (MoonrayScene::ReadsInstance): something that differs between the
// instances of one geometry - a space of the object or of the instance (a
// `space`, `from_space` or `to_space` set to one of those), or one of the
// primitive attributes the device gives every instance (`anari_object_...`).
// Bit 2 (ReadsView): the direction of view (the `observer direction` of an
// AttributeMap) or the side which is hit (TwoSidedMap), and the maps which
// shade by the view.
unsigned mapReads(rdl2::SceneObject *root)
{
  unsigned reads = 0;
  std::vector<rdl2::SceneObject *> open{root};
  std::set<rdl2::SceneObject *> seen;
  while (!open.empty()) {
    rdl2::SceneObject *obj = open.back();
    open.pop_back();
    if (!obj || !seen.insert(obj).second)
      continue;
    const rdl2::SceneClass &sceneClass = obj->getSceneClass();
    const std::string &className = sceneClass.getName();
    if (className == "TwoSidedMap" || className == "ToonMap"
        || className == "DirectionalMap") {
      reads |= 2u;
    }
    for (auto it = sceneClass.beginAttributes(); it != sceneClass.endAttributes();
        ++it) {
      const rdl2::Attribute *attribute = *it;
      const std::string &name = attribute->getName();
      switch (attribute->getType()) {
      case rdl2::TYPE_INT:
        if (attribute->isEnumerable()
            && name.find("space") != std::string::npos) {
          try {
            const std::string &space = attribute->getEnumDescription(
                obj->get(rdl2::AttributeKey<rdl2::Int>(*attribute)));
            if (space.find("object") != std::string::npos
                || space.find("instance") != std::string::npos) {
              reads |= 1u;
            }
          } catch (const std::exception &) {
            // not one of the values of the enumeration
          }
        } else if (attribute->isEnumerable() && name == "map_type") {
          try {
            if (attribute->getEnumDescription(
                    obj->get(rdl2::AttributeKey<rdl2::Int>(*attribute)))
                == "observer direction") {
              reads |= 2u;
            }
          } catch (const std::exception &) {
          }
        }
        break;
      case rdl2::TYPE_STRING:
        if (name == "primitive_attribute_name"
            && obj->get(rdl2::AttributeKey<rdl2::String>(*attribute))
                    .compare(0, 13, "anari_object_")
                == 0) {
          reads |= 1u;
        }
        break;
      case rdl2::TYPE_SCENE_OBJECT:
        open.push_back(
            obj->get(rdl2::AttributeKey<rdl2::SceneObject *>(*attribute)));
        break;
      case rdl2::TYPE_SCENE_OBJECT_VECTOR:
        for (rdl2::SceneObject *other : obj->get(
                 rdl2::AttributeKey<rdl2::SceneObjectVector>(*attribute))) {
          open.push_back(other);
        }
        break;
      default:
        break;
      }
      if (attribute->isBindable())
        open.push_back(obj->getBinding(*attribute));
    }
  }
  return reads;
}

// Back to what a newly created object is: every attribute at its default and
// nothing bound. resetAllToDefault() alone keeps the bindings, and a binding
// that survives is a map that keeps shading.
void resetObject(rdl2::SceneObject *obj)
{
  rdl2::SceneObject::UpdateGuard guard(obj);
  const rdl2::SceneClass &sceneClass = obj->getSceneClass();
  for (auto it = sceneClass.beginAttributes(); it != sceneClass.endAttributes();
      ++it) {
    const rdl2::Attribute *attribute = *it;
    if (attribute->isBindable() && obj->getBinding(*attribute))
      obj->setBinding(*attribute, nullptr);
  }
  obj->resetAllToDefault();
}

// Whether a shader of an `rdla` fragment can be absent at a point: a
// `presence` that is not simply one (the Dwa materials), or
// UsdPreviewSurface's cutout.
bool objectUsesPresence(const rdl2::SceneObject *obj)
{
  const rdl2::SceneClass &sceneClass = obj->getSceneClass();
  for (auto it = sceneClass.beginAttributes(); it != sceneClass.endAttributes();
      ++it) {
    const rdl2::Attribute *attribute = *it;
    if (attribute->getType() != rdl2::TYPE_FLOAT)
      continue;
    const rdl2::AttributeKey<rdl2::Float> key(*attribute);
    const bool bound =
        attribute->isBindable() && obj->getBinding(*attribute) != nullptr;
    if (attribute->getName() == "presence" && (bound || obj->get(key) != 1.f))
      return true;
    if (attribute->getName() == "opacityThreshold"
        && (bound || obj->get(key) > 0.f))
      return true;
  }
  return false;
}

// What an `rdla` fragment consists of: (object name, SceneClass name).
using FragmentContents = std::vector<std::pair<std::string, std::string>>;

// Deletes every object of `context` but its SceneVariables. Only for a
// context MoonRay never rendered: nothing may hold on to the objects.
void clearContext(rdl2::SceneContext &context)
{
  std::vector<std::string> names;
  for (auto it = context.beginSceneObject(); it != context.endSceneObject();
      ++it) {
    if (it->second != &context.getSceneVariables())
      names.push_back(it->first);
  }
  for (const auto &name : names)
    context.deleteSceneObject(name);
}

// Loads `fragment` into `scratch`, a SceneContext that exists for nothing
// else, and reports what it creates. This is where a fragment fails - a Lua
// error, a SceneClass that is not installed, an attribute the class does not
// have, a binding of the wrong type, a missing root - so that nothing of a
// broken fragment ever reaches the scene being rendered, and it is the only
// way to learn which objects the text refers to: the reader creates an object
// or silently picks up an existing one of that name, and the real scene
// cannot tell the two apart. `scratch` is left empty again, keeping only the
// SceneClasses it had to load, which is most of what an inspection costs.
bool inspectFragment(rdl2::SceneContext &scratch,
    const Material::Fragment &fragment,
    FragmentContents &contents,
    std::string &error)
{
  contents.clear();

  struct ClearOnExit
  {
    rdl2::SceneContext &context;
    ~ClearOnExit()
    {
      try {
        clearContext(context);
      } catch (...) {
        // An object that will not go only costs its memory.
      }
    }
  } clearOnExit{scratch};

  try {
    rdl2::AsciiReader reader(scratch);
    // An attribute the class does not have, a value of the wrong type and a
    // binding to an attribute that cannot be bound are only warnings to the
    // reader, which then carries on without them. For a generated fragment
    // they mean the material is not what was asked for.
    reader.setWarningsAsErrors(true);
    reader.fromString(fragment.source, "@rdla material");

    for (auto it = scratch.beginSceneObject(); it != scratch.endSceneObject();
        ++it) {
      const rdl2::SceneObject *obj = it->second;
      if (obj == &scratch.getSceneVariables())
        continue;
      // Materials, maps, normal maps, displacements and volume shaders. A
      // camera, a light, a render output or a layer in a fragment would
      // become part of the device's scene behind its back.
      if (!obj->isA<rdl2::Shader>()) {
        error = "the fragment creates " + obj->getSceneClass().getName()
            + "(\"" + obj->getName()
            + "\"), which is neither a material nor a map";
        return false;
      }
      // RDL2 lets any object be bound to a bindable attribute; MoonRay then
      // samples it as a Map without looking (shading::evalColor()), and
      // anything else is a crash on a render thread.
      const rdl2::SceneClass &sceneClass = obj->getSceneClass();
      for (auto a = sceneClass.beginAttributes();
          a != sceneClass.endAttributes();
          ++a) {
        if (!(*a)->isBindable())
          continue;
        const rdl2::SceneObject *bound = obj->getBinding(**a);
        if (bound && !bound->isA<rdl2::Map>()) {
          error = bound->getSceneClass().getName() + "(\"" + bound->getName()
              + "\") is bound to '" + (*a)->getName() + "' of \""
              + obj->getName() + "\", but only a Map can be bound";
          return false;
        }
      }
      contents.emplace_back(obj->getName(), obj->getSceneClass().getName());
    }

    if (!scratch.sceneObjectExists(fragment.materialName)) {
      error = "the fragment does not create an object named \""
          + fragment.materialName + "\" ('materialName')";
      return false;
    }
    const rdl2::SceneObject *root =
        scratch.getSceneObject(fragment.materialName);
    if (!root->isA<rdl2::Material>()) {
      error = "'materialName' names " + root->getSceneClass().getName()
          + "(\"" + fragment.materialName + "\"), which is not a Material";
      return false;
    }
  } catch (const std::exception &e) {
    error = e.what();
    return false;
  } catch (...) {
    error = "unknown exception while reading the fragment";
    return false;
  }
  return true;
}

} // namespace

MoonrayScene::MoonrayScene(MoonrayDeviceGlobalState *state) : m_state(state)
{
  m_backend = MoonrayBackend::acquire(m_error);
}

MoonrayScene::~MoonrayScene()
{
  if (!m_backend)
    return;

  // ~RenderContext must run on the backend thread, and before the backend
  // releases the global driver (docs/adr/0002, 0005).
  //
  // MoonrayBackend::execute() rethrows on this thread, and this is a
  // destructor - implicitly noexcept, so an escaping exception is
  // std::terminate() and the whole application dies with no message at all.
  // Teardown is exactly where that is least acceptable: the caller is done
  // with the device and can do nothing about the failure. The exception is
  // therefore reported and swallowed.
  try {
    m_backend->execute([this]() {
      std::lock_guard<std::mutex> guard(m_ctxMutex);
      m_objects.clear();
      m_fragments.clear();
      m_fragmentScratch.reset();
      m_ctx.reset();
    });
  } catch (const std::exception &e) {
    std::fprintf(stderr,
        "[moonray] tearing down the render context failed: %s\n",
        e.what());
  } catch (...) {
    std::fprintf(stderr,
        "[moonray] tearing down the render context failed with an unknown "
        "exception\n");
  }
}

bool MoonrayScene::valid() const
{
  return static_cast<bool>(m_backend);
}

const std::string &MoonrayScene::error() const
{
  return m_error;
}

const std::string &MoonrayScene::dsoPath() const
{
  static const std::string empty;
  return m_backend ? m_backend->dsoPath() : empty;
}

int MoonrayScene::objectsUpdatedLastSync() const
{
  return m_objectsUpdated;
}

int MoonrayScene::objectsUnchangedLastSync() const
{
  return m_objectsUnchanged;
}

bool MoonrayScene::layerRebuiltLastSync() const
{
  return m_layerRebuilt;
}

float MoonrayScene::syncMillisecondsLastFrame() const
{
  return m_syncMilliseconds;
}

int MoonrayScene::fragmentLoads() const
{
  return m_fragmentLoads;
}

int MoonrayScene::fragmentObjects() const
{
  return m_fragmentObjects;
}

int MoonrayScene::fragmentObjectsRetired() const
{
  return m_fragmentObjectsRetired;
}

int MoonrayScene::meshLights() const
{
  return int(m_meshLights.size());
}

int MoonrayScene::meshLightsOverLimit() const
{
  return m_meshLightsOverLimit;
}

void MoonrayScene::setMaxMeshLights(int count)
{
  m_deviceMaxMeshLights = std::max(0, count);
}

void MoonrayScene::setExecutionMode(const std::string &mode)
{
  m_requestedExecutionMode = mode;
}

const std::string &MoonrayScene::executionMode() const
{
  return m_executionMode;
}

void MoonrayScene::ensureContext()
{
  if (m_ctx)
    return;

  std::lock_guard<std::mutex> guard(m_ctxMutex);
  // The execution mode is read when the RenderContext initializes, and it
  // lives on the process-global RenderOptions, so this is the moment to
  // apply it (docs/adr/0005: one MoonRay driver per process).
  if (!m_requestedExecutionMode.empty())
    m_backend->options().setDesiredExecutionMode(m_requestedExecutionMode);
  m_ctx = std::make_unique<moonray::rndr::RenderContext>(m_backend->options());
}

scene_rdl2::rdl2::SceneObject *MoonrayScene::createObject(
    const char *className, const std::string &name)
{
  // createSceneObject() returns the existing object when the name is already
  // taken, so this is "create or look up".
  return m_ctx->getSceneContext().createSceneObject(className, name);
}

scene_rdl2::rdl2::SceneObject *MoonrayScene::syncedObject(
    const char *className,
    const std::string &name,
    helium::TimeStamp version,
    bool &needsWrite,
    bool force)
{
  auto &entry = m_objects[name];
  if (!entry.object)
    entry.object = createObject(className, name);

  needsWrite = force || !entry.written || entry.version != version;
  if (needsWrite) {
    entry.version = version;
    entry.written = true;
    ++m_objectsUpdated;
  } else {
    ++m_objectsUnchanged;
  }

  return entry.object;
}

void MoonrayScene::retireObject(const std::string &name)
{
  // `name` and everything named under it: a geometry's primitive attributes
  // are separate UserData objects called "<geometry>/attribute/<what>".
  const std::string prefix = name + "/";
  for (auto it = m_objects.begin(); it != m_objects.end();) {
    const bool mine =
        it->first == name || it->first.compare(0, prefix.size(), prefix) == 0;
    if (!mine) {
      ++it;
      continue;
    }
    if (it->second.object) {
      rdl2::SceneObject::UpdateGuard guard(it->second.object);
      it->second.object->resetAllToDefault();
    }
    it = m_objects.erase(it);
  }
}

rdl2::Material *MoonrayScene::errorMaterial()
{
  if (!m_errorMaterial) {
    rdl2::SceneObject *obj =
        createObject("UsdPreviewSurface", "/anari/material/error");
    rdl2::SceneObject::UpdateGuard guard(obj);
    // Magenta whatever the lighting: a diffuse surface under no light would
    // be as black as the bug it is supposed to point at.
    obj->set("diffuseColor", rdl2::Rgb(1.f, 0.f, 1.f));
    obj->set("emissiveColor", rdl2::Rgb(0.5f, 0.f, 0.5f));
    obj->set("useSpecularWorkflow", 0);
    obj->set("metallic", 0.f);
    obj->set("roughness", 1.f);
    obj->set("clearcoat", 0.f);
    obj->set("ior", 1.f);
    m_errorMaterial = obj->asA<rdl2::Material>();
  }
  return m_errorMaterial;
}

void MoonrayScene::holdMaterial(rdl2::SceneObject *material)
{
  if (!material || !material->isA<rdl2::Material>()
      || !m_heldMaterials.insert(material->getName()).second) {
    return;
  }
  auto *layer = createObject("Layer", "MeshLightLayer")->asA<rdl2::Layer>();
  auto *lightSet =
      createObject("LightSet", "MeshLightLightSet")->asA<rdl2::LightSet>();
  auto *holder = createObject("RdlMeshGeometry", "/anari/materialHolder")
                     ->asA<rdl2::Geometry>();
  rdl2::SceneObject::UpdateGuard guard(layer);
  layer->assign(holder,
      material->getName(),
      material->asA<rdl2::Material>(),
      lightSet,
      /*displacement=*/nullptr,
      /*volumeShader=*/nullptr);
}

void MoonrayScene::retireFragmentObjects(FragmentState &state)
{
  rdl2::SceneContext &sc = m_ctx->getSceneContext();
  for (const auto &name : state.objects) {
    if (sc.sceneObjectExists(name))
      resetObject(sc.getSceneObject(name));
    m_fragmentObjectOwner[name] = 0;
  }
  m_fragmentObjects -= int(state.objects.size());
  m_fragmentObjectsRetired += int(state.objects.size());
  state.objects.clear();
  state.root = nullptr;
}

void MoonrayScene::retireUnusedFragments()
{
  // RDL2 objects are never deleted (see retireObject()), so the objects of a
  // material that has left the scene - released, or merely not assigned to
  // anything any more - are reset to their defaults, which lets go of what
  // they hold. A material that comes back is loaded again.
  for (auto it = m_fragments.begin(); it != m_fragments.end();) {
    if (it->second.used) {
      ++it;
      continue;
    }
    retireFragmentObjects(it->second);
    it = m_fragments.erase(it);
    ++m_fragmentsRetired;
  }
}

rdl2::Material *MoonrayScene::syncFragment(
    const Material *material, const Material::Fragment &fragment)
{
  const uint64_t id = material->syncId();
  FragmentState &state = m_fragments[id];
  state.used = true;

  // The same text with the same images is not read again, however often the
  // application commits it. The images are compared as textures: an image
  // whose pixels did not change still has the texture it had.
  std::vector<Material::FragmentImage> images = material->fragmentImages();
  bool sameImages = images.size() == state.images.size();
  for (size_t i = 0; sameImages && i < images.size(); ++i) {
    sameImages = images[i].name == state.images[i].name
        && images[i].texture == state.images[i].texture;
  }
  const bool retry = !state.root && state.retiredAtLoad != m_fragmentsRetired;
  if (state.version == fragment.version && state.version != 0 && sameImages
      && !retry) {
    ++m_objectsUnchanged;
    return state.root ? state.root : errorMaterial();
  }
  state.version = fragment.version;
  state.retiredAtLoad = m_fragmentsRetired;
  state.emitterReported = false;
  state.readsChecked = false;
  ++m_objectsUpdated;
  ++m_fragmentLoads;

  rdl2::SceneContext &sc = m_ctx->getSceneContext();

  std::string error;
  FragmentContents contents;
  bool ok = true;
  if (fragment.source.empty() || fragment.materialName.empty()) {
    error = "'source' and 'materialName' are both required";
    ok = false;
  }
  const auto loadStart = std::chrono::steady_clock::now();
  if (ok && !m_fragmentScratch) {
    try {
      m_fragmentScratch = std::make_unique<rdl2::SceneContext>();
      m_fragmentScratch->setDsoPath(sc.getDsoPath());
    } catch (const std::exception &e) {
      error = e.what();
      ok = false;
    }
  }
  ok = ok && inspectFragment(*m_fragmentScratch, fragment, contents, error);
  const auto inspected = std::chrono::steady_clock::now();

  // The names have to be this material's to use: its own from an earlier
  // version, retired ones, or new ones. And because RDL2 objects are never
  // deleted, a name keeps the SceneClass it was first created with.
  for (size_t i = 0; ok && i < contents.size(); ++i) {
    const std::string &name = contents[i].first;
    const std::string &className = contents[i].second;
    auto owner = m_fragmentObjectOwner.find(name);
    if (owner != m_fragmentObjectOwner.end()) {
      if (owner->second != 0 && owner->second != id) {
        error = "the object name \"" + name
            + "\" is already used by another rdla material";
        ok = false;
      }
    } else if (sc.sceneObjectExists(name)) {
      error = "the object name \"" + name
          + "\" is used by the device's own scene";
      ok = false;
    }
    if (ok && sc.sceneObjectExists(name)) {
      const std::string &existing =
          sc.getSceneObject(name)->getSceneClass().getName();
      if (existing != className) {
        error = "the object \"" + name + "\" was created as " + existing
            + " earlier and cannot become " + className
            + ": RDL2 objects are never deleted, so an object that changes "
              "its class needs a new name";
        ok = false;
      }
    }
  }

  // Whatever the previous version set must not survive: an attribute or a
  // binding the new text no longer mentions would otherwise keep its old
  // value, and an object it no longer has would keep shading through
  // whatever still points at it. Both are a reset; the reader then writes
  // the new state into the objects that stay.
  retireFragmentObjects(state);

  if (ok) {
    try {
      rdl2::AsciiReader reader(sc);
      reader.setWarningsAsErrors(true);
      reader.fromString(fragment.source, "@rdla material");
    } catch (const std::exception &e) {
      error = e.what();
      ok = false;
    } catch (...) {
      error = "unknown exception while reading the fragment";
      ok = false;
    }

    // Owned from here on, also after a failure: the next load, or the
    // material going away, resets them.
    for (const auto &entry : contents) {
      const std::string &name = entry.first;
      if (!sc.sceneObjectExists(name))
        continue;
      auto owner = m_fragmentObjectOwner.find(name);
      if (owner != m_fragmentObjectOwner.end() && owner->second == 0)
        --m_fragmentObjectsRetired;
      m_fragmentObjectOwner[name] = id;
      state.objects.push_back(name);
    }
    m_fragmentObjects += int(state.objects.size());
  }

  if (ok) {
    // The images: a string attribute whose value is the name of one of the
    // material's image parameters gets the name of that image's texture.
    for (const auto &name : state.objects) {
      rdl2::SceneObject *obj = sc.getSceneObject(name);
      const rdl2::SceneClass &sceneClass = obj->getSceneClass();
      for (auto it = sceneClass.beginAttributes();
          it != sceneClass.endAttributes();
          ++it) {
        const rdl2::Attribute *attribute = *it;
        if (attribute->getType() == rdl2::TYPE_STRING_VECTOR) {
          // A list of textures: each name that is an image parameter.
          const rdl2::AttributeKey<rdl2::StringVector> listKey(*attribute);
          rdl2::StringVector names = obj->get(listKey);
          bool replaced = false;
          for (std::string &entry : names) {
            for (const auto &image : images) {
              if (entry == image.name) {
                entry = image.texture->name();
                replaced = true;
                break;
              }
            }
          }
          if (replaced) {
            rdl2::SceneObject::UpdateGuard guard(obj);
            obj->set(listKey, names);
          }
          continue;
        }
        if (attribute->getType() != rdl2::TYPE_STRING)
          continue;
        const rdl2::AttributeKey<rdl2::String> key(*attribute);
        const std::string value = obj->get(key);
        if (value.empty())
          continue;
        bool isImage = false;
        for (const auto &image : images) {
          if (value == image.name) {
            rdl2::SceneObject::UpdateGuard guard(obj);
            obj->set(key, image.texture->name());
            isImage = true;
            break;
          }
        }
        // A texture name nothing stands behind is not an error to MoonRay -
        // the map renders the scene's fatal color and a line goes to
        // MoonRay's own log - so the application is told here.
        std::error_code ec;
        if (!isImage && attribute->isFilename()
            && value.find("<UDIM>") == std::string::npos
            && !std::filesystem::exists(value, ec)) {
          material->reportMessage(ANARI_SEVERITY_WARNING,
              "'%s' of %s(\"%s\") in the rdla material is \"%s\", which is "
              "neither an image parameter of the material (an image2D sampler "
              "with an image) nor a file - MoonRay renders its error color "
              "there",
              attribute->getName().c_str(),
              sceneClass.getName().c_str(),
              name.c_str(),
              value.c_str());
        }
      }
    }

    state.root = sc.getSceneObject(fragment.materialName)->asA<rdl2::Material>();
    if (!state.root) {
      error = "\"" + fragment.materialName + "\" is not a Material";
      ok = false;
    }
  }
  if (ok) {
    // What a RaySwitchMaterial of the fragment switches to.
    for (const auto &name : state.objects)
      holdMaterial(sc.getSceneObject(name));
  }

  // The textures of the previous version are let go here, after the objects
  // have been pointed at the new ones (or reset).
  state.images = std::move(images);

  state.usesPresence = false;
  if (ok) {
    for (const auto &name : state.objects)
      state.usesPresence = state.usesPresence
          || objectUsesPresence(sc.getSceneObject(name));
  }

  if (std::getenv("OPENMOONRAY_ANARI_DEBUG_FRAGMENT")) {
    const auto done = std::chrono::steady_clock::now();
    std::fprintf(stderr,
        "[moonray] rdla \"%s\": %zu object(s), %zu image(s), inspected in "
        "%.2f ms, applied in %.2f ms, %s\n",
        fragment.materialName.c_str(),
        contents.size(),
        state.images.size(),
        std::chrono::duration<float, std::milli>(inspected - loadStart).count(),
        std::chrono::duration<float, std::milli>(done - inspected).count(),
        ok ? "loaded" : "failed");
  }

  if (!ok) {
    // Nothing of a fragment that failed half way stays in effect.
    for (const auto &name : state.objects) {
      if (sc.sceneObjectExists(name))
        resetObject(sc.getSceneObject(name));
    }
    state.root = nullptr;
    material->reportMessage(ANARI_SEVERITY_ERROR,
        "the rdla material could not be loaded and renders with the error "
        "material: %s",
        error.c_str());
    return errorMaterial();
  }

  return state.root;
}

bool MoonrayScene::fragmentEmitter(const Material *material,
    const Material::Emitter &emitter,
    rdl2::Material *&sampledRoot,
    rdl2::SceneObject *&emissionMap)
{
  auto found = m_fragments.find(material->syncId());
  if (found == m_fragments.end() || !found->second.root)
    return false; // not loaded: the error material, reported there
  FragmentState &state = found->second;
  rdl2::SceneContext &sc = m_ctx->getSceneContext();

  // Only objects of this fragment: a name of another material or of the
  // device's own scene is not the application's to point a light at.
  const auto owned = [&](const std::string &name) {
    return std::find(state.objects.begin(), state.objects.end(), name)
        != state.objects.end()
        && sc.sceneObjectExists(name);
  };
  std::string error;
  rdl2::SceneObject *root = nullptr;
  rdl2::SceneObject *map = nullptr;
  if (!owned(emitter.sampledMaterialName)) {
    error = "the fragment does not create an object named \""
        + emitter.sampledMaterialName + "\" ('sampledMaterialName')";
  } else {
    root = sc.getSceneObject(emitter.sampledMaterialName);
    if (!root->isA<rdl2::Material>())
      error = "'sampledMaterialName' names " + root->getSceneClass().getName()
          + ", which is not a Material";
  }
  if (error.empty() && !emitter.mapName.empty()) {
    if (!owned(emitter.mapName)) {
      error = "the fragment does not create an object named \""
          + emitter.mapName + "\" ('emissionMap')";
    } else {
      map = sc.getSceneObject(emitter.mapName);
      if (!map->isA<rdl2::Map>())
        error = "'emissionMap' names " + map->getSceneClass().getName()
            + ", which is not a Map";
    }
  }
  if (!error.empty()) {
    if (!state.emitterReported) {
      state.emitterReported = true;
      material->reportMessage(ANARI_SEVERITY_WARNING,
          "the emission of the rdla material is not sampled by lights: %s",
          error.c_str());
    }
    return false;
  }
  sampledRoot = root->asA<rdl2::Material>();
  emissionMap = map;
  return true;
}

unsigned MoonrayScene::emissionReads(
    const Material *material, const Material::Emitter &emitter)
{
  if (emitter.mapName.empty())
    return 0;
  auto found = m_fragments.find(material->syncId());
  if (found == m_fragments.end() || !found->second.root)
    return 0;
  FragmentState &state = found->second;
  if (state.readsChecked && state.readsCheckedMap == emitter.mapName)
    return state.reads;
  rdl2::SceneContext &sc = m_ctx->getSceneContext();
  state.readsChecked = true;
  state.readsCheckedMap = emitter.mapName;
  const bool owned = std::find(state.objects.begin(),
                         state.objects.end(),
                         emitter.mapName)
          != state.objects.end()
      && sc.sceneObjectExists(emitter.mapName);
  state.reads = owned ? mapReads(sc.getSceneObject(emitter.mapName)) : 0;
  if ((state.reads & ReadsView) != 0) {
    material->reportMessage(ANARI_SEVERITY_INFO,
        "the emission of the rdla material depends on the direction of view "
        "or on the side which is hit ('emissionMap' \"%s\"): it is not "
        "sampled by lights",
        emitter.mapName.c_str());
  }
  return state.reads;
}

void MoonrayScene::writeGeometry(const Geometry *geometry,
    rdl2::SceneObject *obj,
    const std::string &name)
{
  // The UserData objects are written first, before the geometry's own update
  // is opened: they are separate scene objects, and the geometry only needs to
  // end up holding pointers to them.
  rdl2::SceneObjectVector userData;
  for (const auto &attribute : geometry->primitiveAttributes()) {
    if (attribute.values.empty())
      continue;

    // One UserData object per attribute. MoonRay keys an attribute on name
    // and type, and a geometry may export the same name as two types
    // (Geometry::attributeName()), so the vec3f ones say so in their name.
    const bool isVec3f =
        attribute.type == Geometry::PrimitiveAttribute::Type::Vec3f;
    rdl2::SceneObject *dataObj = createObject("UserData",
        name + "/attribute/" + attribute.name + (isVec3f ? ".vec3f" : ""));
    {
      rdl2::SceneObject::UpdateGuard guard(dataObj);
      dataObj->set("rate", attribute.rate);
      switch (attribute.type) {
      case Geometry::PrimitiveAttribute::Type::Color: {
        rdl2::RgbVector values;
        values.reserve(attribute.values.size() / 3);
        for (size_t i = 0; i + 2 < attribute.values.size(); i += 3) {
          values.emplace_back(attribute.values[i],
              attribute.values[i + 1],
              attribute.values[i + 2]);
        }
        dataObj->set("color_key", attribute.name);
        dataObj->set("color_values_0", values);
        break;
      }
      case Geometry::PrimitiveAttribute::Type::Vec3f: {
        rdl2::Vec3fVector values;
        values.reserve(attribute.values.size() / 3);
        for (size_t i = 0; i + 2 < attribute.values.size(); i += 3) {
          values.emplace_back(attribute.values[i],
              attribute.values[i + 1],
              attribute.values[i + 2]);
        }
        dataObj->set("vec3f_key", attribute.name);
        dataObj->set("vec3f_values_0", values);
        break;
      }
      case Geometry::PrimitiveAttribute::Type::Vec2f: {
        rdl2::Vec2fVector values;
        values.reserve(attribute.values.size() / 2);
        for (size_t i = 0; i + 1 < attribute.values.size(); i += 2)
          values.emplace_back(attribute.values[i], attribute.values[i + 1]);
        dataObj->set("vec2f_key", attribute.name);
        dataObj->set("vec2f_values_0", values);
        break;
      }
      case Geometry::PrimitiveAttribute::Type::Float: {
        rdl2::FloatVector values(
            attribute.values.begin(), attribute.values.end());
        dataObj->set("float_key", attribute.name);
        dataObj->set("float_values_0", values);
        break;
      }
      }
    }
    userData.push_back(dataObj);
  }

  rdl2::SceneObject::UpdateGuard guard(obj);
  geometry->syncRdl2(*obj);
  // Always written, so that dropping `vertex.color` really drops it.
  obj->set("primitive_attributes", userData);
}

void MoonrayScene::appendObjectInfo(const std::vector<Instance *> &instances,
    rdl2::SceneObject *obj,
    const std::string &name,
    bool instancer)
{
  bool anyColor = false, anyRandom = false, anyIndex = false;
  for (const Instance *instance : instances) {
    anyColor = anyColor || instance->hasObjectColor();
    anyRandom = anyRandom || instance->hasObjectRandom();
    anyIndex = anyIndex || instance->id() != ~0u;
  }

  const rdl2::AttributeKey<rdl2::SceneObjectVector> key =
      obj->getSceneClass().getAttributeKey<rdl2::SceneObjectVector>(
          "primitive_attributes");
  rdl2::SceneObjectVector userData = obj->get(key);
  // One value for a geometry (constant), one per instance for an instancer
  // (0: MoonRay tells by the number of values).
  const int rate = instancer ? 0 : 1;

  // Primitive attributes a shading network reads by name (AttributeMap).
  if (anyColor) {
    rdl2::SceneObject *dataObj =
        createObject("UserData", name + "/attribute/anari_object_color");
    rdl2::RgbVector values;
    for (const Instance *instance : instances) {
      const float4 &c = instance->objectColor();
      values.emplace_back(c.x, c.y, c.z);
    }
    rdl2::SceneObject::UpdateGuard guard(dataObj);
    dataObj->set("rate", rate);
    dataObj->set("color_key", std::string("anari_object_color"));
    dataObj->set("color_values_0", values);
    userData.push_back(dataObj);
  }
  if (anyRandom) {
    rdl2::SceneObject *dataObj =
        createObject("UserData", name + "/attribute/anari_object_random");
    rdl2::FloatVector values;
    for (const Instance *instance : instances)
      values.push_back(instance->objectRandom());
    rdl2::SceneObject::UpdateGuard guard(dataObj);
    dataObj->set("rate", rate);
    dataObj->set("float_key", std::string("anari_object_random"));
    dataObj->set("float_values_0", values);
    userData.push_back(dataObj);
  }
  if (anyIndex) {
    rdl2::SceneObject *dataObj =
        createObject("UserData", name + "/attribute/anari_object_index");
    rdl2::FloatVector values;
    for (const Instance *instance : instances)
      values.push_back(instance->id() != ~0u ? float(instance->id()) : 0.f);
    rdl2::SceneObject::UpdateGuard guard(dataObj);
    dataObj->set("rate", rate);
    dataObj->set("float_key", std::string("anari_object_index"));
    dataObj->set("float_values_0", values);
    userData.push_back(dataObj);
  }

  rdl2::SceneObject::UpdateGuard guard(obj);
  obj->set(key, userData);
}

void MoonrayScene::syncSurfaces(World *world,
    scene_rdl2::rdl2::SceneObject *lightSetObj,
    scene_rdl2::rdl2::SceneObject *geometrySetObj,
    scene_rdl2::rdl2::SceneObject *layerObj)
{
  auto *lightSet = lightSetObj->asA<rdl2::LightSet>();
  auto *geometrySet = geometrySetObj->asA<rdl2::GeometrySet>();
  auto *layer = layerObj->asA<rdl2::Layer>();

  // --- which instances share which group ----------------------------------
  // A group used by more than one instance is what MoonRay-native instancing
  // is for: the geometry data is written once, into a prototype, and an
  // RdlInstancerGeometry carries the per-instance transforms. A group used
  // once takes the direct path, where the instance transform is simply the
  // geometry's node_xform - an instancer there would only add indirection.
  std::vector<Group *> groupOrder;
  std::unordered_map<Group *, std::vector<Instance *>> groupInstances;
  for (auto *instance : world->instances()) {
    if (!instance || !instance->isValid())
      continue;
    Group *group = instance->group();
    if (!group)
      continue;
    auto it = groupInstances.find(group);
    if (it == groupInstances.end()) {
      groupOrder.push_back(group);
      groupInstances[group] = {instance};
    } else {
      it->second.push_back(instance);
    }
  }

  // What goes into the GeometrySet (i.e. what MoonRay builds a BVH for) and
  // what gets a layer assignment. A prototype is assigned but *not* in the
  // GeometrySet: the assignment is where its material comes from, and being
  // referenced by an instancer is what turns it into a shared primitive that
  // is never rendered on its own.
  std::vector<rdl2::Geometry *> renderGeometries;
  std::vector<LayerAssignment> assignments;

  // `rdla` materials that are no longer in the scene are retired before any
  // of the ones that are is loaded: an application that replaces a material
  // object by a new one with the same fragment gives the new one the names
  // the old one still holds.
  for (auto &entry : m_fragments)
    entry.second.used = false;
  m_needPresenceShadows = false;
  for (Group *group : groupOrder) {
    for (auto *surface : group->surfaces()) {
      if (!surface || !surface->isValid())
        continue;
      m_needPresenceShadows =
          m_needPresenceShadows || surface->material()->usesPresence();
      if (!surface->material()->fragment())
        continue;
      auto known = m_fragments.find(surface->material()->syncId());
      if (known != m_fragments.end())
        known->second.used = true;
    }
  }
  retireUnusedFragments();

  // --- which emitters lights sample ---------------------------------------
  // One MeshLight serves a surface: its mesh holds the sampled sides of all
  // instances - of every group the surface is in - which the same rays see.
  // Unless the map of the emission reads the space of the object or what an
  // instance has of its own (emissionReads()): then every instance is a
  // light, with its sides. An emission which reads the direction of view or
  // the side which is hit stays with the surface: a light has neither. MoonRay samples every light at every hit and
  // intersects every sampled direction with every mesh light, so the number
  // of lights is bounded (`moonray.maxMeshLights`), and so is the number of
  // triangles their meshes have together (`moonray.maxMeshLightTriangles`:
  // MoonRay builds the sampling structure of a light again for every edit
  // of it); the emitters with the most power get the lights. All instances of a surface or none: they share one
  // material, which either emits for all rays or leaves that to the light.
  // An instance no diffuse or glossy ray sees lights nothing.
  const uint32_t litBy = visibility::DiffuseReflection
      | visibility::DiffuseTransmission | visibility::GlossyReflection
      | visibility::GlossyTransmission | visibility::Phase;
  // The sampled surfaces, and whether each instance is a light of its own.
  std::map<const Surface *, bool> sampledSurfaces;
  m_meshLightsOverLimit = 0;
  {
    struct Candidate
    {
      const Surface *surface;
      bool perInstance;
      // False for an emission no light can have (ReadsView): it counts for
      // the power of the scene only.
      bool sampleable;
      std::vector<uint32_t> seenBy;
      int instances;
      size_t triangles;
      float power;
      int lights() const
      {
        return perInstance ? instances : int(seenBy.size());
      }
    };
    std::vector<Candidate> candidates;
    int loadedEarly = 0;
    for (Group *group : groupOrder) {
      for (auto *surface : group->surfaces()) {
        if (!surface || !surface->isValid())
          continue;
        const Material *material = surface->material();
        const Material::Emitter emitter = material->emitter();
        const float area = surface->geometry()->surfaceArea();
        if (emitter.sides == 0 || !(area > 0.f))
          continue;
        const int sides = (emitter.sides & 1) + ((emitter.sides >> 1) & 1);
        // A surface may be in more than one group.
        auto known = std::find_if(candidates.begin(),
            candidates.end(),
            [&](const Candidate &c) { return c.surface == surface; });
        Candidate fresh{surface, false, true, {}, 0, 0, 0.f};
        if (known == candidates.end() && material->fragment()
            && !emitter.mapName.empty()) {
          // The map is an object of the fragment, which the loop over the
          // surfaces below would load; that one then finds it loaded.
          syncFragment(material, *material->fragment());
          ++loadedEarly;
          const unsigned reads = emissionReads(material, emitter);
          fresh.sampleable = (reads & ReadsView) == 0;
          fresh.perInstance = (reads & ReadsInstance) != 0;
        }
        Candidate &candidate = known != candidates.end() ? *known : fresh;
        for (const Instance *instance : groupInstances[group]) {
          const uint32_t lit = instance->visibility() & litBy;
          if (lit == 0)
            continue;
          if (std::find(candidate.seenBy.begin(), candidate.seenBy.end(), lit)
              == candidate.seenBy.end()) {
            candidate.seenBy.push_back(lit);
          }
          ++candidate.instances;
          candidate.triangles +=
              size_t(sides) * surface->geometry()->triangleCount();
          // The area grows with the square of a uniform scale.
          const float scale = std::cbrt(
              std::fabs(float(linalg::determinant(instance->xfm()))));
          candidate.power += float(sides) * area * scale * scale
              * (emitter.color.x + emitter.color.y + emitter.color.z);
        }
        if (known == candidates.end() && !fresh.seenBy.empty())
          candidates.push_back(std::move(fresh));
      }
    }
    // Each of them is counted once more as unchanged in the loop below.
    m_objectsUnchanged -= loadedEarly;
    std::stable_sort(candidates.begin(),
        candidates.end(),
        [](const Candidate &a, const Candidate &b) {
          return a.power > b.power;
        });
    // An emitter with a small share of the power of all of them is not worth
    // a light: as a surface it adds as little noise as it adds light, as a
    // light it costs what every light costs.
    float allPower = 0.f;
    for (const Candidate &candidate : candidates)
      allPower += candidate.power;
    int lightsLeft = m_maxMeshLights;
    size_t trianglesLeft = m_maxMeshLightTriangles;
    for (const Candidate &candidate : candidates) {
      if (!candidate.sampleable
          || candidate.power < m_minMeshLightPowerShare * allPower) {
        continue;
      }
      const int lights = candidate.lights();
      if (lights <= lightsLeft && candidate.triangles <= trianglesLeft) {
        lightsLeft -= lights;
        trianglesLeft -= candidate.triangles;
        sampledSurfaces[candidate.surface] = candidate.perInstance;
      } else {
        m_meshLightsOverLimit += lights;
      }
    }
  }
  // One instance of a sampled surface, for the lights made after the loop.
  struct EmitterInstance
  {
    const Surface *surface;
    const Instance *instance;
    const Geometry *geometry;
    Material::Emitter emitter;
    rdl2::SceneObject *emissionMap;
    helium::TimeStamp materialVersion;
  };
  std::vector<EmitterInstance> emitterInstances;

  for (Group *group : groupOrder) {
    const auto &instances = groupInstances[group];
    const bool instanced = instances.size() > 1;

    helium::TimeStamp instancesVersion = 0;
    for (auto *instance : instances)
      instancesVersion = newest(instancesVersion, instance->lastUpdated());

    for (auto *surface : group->surfaces()) {
      if (!surface || !surface->isValid())
        continue;

      const Geometry *geometry = surface->geometry();
      const Material *material = surface->material();
      const char *geometryClass = geometry->rdl2ClassName();
      const char *materialClass = material->rdl2ClassName();
      // A material that brings its RDL2 objects along as text; everything
      // below that builds a material out of ANARI parameters is skipped.
      const Material::Fragment *fragment = material->fragment();

      // Samplers first: a textured parameter is an RDL2 binding to a Map
      // object, so the Map has to exist before the material is written.
      std::vector<std::pair<const char *, rdl2::SceneObject *>> bindings;
      bool write = false;
      // The newest state of the material *and* of everything bound to it:
      // what the material's own attributes hold depends on its samplers too
      // (Sampler::attributeFactor(), and whether a sampler is usable at all).
      helium::TimeStamp materialVersion = material->lastUpdated();
      for (const auto &binding : material->samplerBindings()) {
        Sampler *sampler = binding.sampler;
        if (!sampler || !sampler->isValid() || !binding.rdl2Attribute)
          continue;
        materialVersion = newest(materialVersion, sampler->lastUpdated());
        const char *samplerClass = sampler->rdl2ClassName();
        // One map per usage: a color, a scalar and a normal attribute take
        // the sampler's value in different ways (SamplerUsage). The color
        // map keeps the plain name.
        const std::string samplerName =
            objectName("sampler", samplerClass, sampler);
        const char *usageSuffix =
            binding.usage == SamplerUsage::Scalar
                ? "/scalar"
                : (binding.usage == SamplerUsage::Normal ? "/normal" : "");
        rdl2::SceneObject *mapObj = syncedObject(samplerClass,
            samplerName + usageSuffix,
            sampler->lastUpdated(),
            write);
        if (write) {
          // A sampler that reads a geometry attribute rather than the
          // surface's st gets it through an AttributeMap bound to the map's
          // input coordinates (Sampler::inputPrimitiveAttribute()). Written
          // first, like the geometry's UserData: it is a separate object the
          // map only has to point at, and one serves all usages.
          rdl2::SceneObject *inputObj = nullptr;
          if (const char *input = sampler->inputPrimitiveAttribute()) {
            inputObj = createObject("AttributeMap", samplerName + "/input");
            rdl2::SceneObject::UpdateGuard guard(inputObj);
            inputObj->set("map_type", 0); // primitive attribute
            inputObj->set("primitive_attribute_name", std::string(input));
            inputObj->set("primitive_attribute_type",
                sampler->inputPrimitiveAttributeType());
            // ImageMap reads a negative third component as "no coordinate"
            // and returns black; a geometry without the attribute samples the
            // start of the image instead.
            inputObj->set("default_value", rdl2::Rgb(0.f, 0.f, 0.f));
            inputObj->set("warn_when_unavailable", false);
          }
          rdl2::SceneObject::UpdateGuard guard(mapObj);
          sampler->syncRdl2(*mapObj, binding.usage);
          if (inputObj)
            mapObj->setBinding("input_texture_coordinates", inputObj);
          else if (mapObj->getSceneClass().getName() == "ImageMap")
            mapObj->setBinding("input_texture_coordinates", nullptr);
        }
        bindings.emplace_back(binding.rdl2Attribute, mapObj);
      }

      // The same for a parameter that reads a *geometry* attribute: MoonRay
      // gets at primitive attributes through an AttributeMap, so one is
      // created per binding and bound to the material's attribute.
      for (const auto &binding : material->attributeBindings()) {
        if (!binding.rdl2Attribute || binding.primitiveAttribute.empty())
          continue;
        rdl2::SceneObject *mapObj = syncedObject("AttributeMap",
            objectName("attribute", "AttributeMap", material)
                + "/" + binding.primitiveAttribute,
            material->lastUpdated(),
            write);
        if (write) {
          rdl2::SceneObject::UpdateGuard guard(mapObj);
          // 0 == "primitive attribute": read the named attribute rather than
          // one of the shading-state quantities AttributeMap can also return.
          mapObj->set("map_type", 0);
          mapObj->set("primitive_attribute_name", binding.primitiveAttribute);
          mapObj->set(
              "primitive_attribute_type", binding.primitiveAttributeType);
          // A geometry that does not carry the attribute renders black rather
          // than AttributeMap's white default, which would look like a
          // working binding.
          mapObj->set("default_value", rdl2::Rgb(0.f, 0.f, 0.f));
          mapObj->set("warn_when_unavailable", true);
        }
        bindings.emplace_back(binding.rdl2Attribute, mapObj);
      }

      // A material whose bindings changed has itself changed - the binding is
      // written inside the material's own update - so the set of bound
      // attributes is part of what decides whether it must be rewritten.
      rdl2::Material *rdlMaterial = nullptr;
      rdl2::SceneObject *materialObj = nullptr;
      if (fragment) {
        rdlMaterial = syncFragment(material, *fragment);
        m_needPresenceShadows = m_needPresenceShadows
            || m_fragments[material->syncId()].usesPresence;
      } else {
        materialObj = syncedObject(materialClass,
            objectName("material", materialClass, material),
            materialVersion,
            write);
        if (write) {
          rdl2::SceneObject::UpdateGuard guard(materialObj);
          material->syncRdl2(*materialObj);
          // A parameter that had a sampler and now has a constant must lose
          // its map: every binding that is not wanted any more is dropped.
          const rdl2::SceneClass &sceneClass = materialObj->getSceneClass();
          for (auto it = sceneClass.beginAttributes();
              it != sceneClass.endAttributes();
              ++it) {
            const rdl2::Attribute *attribute = *it;
            if (!attribute->isBindable() || !materialObj->getBinding(*attribute))
              continue;
            const bool wanted = std::any_of(bindings.begin(),
                bindings.end(),
                [&](const auto &b) { return attribute->getName() == b.first; });
            if (!wanted)
              materialObj->setBinding(*attribute, nullptr);
          }
          for (const auto &b : bindings)
            materialObj->setBinding(b.first, b.second);
        }
        rdlMaterial = materialObj->asA<rdl2::Material>();
      }

      // --- an emitter the lights sample (Material::Emitter) ----------------
      // The surface gets the variant of its material which emits for camera
      // and mirror rays only: those are the rays the lights are not seen by.
      // An `rdla` material brings the variant along; for a material that is
      // one object it is that object for camera and mirror rays and a copy
      // without the emission for the others, behind a RaySwitchMaterial.
      Material::Emitter emitter;
      rdl2::SceneObject *emissionMap = nullptr;
      bool sampled = false;
      if (sampledSurfaces.count(surface) != 0) {
        emitter = material->emitter();
        if (fragment) {
          sampled = fragmentEmitter(material, emitter, rdlMaterial, emissionMap);
        } else if (emitter.rdl2Attribute) {
          const std::string baseName =
              objectName("material", materialClass, material);
          bool variantWrite = false;
          rdl2::SceneObject *darkObj = syncedObject(materialClass,
              baseName + "/noEmission",
              materialVersion,
              variantWrite);
          if (variantWrite) {
            rdl2::SceneObject::UpdateGuard guard(darkObj);
            material->syncRdl2(*darkObj);
            const rdl2::SceneClass &sceneClass = darkObj->getSceneClass();
            for (auto it = sceneClass.beginAttributes();
                it != sceneClass.endAttributes();
                ++it) {
              if ((*it)->isBindable() && darkObj->getBinding(**it))
                darkObj->setBinding(**it, nullptr);
            }
            for (const auto &b : bindings) {
              if (std::strcmp(b.first, emitter.rdl2Attribute) != 0)
                darkObj->setBinding(b.first, b.second);
            }
            darkObj->set(emitter.rdl2Attribute, rdl2::Rgb(0.f, 0.f, 0.f));
          }
          bool switchWrite = false;
          rdl2::SceneObject *switchObj = syncedObject("RaySwitchMaterial",
              baseName + "/sampled",
              materialVersion,
              switchWrite);
          if (switchWrite) {
            rdl2::SceneObject::UpdateGuard guard(switchObj);
            // Shadow rays take the default one: only its presence counts.
            setObject(switchObj, "default_material", darkObj);
            setObject(switchObj, "camera_ray_material", materialObj);
            setObject(switchObj, "indirect_mirror_ray_material", materialObj);
            setObject(switchObj, "indirect_glossy_ray_material", darkObj);
            setObject(switchObj, "indirect_diffuse_ray_material", darkObj);
          }
          for (const auto &b : bindings) {
            if (std::strcmp(b.first, emitter.rdl2Attribute) == 0)
              emissionMap = b.second;
          }
          holdMaterial(materialObj);
          holdMaterial(darkObj);
          rdlMaterial = switchObj->asA<rdl2::Material>();
          sampled = true;
        }
      }
      const auto addEmitter = [&](const Instance *instance) {
        if (sampled && (instance->visibility() & litBy) != 0) {
          emitterInstances.push_back({surface,
              instance,
              geometry,
              emitter,
              emissionMap,
              materialVersion});
        }
      };

      if (!instanced) {
        Instance *instance = instances.front();
        const std::string geometryName =
            objectName("geometry", geometryClass, instance, geometry);
        rdl2::SceneObject *geometryObj = syncedObject(geometryClass,
            geometryName,
            newest(geometry->lastUpdated(), instance->lastUpdated()),
            write);
        if (write) {
          writeGeometry(geometry, geometryObj, geometryName);
          appendObjectInfo({instance}, geometryObj, geometryName, false);
          rdl2::SceneObject::UpdateGuard guard(geometryObj);
          geometryObj->set("node_xform", toRdl2(instance->xfm()));
          // A mirroring transform turns the winding of the triangles around,
          // and the winding is what MoonRay takes the geometric normal from -
          // which then points the other way than the (correctly transformed)
          // vertex normals, and the surface renders black. An instancer
          // handles this itself.
          if (geometryObj->getSceneClass().getName() == "RdlMeshGeometry") {
            geometryObj->set("orientation",
                linalg::determinant(instance->xfm()) < 0.f ? 1 : 0);
          }
        }
        auto *rdlGeometry = geometryObj->asA<rdl2::Geometry>();
        renderGeometries.push_back(rdlGeometry);
        // One instance, one RDL2 geometry: the instance's ray visibility is
        // simply that geometry's.
        assignments.push_back({rdlGeometry,
            rdlMaterial,
            nullptr,
            false,
            geometryName,
            instance->visibility()});
        addEmitter(instance);
        continue;
      }

      // MoonRay takes the ray visibility of every instance from the
      // *instancer* geometry (rt::EmbreeAccelerator: "we use the visibility
      // flags of the instance geometry"), so instances of one group that
      // differ in visibility cannot share an instancer: there is one per
      // distinct visibility, all referencing the same prototype. Normally
      // that is a single one. Instances no ray sees get none.
      std::vector<std::pair<uint32_t, std::vector<Instance *>>> byVisibility;
      for (auto *instance : instances) {
        const uint32_t seenBy = instance->visibility();
        if (seenBy == visibility::None)
          continue;
        auto same = std::find_if(byVisibility.begin(),
            byVisibility.end(),
            [&](const auto &entry) { return entry.first == seenBy; });
        if (same == byVisibility.end())
          byVisibility.push_back({seenBy, {instance}});
        else
          same->second.push_back(instance);
      }
      if (byVisibility.empty())
        continue; // nothing to render; what was there is retired below

      // The instancer is decided first because the prototype has to follow it:
      // MoonRay regenerates a referenced geometry whenever its referrer
      // changes, but its *update* pass only carries the shader attribute set
      // for geometries whose own assignment changed. A prototype that is
      // regenerated without being told it needs updating is therefore rebuilt
      // without the primitive attributes its material asked for (MoonRay warns
      // "Geometry is not in the Layer" and the surface renders black -
      // anari.shared_group_attributes).
      //
      // Saying so costs one bool: rdl2::SceneObject::requestUpdate() exists
      // for exactly this ("changes external to the SceneObject can require
      // that the SceneObject be updated"). Rewriting the prototype's vertex
      // arrays says the same thing far more expensively - and, because RDL2
      // compares values before marking an attribute changed, it says it only
      // by accident.
      bool instancerWrite = false;
      std::vector<std::pair<rdl2::SceneObject *, bool>> instancers;
      for (const auto &entry : byVisibility) {
        // The instancer of the instances every ray sees keeps the plain
        // name. The others are told apart by their visibility - with a
        // separator that keeps them from being named *under* the plain one,
        // which retireObject() would take as belonging to it.
        std::string name =
            objectName("instancer", "RdlInstancerGeometry", group, surface);
        if (entry.first != visibility::All)
          name += "@visibility" + std::to_string(entry.first);
        bool thisWrite = false;
        rdl2::SceneObject *obj = syncedObject("RdlInstancerGeometry",
            name,
            newest(geometry->lastUpdated(), instancesVersion),
            thisWrite);
        instancers.push_back({obj, thisWrite});
        instancerWrite = instancerWrite || thisWrite;
      }

      // The prototype is keyed by the *surface*, not by the geometry: the
      // layer assigns one material per geometry, so two surfaces sharing an
      // ANARI geometry with different materials need one prototype each.
      const std::string prototypeName =
          objectName("prototype", geometryClass, surface);
      rdl2::SceneObject *prototypeObj = syncedObject(
          geometryClass, prototypeName, geometry->lastUpdated(), write);
      if (!write && instancerWrite) {
        // The data is unchanged; MoonRay only has to know that this geometry
        // is regenerated along with its referrer.
        prototypeObj->requestUpdate();
      }
      if (write) {
        writeGeometry(geometry, prototypeObj, prototypeName);
        rdl2::SceneObject::UpdateGuard guard(prototypeObj);
        // The instance transforms live on the instancer; the prototype is the
        // geometry in its own local space.
        prototypeObj->set("node_xform", rdl2::Mat4d(rdl2::Vec4d(1, 0, 0, 0),
                                            rdl2::Vec4d(0, 1, 0, 0),
                                            rdl2::Vec4d(0, 0, 1, 0),
                                            rdl2::Vec4d(0, 0, 0, 1)));
      }
      auto *rdlPrototype = prototypeObj->asA<rdl2::Geometry>();

      for (size_t i = 0; i < byVisibility.size(); ++i) {
        rdl2::SceneObject *instancerObj = instancers[i].first;
        if (instancers[i].second) {
          rdl2::Mat4dVector xforms;
          xforms.reserve(byVisibility[i].second.size());
          for (auto *instance : byVisibility[i].second)
            xforms.push_back(toRdl2(instance->xfm()));

          rdl2::SceneObjectVector references;
          references.push_back(rdlPrototype);

          rdl2::SceneObject::UpdateGuard guard(instancerObj);
          // 2 == "xform list": the per-instance transforms come from
          // xform_list rather than from positions/orientations/scales.
          instancerObj->set("method", 2);
          instancerObj->set("xform_list", xforms);
          // The prototype's node_xform is the identity above; not applying
          // it keeps the ANARI instance transform the only transform in play.
          instancerObj->set("use_reference_xforms", false);
          instancerObj->set(rdl2::Geometry::sReferenceGeometries, references);
          instancerObj->set(
              "primitive_attributes", rdl2::SceneObjectVector());
        }
        if (instancers[i].second) {
          appendObjectInfo(byVisibility[i].second,
              instancerObj,
              instancerObj->getName(),
              true);
        }
        auto *rdlInstancer = instancerObj->asA<rdl2::Geometry>();
        for (const Instance *instance : byVisibility[i].second)
          addEmitter(instance);

        renderGeometries.push_back(rdlInstancer);
        assignments.push_back({rdlInstancer,
            rdlMaterial,
            nullptr,
            false,
            instancerObj->getName(),
            byVisibility[i].first});
      }
      // The prototype itself stays visible to everything: what a ray may hit
      // is decided at the instancer it came through.
      assignments.push_back({rdlPrototype,
          rdlMaterial,
          nullptr,
          false,
          prototypeName,
          visibility::All});
    }

    // --- volumes ----------------------------------------------------------
    // A volume is a geometry (VdbGeometry, reading the file the volume baked)
    // plus a volume shader in the layer assignment instead of a material.
    // Instancing a volume would mean instancing that geometry, which MoonRay's
    // instancer does not do for volumes, so each instance gets its own.
    for (auto *instance : instances) {
      for (auto *volume : group->volumes()) {
        if (!volume || !volume->isValid())
          continue;
        auto *tf = static_cast<TransferFunction1D *>(volume);

        bool write = false;
        const char *geometryClass = tf->rdl2ClassName();
        rdl2::SceneObject *shaderObj = syncedObject(tf->volumeShaderClassName(),
            objectName("volumeShader", tf->volumeShaderClassName(), volume),
            volume->lastUpdated(),
            write);
        rdl2::SceneObject *colorMapObj = nullptr;
        if (tf->needsColorMap()) {
          bool mapWrite = false;
          colorMapObj = syncedObject(tf->colorMapClassName(),
              objectName("volumeColor", tf->colorMapClassName(), volume),
              volume->lastUpdated(),
              mapWrite);
          if (mapWrite) {
            rdl2::SceneObject::UpdateGuard guard(colorMapObj);
            tf->syncColorMap(*colorMapObj);
          }
        }
        if (write) {
          rdl2::SceneObject::UpdateGuard guard(shaderObj);
          tf->syncVolumeShader(*shaderObj);
          if (colorMapObj)
            shaderObj->setBinding("color_mult", colorMapObj);
        }

        rdl2::SceneObject *geometryObj = syncedObject(geometryClass,
            objectName("volume", geometryClass, instance, volume),
            newest(volume->lastUpdated(), instance->lastUpdated()),
            write);
        if (write) {
          rdl2::SceneObject::UpdateGuard guard(geometryObj);
          tf->syncRdl2(*geometryObj);
          geometryObj->set("node_xform", toRdl2(instance->xfm()));
        }

        auto *rdlGeometry = geometryObj->asA<rdl2::Geometry>();
        renderGeometries.push_back(rdlGeometry);
        // No material: a volume is shaded by its volume shader, which is what
        // MoonRay's own example scenes do too. That this works at all needed
        // an upstream fix - the release build dereferenced the null material
        // before the check written for it (docs/WINDOWS_PORT.md).
        assignments.push_back({rdlGeometry,
            nullptr,
            shaderObj->asA<rdl2::VolumeShader>(),
            false,
            geometryObj->getName(),
            instance->visibility()});
      }
    }
  }

  // --- the lights of the emitters -----------------------------------------
  // A MeshLight takes its shape from a geometry which is in no layer of the
  // scene (RenderContext::createMeshLightLayer() refuses one that is), and it
  // emits from the front of its faces only. The light of a surface therefore
  // sits on a mesh of its own: the triangles of every instance, once for each
  // sampled side, the back side with its winding turned around. One light
  // for all of them rather than one each, because a mesh light is not cheap -
  // every sampled direction is intersected with every one of them.
  //
  // The mesh of several instances is in world space. One instance keeps its
  // transform, so that the map of its emission finds the space of the object
  // where the surface has it; that is the only form when the map reads it.
  //
  // MoonRay loads such geometry through a layer of its own, "MeshLightLayer",
  // which it fills from the lights of the *first* frame only. The device
  // keeps that layer itself, the way MoonRay does: the geometry is assigned a
  // material named after it, whose albedo is bound to the light's map so
  // that the geometry is built with the primitive attributes the map reads.
  m_meshLights.clear();
  for (auto &entry : m_emitterLights)
    entry.second.live = false;
  if (!emitterInstances.empty()) {
    struct LightInput
    {
      const Surface *surface;
      uint32_t seenBy;
      // Which of the lights of the surface for these rays, when every
      // instance is one; -1 for the light of all instances.
      int ordinal;
      std::vector<const EmitterInstance *> instances;
    };
    std::vector<LightInput> inputs;
    for (const EmitterInstance &e : emitterInstances) {
      const uint32_t seenBy = e.instance->visibility() & litBy;
      const bool perInstance = sampledSurfaces[e.surface];
      int ordinal = perInstance ? 0 : -1;
      LightInput *same = nullptr;
      for (LightInput &input : inputs) {
        if (input.surface != e.surface || input.seenBy != seenBy)
          continue;
        if (perInstance)
          ++ordinal;
        else
          same = &input;
      }
      if (same)
        same->instances.push_back(&e);
      else
        inputs.push_back({e.surface, seenBy, ordinal, {&e}});
    }

    auto *meshLightLayer =
        createObject("Layer", "MeshLightLayer")->asA<rdl2::Layer>();
    auto *meshLightSet =
        createObject("LightSet", "MeshLightLightSet")->asA<rdl2::LightSet>();
    for (const LightInput &input : inputs) {
      const EmitterInstance &first = *input.instances.front();
      const Geometry *geometry = first.geometry;
      const Material::Emitter &emitter = first.emitter;
      // By the surface and not by the instance: an application which makes
      // its instances anew for every edit gets the same objects again.
      std::string suffix = "@" + std::to_string(input.seenBy);
      if (input.ordinal >= 0)
        suffix += "#" + std::to_string(input.ordinal);
      const std::string geometryName =
          objectName("emitter", "RdlMeshGeometry", input.surface) + suffix;
      const std::string lightName =
          objectName("meshLight", "MeshLight", input.surface) + suffix;

      helium::TimeStamp geometryVersion = geometry->lastUpdated();
      std::vector<uint64_t> signature;
      signature.push_back(uint64_t(emitter.sides));
      for (const EmitterInstance *e : input.instances) {
        geometryVersion = newest(geometryVersion, e->instance->lastUpdated());
        signature.push_back(e->instance->syncId());
      }
      EmitterLight &slot = m_emitterLights[geometryName];
      const bool isNew = slot.lightName.empty();
      const bool sameInstances = slot.signature == signature;
      slot.lightName = lightName;
      slot.signature = std::move(signature);
      slot.live = true;

      bool geometryWrite = false;
      rdl2::SceneObject *geometryObj = syncedObject("RdlMeshGeometry",
          geometryName,
          geometryVersion,
          geometryWrite,
          /*force=*/!sameInstances);
      if (geometryWrite) {
        // The geometry as it is, for its texture coordinates and its other
        // primitive attributes; then the vertices and faces of all copies.
        writeGeometry(geometry, geometryObj, geometryName);
        const bool local = input.instances.size() == 1;
        if (local) {
          appendObjectInfo({const_cast<Instance *>(first.instance)},
              geometryObj,
              geometryName,
              false);
        }
        const rdl2::IntVector baseIndices =
            geometryObj->get<rdl2::IntVector>("vertices_by_index");
        const size_t numVertices =
            geometryObj->get<rdl2::Vec3fVector>("vertex_list_0").size();
        // What an earlier write of the copies left there has another size.
        rdl2::Vec2fVector baseUvs =
            geometryObj->get<rdl2::Vec2fVector>("uv_list");
        if (baseUvs.size() != numVertices)
          baseUvs.clear();

        rdl2::Vec3fVector vertices;
        rdl2::IntVector indices;
        rdl2::Vec2fVector uvs;
        size_t copies = 0;
        std::vector<float> moved;
        for (const EmitterInstance *e : input.instances) {
          const mat4 &xfm = e->instance->xfm();
          const bool mirrored = linalg::determinant(xfm) < 0.f;
          // Each copy is moved off the surface by a hair, to the side it
          // emits to. On the surface itself, a ray which leaves the surface
          // to one side starts within rounding of the copy of the other
          // side, and half of such rays would hit that copy from the front
          // at distance zero: the surface would reflect its own emission.
          // Moved, each copy is behind the rays which leave to the other
          // side, and those which leave to its own side meet its back, which
          // emits nothing. The distance is far below what could be seen, and
          // above the rounding of positions as far out as the object is.
          const float radius = geometry->boundingRadius();
          const float scale =
              std::cbrt(std::fabs(float(linalg::determinant(xfm))));
          const float reach = std::fabs(xfm[3].x) + std::fabs(xfm[3].y)
              + std::fabs(xfm[3].z) + scale * radius;
          const float distance = 1e-4f * radius
              + (scale > 0.f ? 4e-6f * reach / scale : 0.f);
          for (int side = 0; side < 2; ++side) {
            if ((emitter.sides & (1 << side)) == 0)
              continue;
            if (!geometry->displacedPositions(
                    side == 0 ? distance : -distance, moved)
                || moved.size() != 3 * numVertices) {
              continue;
            }
            for (size_t i = 0; i < numVertices; ++i) {
              if (local) {
                vertices.emplace_back(
                    moved[3 * i], moved[3 * i + 1], moved[3 * i + 2]);
              } else {
                const float4 p = xfm[0] * moved[3 * i]
                    + xfm[1] * moved[3 * i + 1] + xfm[2] * moved[3 * i + 2]
                    + xfm[3];
                vertices.emplace_back(p.x, p.y, p.z);
              }
            }
            // The front of a face is where its winding says, in world space:
            // the other way around for the back side, and for a transform
            // which mirrors.
            const int offset = int(copies * numVertices);
            const bool reversed = (side == 1) != mirrored;
            for (size_t i = 0; i + 2 < baseIndices.size(); i += 3) {
              indices.push_back(offset + baseIndices[i]);
              indices.push_back(offset + baseIndices[i + (reversed ? 2 : 1)]);
              indices.push_back(offset + baseIndices[i + (reversed ? 1 : 2)]);
            }
            uvs.insert(uvs.end(), baseUvs.begin(), baseUvs.end());
            ++copies;
          }
        }

        {
          rdl2::SceneObject::UpdateGuard guard(geometryObj);
          geometryObj->set("vertex_list_0", vertices);
          geometryObj->set("vertices_by_index", indices);
          geometryObj->set(
              "face_vertex_count", rdl2::IntVector(indices.size() / 3, 3));
          geometryObj->set("uv_list", uvs);
          // The light has no use for shading normals.
          geometryObj->set("normal_list", rdl2::Vec3fVector());
          geometryObj->set("node_xform",
              local ? toRdl2(first.instance->xfm())
                    : rdl2::Mat4d(rdl2::Vec4d(1, 0, 0, 0),
                          rdl2::Vec4d(0, 1, 0, 0),
                          rdl2::Vec4d(0, 0, 1, 0),
                          rdl2::Vec4d(0, 0, 0, 1)));
          geometryObj->set("orientation", 0);
        }
        // The per-vertex attributes, once for each copy.
        const auto repeat = [&](rdl2::SceneObject *data, const char *name,
                                auto values) {
          if (values.size() != numVertices || copies < 2)
            return;
          const auto once = values;
          for (size_t copy = 1; copy < copies; ++copy)
            values.insert(values.end(), once.begin(), once.end());
          rdl2::SceneObject::UpdateGuard guard(data);
          data->set(name, values);
        };
        for (rdl2::SceneObject *data :
            geometryObj->get<rdl2::SceneObjectVector>("primitive_attributes")) {
          if (!data || data->get<rdl2::Int>("rate") != 4)
            continue;
          repeat(data, "float_values_0",
              data->get<rdl2::FloatVector>("float_values_0"));
          repeat(data, "vec2f_values_0",
              data->get<rdl2::Vec2fVector>("vec2f_values_0"));
          repeat(data, "vec3f_values_0",
              data->get<rdl2::Vec3fVector>("vec3f_values_0"));
          repeat(data, "color_values_0",
              data->get<rdl2::RgbVector>("color_values_0"));
        }
      }

      // The name is the one MoonRay looks the material up by.
      rdl2::SceneObject *holderObj =
          createObject("DwaBaseMaterial", geometryName + "_MeshLightMaterial");
      const rdl2::Attribute *albedo =
          holderObj->getSceneClass().getAttribute("albedo");
      if (holderObj->getBinding(*albedo) != first.emissionMap) {
        rdl2::SceneObject::UpdateGuard guard(holderObj);
        holderObj->setBinding("albedo", first.emissionMap);
      }
      if (isNew) {
        rdl2::SceneObject::UpdateGuard guard(meshLightLayer);
        meshLightLayer->assign(geometryObj->asA<rdl2::Geometry>(),
            "",
            holderObj->asA<rdl2::Material>(),
            meshLightSet,
            /*displacement=*/nullptr,
            /*volumeShader=*/nullptr);
      }

      bool lightWrite = false;
      rdl2::SceneObject *lightObj = syncedObject("MeshLight",
          lightName,
          newest(geometryVersion, first.materialVersion),
          lightWrite,
          /*force=*/geometryWrite);
      if (lightWrite) {
        const float strength = std::max(
            emitter.color.x, std::max(emitter.color.y, emitter.color.z));
        rdl2::SceneObject::UpdateGuard guard(lightObj);
        setObject(lightObj, "geometry", geometryObj);
        setObject(lightObj, "map_shader", first.emissionMap);
        // normalized = false makes color * intensity the radiance.
        lightObj->set("normalized", false);
        lightObj->set("color",
            strength > 0.f ? rdl2::Rgb(emitter.color.x / strength,
                                 emitter.color.y / strength,
                                 emitter.color.z / strength)
                           : rdl2::Rgb(0.f, 0.f, 0.f));
        lightObj->set("intensity", strength);
        lightObj->set("exposure", 0.f);
        // The camera and the mirrors see the surface itself.
        lightObj->set("visible_in_camera", 0);
        lightObj->set("visible_mirror_reflection", false);
        lightObj->set("visible_mirror_transmission", false);
        lightObj->set("visible_diffuse_reflection",
            (input.seenBy & visibility::DiffuseReflection) != 0);
        lightObj->set("visible_diffuse_transmission",
            (input.seenBy & visibility::DiffuseTransmission) != 0);
        lightObj->set("visible_glossy_reflection",
            (input.seenBy & visibility::GlossyReflection) != 0);
        lightObj->set("visible_glossy_transmission",
            (input.seenBy & visibility::GlossyTransmission) != 0);
      }
      m_meshLights.push_back(lightObj->asA<rdl2::Light>());
    }
  }
  // An emitter that is gone gives its mesh back. Its assignment in the mesh
  // light layer stays, like one of the scene's layer.
  for (auto &entry : m_emitterLights) {
    if (!entry.second.live && m_objects.count(entry.first) != 0) {
      retireObject(entry.first);
      retireObject(entry.second.lightName);
      entry.second.signature.clear();
    }
  }
  // MoonRay only tessellates geometry which is in a geometry set - and a mesh
  // light with a map reads the texture coordinates and the other primitive
  // attributes of the tessellated mesh. The meshes of the lights get a set of
  // their own: the one of the scene is what the layer's geometry is.
  if (!m_emitterLights.empty()) {
    std::vector<rdl2::Geometry *> copies;
    for (const auto &entry : m_emitterLights) {
      if (entry.second.live)
        copies.push_back(m_objects[entry.first].object->asA<rdl2::Geometry>());
    }
    auto *emitterSet =
        createObject("GeometrySet", "/anari/emitterGeometrySet")
            ->asA<rdl2::GeometrySet>();
    const auto &current = emitterSet->getGeometries();
    bool same = current.size() == copies.size();
    for (size_t i = 0; same && i < copies.size(); ++i)
      same = emitterSet->contains(copies[i]);
    if (!same) {
      rdl2::SceneObject::UpdateGuard guard(emitterSet);
      emitterSet->clear();
      for (auto *copy : copies)
        emitterSet->add(copy);
    }
  }
  if (std::getenv("OPENMOONRAY_ANARI_DEBUG_LAYER")) {
    std::fprintf(stderr,
        "[moonray] sync: %zu mesh light(s) for %zu emitter instance(s), %d "
        "over the limit of %d, %zu emitter geometr(ies) ever\n",
        m_meshLights.size(),
        emitterInstances.size(),
        m_meshLightsOverLimit,
        m_maxMeshLights,
        m_emitterLights.size());
  }

  // --- the layer never shrinks --------------------------------------------
  // RDL2 cannot delete a SceneObject, so the geometry of a scene the
  // application has moved on from stays in the SceneContext - and MoonRay
  // keeps the primitives it built from it. Dropping its layer assignment
  // leaves those primitives with a baked assignment ID past the end of the
  // layer, and the first ray that hits one makes scene_rdl2 throw IndexError
  // on a render thread: std::terminate, and the process is gone without a
  // message. (Found by running the ANARI CTS, which drives every one of its
  // test scenes through a single device; the minimal case is a world that
  // becomes empty - anari.scene_churn.)
  //
  // So an assignment, once made, is kept for the life of the device, and a
  // geometry that has left the scene is turned invisible to every ray type
  // instead. It costs one slot and its BVH memory; it does not cost the
  // application its process.
  for (const auto &a : assignments) {
    auto it = std::find_if(m_layerAssignments.begin(),
        m_layerAssignments.end(),
        [&](const LayerAssignment &s) { return s.geometry == a.geometry; });
    if (it == m_layerAssignments.end()) {
      m_layerAssignments.push_back(a);
    } else {
      // The same geometry, possibly with a different shader or visibility.
      // Whether the slot is retired and what its RDL2 object currently holds
      // is the slot's own knowledge and stays.
      it->material = a.material;
      it->volumeShader = a.volumeShader;
      it->name = a.name;
      it->visibility = a.visibility;
    }
  }

  for (auto &slot : m_layerAssignments) {
    const bool live = std::any_of(assignments.begin(),
        assignments.end(),
        [&](const LayerAssignment &a) { return a.geometry == slot.geometry; });

    if (live == slot.retired) {
      slot.retired = !live;

      // A retired geometry gives its data back: RDL2 cannot delete the
      // object, but resetting it to its defaults releases the vertex arrays
      // it is holding (and the UserData objects named after it, which carry
      // the primitive attributes). Dropping the cache entries with them is
      // what makes this reversible - a geometry that comes back is then
      // written from scratch instead of being trusted to still hold its data.
      if (!live) {
        retireObject(slot.name);
        // The reset made it visible to everything again.
        slot.appliedVisibility = visibility::All;
      }
    }

    // In the scene the geometry is seen by the rays its instance says; out of
    // it, by none. Only a difference is written: MoonRay rebuilds a geometry
    // whose visibility attributes changed.
    const uint32_t wanted = live ? slot.visibility : visibility::None;
    if (slot.appliedVisibility == wanted)
      continue;
    slot.appliedVisibility = wanted;

    // The keys rather than the attribute names: these are declared on RDL2's
    // Geometry base class, so every geometry has them, and the names are not
    // always what the key is called (sVisiblePhase is "visible_volume").
    rdl2::SceneObject::UpdateGuard guard(slot.geometry);
    slot.geometry->set(
        rdl2::Geometry::sVisibleCamera, (wanted & visibility::Camera) != 0);
    slot.geometry->set(
        rdl2::Geometry::sVisibleShadow, (wanted & visibility::Shadow) != 0);
    slot.geometry->set(rdl2::Geometry::sVisibleDiffuseReflection,
        (wanted & visibility::DiffuseReflection) != 0);
    slot.geometry->set(rdl2::Geometry::sVisibleDiffuseTransmission,
        (wanted & visibility::DiffuseTransmission) != 0);
    slot.geometry->set(rdl2::Geometry::sVisibleGlossyReflection,
        (wanted & visibility::GlossyReflection) != 0);
    slot.geometry->set(rdl2::Geometry::sVisibleGlossyTransmission,
        (wanted & visibility::GlossyTransmission) != 0);
    slot.geometry->set(rdl2::Geometry::sVisibleMirrorReflection,
        (wanted & visibility::MirrorReflection) != 0);
    slot.geometry->set(rdl2::Geometry::sVisibleMirrorTransmission,
        (wanted & visibility::MirrorTransmission) != 0);
    slot.geometry->set(
        rdl2::Geometry::sVisiblePhase, (wanted & visibility::Phase) != 0);
  }

  // --- containers ---------------------------------------------------------
  // Rebuilding the layer is not free: MoonRay reloads the geometry whose
  // assignments changed. Since the containers only carry *membership*, they
  // are rewritten only when that membership actually differs.
  std::vector<const void *> signature;
  signature.reserve(
      3 * m_layerAssignments.size() + renderGeometries.size() + 1);
  for (auto *g : renderGeometries)
    signature.push_back(g);
  signature.push_back(nullptr); // separates the two lists
  for (const auto &a : m_layerAssignments) {
    signature.push_back(a.geometry);
    signature.push_back(a.material);
    signature.push_back(a.volumeShader);
  }
  signature.push_back(lightSet);

  m_layerRebuilt = signature != m_containerSignature;

  if (std::getenv("OPENMOONRAY_ANARI_DEBUG_LAYER")) {
    size_t retired = 0;
    for (const auto &slot : m_layerAssignments)
      retired += slot.retired ? 1 : 0;
    std::fprintf(stderr,
        "[moonray] sync: %zu live assignment(s), %zu slot(s) (%zu retired), "
        "%zu render geometr(ies), layer %s\n",
        assignments.size(),
        m_layerAssignments.size(),
        retired,
        renderGeometries.size(),
        m_layerRebuilt ? "rebuilt" : "unchanged");
  }

  if (!m_layerRebuilt)
    return;

  m_containerSignature = std::move(signature);

  {
    rdl2::SceneObject::UpdateGuard guard(geometrySet);
    geometrySet->clear();
    for (auto *g : renderGeometries)
      geometrySet->add(g);
  }

  {
    rdl2::SceneObject::UpdateGuard guard(layer);
    layer->clear();
    for (const auto &a : m_layerAssignments) {
      layer->assign(a.geometry,
          "",
          a.material,
          lightSet,
          /*displacement=*/nullptr,
          a.volumeShader);
    }
  }
}

void MoonrayScene::syncScene(World *world,
    Camera *camera,
    Renderer *renderer,
    uint2 size,
    bool wantsDepth,
    bool accumulate)
{
  rdl2::SceneContext &sc = m_ctx->getSceneContext();

  m_objectsUpdated = 0;
  m_objectsUnchanged = 0;

  const float imageAspect =
      size.y > 0 ? static_cast<float>(size.x) / static_cast<float>(size.y) : 1.f;

  // --- camera -------------------------------------------------------------
  // The frame size feeds the camera's aspect handling, so a resized frame has
  // to rewrite the camera even when the ANARI camera object itself did not
  // change. Forcing the version rather than folding the size into it keeps
  // "version" meaning exactly "the ANARI state this was written from".
  const bool sizeChanged = size.x != m_lastSize.x || size.y != m_lastSize.y;
  m_lastSize = size;

  const char *cameraClass = camera->rdl2ClassName();
  bool write = false;
  rdl2::SceneObject *cameraObj = syncedObject(cameraClass,
      objectName("camera", cameraClass, camera),
      camera->lastUpdated(),
      write,
      /*force=*/sizeChanged);
  if (write) {
    rdl2::SceneObject::UpdateGuard guard(cameraObj);
    camera->syncRdl2(*cameraObj, imageAspect);
  }

  // --- lights -------------------------------------------------------------
  std::vector<rdl2::Light *> lights;

  // An hdri light *is* the environment, and ambient light is the uniform
  // stand-in for one when there is none: with both, every surface - and
  // every reflection - would see the sum, and a mirror would reflect a
  // white-washed environment instead of the hdri (pynari reference-hdri.py).
  // So, as in barney, the ambient term only applies without an hdri light.
  bool hasEnvironmentLight = false;
  for (auto *instance : world->instances()) {
    if (!instance || !instance->isValid() || !instance->group())
      continue;
    for (auto *light : instance->group()->lights()) {
      if (light && light->isValid() && dynamic_cast<HDRI *>(light))
        hasEnvironmentLight = true;
    }
  }

  if (renderer->ambientRadiance() > 0.f && !hasEnvironmentLight) {
    // A uniform environment light is exactly what an ambient term is: MoonRay
    // treats an EnvLight's color as radiance directly.
    rdl2::SceneObject *envObj =
        syncedObject("EnvLight",
            objectName("ambient", "EnvLight", renderer),
            renderer->lastUpdated(),
            write);
    if (write) {
      rdl2::SceneObject::UpdateGuard guard(envObj);
      const float3 c = renderer->ambientColor();
      envObj->set("color", rdl2::Rgb(c.x, c.y, c.z));
      envObj->set("intensity", renderer->ambientRadiance());
      envObj->set("exposure", 0.f);
      envObj->set("visible_in_camera", 0);
    }
    lights.push_back(envObj->asA<rdl2::Light>());
  }

  for (auto *instance : world->instances()) {
    if (!instance || !instance->isValid())
      continue;
    Group *group = instance->group();
    if (!group)
      continue;

    for (auto *light : group->lights()) {
      if (!light || !light->isValid())
        continue;
      const char *lightClass = light->rdl2ClassName();
      rdl2::SceneObject *lightObj = syncedObject(lightClass,
          objectName("light", lightClass, instance, light),
          newest(light->lastUpdated(), instance->lastUpdated()),
          write);
      if (write) {
        // An emission which changes with the distance is a filter of the
        // light: a ramp over the distance between the point of the light
        // which emits and the point which is lit, which is what MoonRay's
        // radial ColorRampLightFilter takes when it has no transform of its
        // own. Not for a light at infinity.
        rdl2::SceneObjectVector filters;
        const std::vector<float> &distances = light->falloffDistances();
        const bool local = std::strcmp(lightClass, "DistantLight") != 0
            && std::strcmp(lightClass, "EnvLight") != 0;
        if (local && distances.size() >= 2 && distances.back() > 0.f) {
          rdl2::SceneObject *filterObj = createObject("ColorRampLightFilter",
              objectName("lightFalloff", "ColorRampLightFilter", instance, light));
          // A ramp of MoonRay has ten control points at most. Of a longer
          // table the points go which a line between their neighbours
          // misses least, one after the other.
          std::vector<float> kept = distances;
          std::vector<float3> colors = light->falloffColors();
          while (kept.size() > 10) {
            size_t least = 1;
            float leastError = std::numeric_limits<float>::max();
            for (size_t i = 1; i + 1 < kept.size(); ++i) {
              const float t =
                  (kept[i] - kept[i - 1]) / (kept[i + 1] - kept[i - 1]);
              const float3 line = colors[i - 1] + (colors[i + 1] - colors[i - 1]) * t;
              const float3 miss = linalg::abs(colors[i] - line);
              const float error = std::max(miss.x, std::max(miss.y, miss.z));
              if (error < leastError) {
                leastError = error;
                least = i;
              }
            }
            kept.erase(kept.begin() + least);
            colors.erase(colors.begin() + least);
          }
          // The filter has its distances as fractions of a range.
          const float end = kept.back();
          rdl2::FloatVector positions;
          rdl2::RgbVector values;
          for (size_t i = 0; i < kept.size(); ++i) {
            positions.push_back(kept[i] / end);
            values.emplace_back(colors[i].x, colors[i].y, colors[i].z);
          }
          if (std::getenv("OPENMOONRAY_ANARI_DEBUG_LAYER")) {
            std::fprintf(stderr, "[moonray] sync: falloff of %s:", lightClass);
            for (size_t i = 0; i < kept.size(); ++i)
              std::fprintf(stderr, " %g:%g", kept[i], colors[i].x);
            std::fprintf(stderr, "\n");
          }
          rdl2::SceneObject::UpdateGuard guard(filterObj);
          filterObj->set("mode", 0); // radial
          filterObj->set("use_xform", false);
          filterObj->set("begin_distance", 0.f);
          filterObj->set("end_distance", end);
          filterObj->set("distances", positions);
          filterObj->set("colors", values);
          filterObj->set("interpolation_types",
              rdl2::IntVector(positions.size(), 1)); // linear
          filterObj->set("intensity", 1.f);
          filterObj->set("density", 1.f);
          filters.push_back(filterObj);
        }
        rdl2::SceneObject::UpdateGuard guard(lightObj);
        light->syncRdl2(*lightObj, instance->xfm());
        lightObj->set("light_filters", filters);
      }
      lights.push_back(lightObj->asA<rdl2::Light>());
    }
  }

  rdl2::LightSet *lightSet =
      createObject("LightSet", "/anari/lightSet")->asA<rdl2::LightSet>();

  // --- surfaces: geometry + material + layer assignment -------------------
  rdl2::SceneObject *geometrySet =
      createObject("GeometrySet", "/anari/geometrySet");
  rdl2::SceneObject *layer = createObject("Layer", "/anari/layer");

  m_maxMeshLights = renderer->maxMeshLights() >= 0 ? renderer->maxMeshLights()
                                                   : m_deviceMaxMeshLights;
  m_maxMeshLightTriangles = size_t(renderer->maxMeshLightTriangles());
  m_minMeshLightPowerShare = renderer->minMeshLightPowerShare();
  syncSurfaces(world, lightSet, geometrySet, layer);

  // The lights of the emitting surfaces, which the surfaces brought along.
  lights.insert(lights.end(), m_meshLights.begin(), m_meshLights.end());

  bool lightsChanged = false;
  {
    // The LightSet is membership only, and a light set that already holds
    // exactly these lights does not need rewriting.
    bool sameLights = lightSet->getLights().size() == lights.size();
    if (sameLights) {
      for (auto *l : lights) {
        if (!lightSet->contains(l)) {
          sameLights = false;
          break;
        }
      }
    }
    if (!sameLights) {
      lightsChanged = true;
      rdl2::SceneObject::UpdateGuard guard(lightSet);
      lightSet->clear();
      for (auto *l : lights)
        lightSet->add(l);
    }
  }

  // --- depth render output ------------------------------------------------
  // MoonRay produces anything beyond beauty through RenderOutput objects, and
  // rebuilds its RenderOutputDriver in every renderPrep, so the channel can
  // come and go between frames. `active` is what turns it off without
  // removing the object.
  {
    // The version is synthetic (the request is a bool, not an ANARI object),
    // which is enough to write the output once and again whenever the
    // channel is switched on or off. Note RenderOutput's `active` defaults to
    // *true*, so "configure it only when active changes" would never write
    // the result type at all.
    bool writeDepthOutput = false;
    rdl2::SceneObject *depthOutput = syncedObject("RenderOutput",
        "/anari/renderOutput/depth",
        helium::TimeStamp(wantsDepth ? 1 : 2),
        writeDepthOutput);
    if (writeDepthOutput) {
      rdl2::SceneObject::UpdateGuard guard(depthOutput);
      depthOutput->set("active", wantsDepth);
      // 2 == RESULT_DEPTH (scene_rdl2 RenderOutput.h).
      depthOutput->set("result", 2);
      depthOutput->set("channel_format", 1); // full float, not half
      depthOutput->set("file_name", std::string("anari_depth.exr"));
    }
  }

  // --- noise of a repeated render -----------------------------------------
  // Nothing the image depends on was written: the application is rendering
  // the same scene again. Without accumulation that reproduces the previous
  // image exactly. When accumulating, MoonRay's sampling - deterministic per
  // frame number - has to move on, so the frame number advances and the new
  // frame carries new samples instead of a copy of the last one. (Renderer
  // parameters - pixelSamples, the sample splitting - change the render
  // without writing an object; they only change the noise, so they do not
  // need to be told apart from a repeat.)
  // --- presence shadows ---------------------------------------------------
  // A light reads the scene's enable_presence_shadows when it is updated, so
  // switching it means telling the lights.
  bool integratorChanged = false;
  if (m_needPresenceShadows != m_presenceShadows) {
    m_presenceShadows = m_needPresenceShadows;
    for (auto *l : lights)
      l->requestUpdate();
    integratorChanged = true;
  }
  if (!(renderer->integrator() == m_integrator)) {
    m_integrator = renderer->integrator();
    integratorChanged = true;
  }

  m_sceneChanged = !m_hasRendered || m_objectsUpdated > 0 || m_layerRebuilt
      || lightsChanged || integratorChanged || cameraObj != m_lastCamera;
  m_lastCamera = cameraObj;
  m_hasRendered = true;
  if (accumulate && !m_sceneChanged)
    m_frameNumber += 1.f;

  // --- scene variables ----------------------------------------------------
  rdl2::SceneVariables &vars = sc.getSceneVariables();
  {
    rdl2::SceneObject::UpdateGuard guard(&vars);
    vars.set(rdl2::SceneVariables::sImageWidth, static_cast<int>(size.x));
    vars.set(rdl2::SceneVariables::sImageHeight, static_cast<int>(size.y));
    vars.set(
        rdl2::SceneVariables::sPixelSamplesSqrt, renderer->pixelSamplesSqrt());
    // 0 == SamplingMode::UNIFORM: a batch render of a fixed sample count,
    // not adaptive sampling (docs/adr/0002).
    vars.set(rdl2::SceneVariables::sSamplingMode, 0);
    // Per-hit sample splitting (Renderer::commitParameters()). A volume
    // scattering event draws volume_illumination_samples light samples - not
    // a per-axis count, and 4 by default, i.e. light_samples = 2 squared - so
    // it follows the light samples.
    const int lightSamples = renderer->lightSamples();
    vars.set(rdl2::SceneVariables::sLightSamplesSqrt, lightSamples);
    vars.set(rdl2::SceneVariables::sBsdfSamplesSqrt, renderer->bsdfSamples());
    vars.set(rdl2::SceneVariables::sVolumeIlluminationSamples,
        lightSamples * lightSamples);
    // Path depths and sample clamping (Renderer::commitParameters()), and
    // whether shadow rays evaluate the presence of what they pass.
    vars.set(rdl2::SceneVariables::sMaxDepth, m_integrator.maxDepth);
    vars.set(
        rdl2::SceneVariables::sMaxDiffuseDepth, m_integrator.maxDiffuseDepth);
    vars.set(
        rdl2::SceneVariables::sMaxGlossyDepth, m_integrator.maxGlossyDepth);
    vars.set(
        rdl2::SceneVariables::sMaxMirrorDepth, m_integrator.maxMirrorDepth);
    vars.set(
        rdl2::SceneVariables::sMaxVolumeDepth, m_integrator.maxVolumeDepth);
    vars.set(rdl2::SceneVariables::sMaxPresenceDepth,
        m_integrator.maxPresenceDepth);
    vars.set(rdl2::SceneVariables::sSampleClampingValue,
        m_integrator.sampleClampingValue);
    vars.set(rdl2::SceneVariables::sSampleClampingDepth,
        m_integrator.sampleClampingDepth);
    vars.set(rdl2::SceneVariables::sEnablePresenceShadows, m_presenceShadows);
    // No ANARI object in this milestone carries motion samples, and shutter
    // time sampling would only add noise.
    vars.set(rdl2::SceneVariables::sEnableMotionBlur, false);
    vars.set(rdl2::SceneVariables::sCamera, cameraObj);
    vars.set(rdl2::SceneVariables::sFrameKey, m_frameNumber);
    vars.set(rdl2::SceneVariables::sLayer, layer);
  }
}

namespace {

// Reads MoonRay's depth render output for the frame just rendered. A free
// function because `RenderBuffer` is a typedef of a template instantiation
// and cannot be forward declared in the header.
void snapshotDepthOutput(const moonray::rndr::RenderContext &ctx,
    const scene_rdl2::fb_util::RenderBuffer &beauty,
    uint2 size,
    std::vector<float> &depth,
    std::string &error)
{
  // The render output driver is rebuilt in every renderPrep, so the index of
  // our depth output is looked up per frame rather than remembered.
  const auto *driver = ctx.getRenderOutputDriver();
  if (!driver) {
    error = "MoonRay produced no render output driver, so there is no depth";
    return;
  }

  const rdl2::SceneObject *wanted =
      ctx.getSceneContext().getSceneObject("/anari/renderOutput/depth");
  int index = -1;
  for (int i = 0; i < driver->getNumberOfRenderOutputs(); ++i) {
    if (driver->getRenderOutput(i) == wanted) {
      index = i;
      break;
    }
  }
  if (index < 0) {
    error = "the depth render output is missing from MoonRay's output driver";
    return;
  }


  scene_rdl2::fb_util::VariablePixelBuffer buffer;
  ctx.snapshotRenderOutput(&buffer,
      index,
      &beauty,
      &beauty,
      /*heatMap=*/nullptr,
      /*weight=*/nullptr,
      /*renderBufferOdd=*/nullptr,
      /*untile=*/true,
      /*parallel=*/true);

  if (buffer.getWidth() != size.x || buffer.getHeight() != size.y) {
    error = "MoonRay's depth output has the wrong size";
    return;
  }

  // The depth AOV is one value per pixel, but MoonRay hands the render
  // output back in whatever width its AOV buffer was allocated with, so the
  // first channel is read out of whichever float format arrives.
  const size_t numPixels = size_t(size.x) * size_t(size.y);
  using VPB = scene_rdl2::fb_util::VariablePixelBuffer;
  switch (buffer.getFormat()) {
  case VPB::FLOAT: {
    const float *values = buffer.getFloatBuffer().getData();
    depth.assign(values, values + numPixels);
    break;
  }
  case VPB::FLOAT2: {
    const auto *values = buffer.getFloat2Buffer().getData();
    depth.resize(numPixels);
    for (size_t i = 0; i < numPixels; ++i)
      depth[i] = values[i].x;
    break;
  }
  case VPB::FLOAT3: {
    const auto *values = buffer.getFloat3Buffer().getData();
    depth.resize(numPixels);
    for (size_t i = 0; i < numPixels; ++i)
      depth[i] = values[i].x;
    break;
  }
  case VPB::FLOAT4: {
    const auto *values = buffer.getFloat4Buffer().getData();
    depth.resize(numPixels);
    for (size_t i = 0; i < numPixels; ++i)
      depth[i] = values[i].x;
    break;
  }
  default:
    error = "MoonRay's depth output is not a float buffer (format "
        + std::to_string(int(buffer.getFormat())) + ")";
    return;
  }
}

} // namespace

bool MoonrayScene::render(World *world,
    Camera *camera,
    Renderer *renderer,
    uint2 size,
    std::vector<float4> &color,
    std::vector<float> *depth,
    bool accumulate,
    bool &sceneChanged,
    std::string &error)
{
  if (!m_backend) {
    error = m_error;
    return false;
  }

  error.clear();
  sceneChanged = true;
  color.assign(size_t(size.x) * size_t(size.y), float4(0.f, 0.f, 0.f, 0.f));
  if (depth)
    depth->assign(size_t(size.x) * size_t(size.y), 0.f);

  m_backend->execute([&]() {
    try {
      ensureContext();
      {
        const auto syncStart = std::chrono::steady_clock::now();
        syncScene(world, camera, renderer, size, depth != nullptr, accumulate);
        sceneChanged = m_sceneChanged;
        m_syncMilliseconds = std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - syncStart)
                                 .count();
      }

      // A debugging aid, off unless asked for: writing the translated scene
      // out means it can be rendered with `moonray -in`, which is how a
      // problem gets pinned on the translation or on MoonRay itself.
      if (const char *dump = std::getenv("OPENMOONRAY_ANARI_DUMP_RDLA")) {
        scene_rdl2::rdl2::writeSceneToFile(
            m_ctx->getSceneContext(), dump, /*deltaEncoding=*/false, true);
      }

      m_ctx->setSceneUpdated();
      if (!m_ctx->isInitialized()) {
        std::stringstream initMessages;
        m_ctx->initialize(initMessages);
      }
      // RenderOptions default to PROGRESSIVE, which renders the frame as a
      // sequence of ever finer passes for an interactive viewer to display.
      // An ANARI frame is one fixed-sample-count render that nothing looks
      // at until it completes, so every frame - the first one included - is
      // a BATCH render (docs/adr/0002): all of a tile's samples in one pass,
      // without the progressive pass refinement.
      m_ctx->setRenderMode(moonray::rndr::RenderMode::BATCH);
      m_rendering = true;
      m_ctx->startFrame();
      while (!m_ctx->isFrameComplete()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      m_ctx->stopFrame();
      m_rendering = false;

      // What MoonRay decided, which is not always what was asked for: xpu
      // falls back to vectorized when the GPU cannot be set up, and auto
      // falls back to scalar for scenes with features vector mode lacks.
      switch (m_ctx->getCurrentExecutionMode()) {
      case moonray::mcrt_common::ExecutionMode::SCALAR:
        m_executionMode = "scalar";
        break;
      case moonray::mcrt_common::ExecutionMode::VECTORIZED:
        m_executionMode = "vectorized";
        break;
      case moonray::mcrt_common::ExecutionMode::XPU:
        m_executionMode = "xpu";
        break;
      default:
        m_executionMode = "auto";
        break;
      }

      scene_rdl2::fb_util::RenderBuffer buffer;
      m_ctx->snapshotRenderBuffer(
          &buffer, /*untile=*/true, /*parallel=*/true, /*usePrimaryAov=*/true);

      const unsigned w = buffer.getWidth();
      const unsigned h = buffer.getHeight();
      if (w != size.x || h != size.y) {
        error = "MoonRay produced a " + std::to_string(w) + "x"
            + std::to_string(h) + " frame, expected "
            + std::to_string(size.x) + "x" + std::to_string(size.y);
        return;
      }

      // MoonRay's framebuffer and ANARI's frame both start at the lower left
      // (RenderOutputWriter mirrors y when it hands scanlines to OpenImageIO),
      // so the rows are copied straight across.
      const scene_rdl2::fb_util::RenderColor *pixels = buffer.getData();
      for (size_t i = 0; i < size_t(w) * size_t(h); ++i) {
        color[i] = float4(pixels[i][0], pixels[i][1], pixels[i][2],
            pixels[i][3]);
      }

      if (depth)
        snapshotDepthOutput(*m_ctx, buffer, size, *depth, error);
    } catch (const std::exception &e) {
      m_rendering = false;
      error = e.what();
    } catch (...) {
      m_rendering = false;
      error = "unknown exception from MoonRay";
    }
  });

  return error.empty();
}


void MoonrayScene::requestStop()
{
  // requestStop() only raises MoonRay's global cancel flag, which is designed
  // to be set from another thread (it is even called from a signal handler by
  // moonray's own checkpoint handling).
  std::lock_guard<std::mutex> guard(m_ctxMutex);
  if (m_ctx && m_rendering)
    m_ctx->requestStop();
}

} // namespace moonray_anari
