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
#include <cstdio>
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

helium::TimeStamp newest(helium::TimeStamp a, helium::TimeStamp b)
{
  return a > b ? a : b;
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

    rdl2::SceneObject *dataObj =
        createObject("UserData", name + "/attribute/" + attribute.name);
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

      // Samplers first: a textured parameter is an RDL2 binding to a Map
      // object, so the Map has to exist before the material is written.
      std::vector<std::pair<const char *, rdl2::SceneObject *>> bindings;
      bool write = false;
      for (const auto &binding : material->samplerBindings()) {
        Sampler *sampler = binding.sampler;
        if (!sampler || !sampler->isValid() || !binding.rdl2Attribute)
          continue;
        const char *samplerClass = sampler->rdl2ClassName();
        rdl2::SceneObject *mapObj = syncedObject(samplerClass,
            objectName("sampler", samplerClass, sampler),
            sampler->lastUpdated(),
            write);
        if (write) {
          // A sampler that reads a geometry attribute rather than the
          // surface's st gets it through an AttributeMap bound to the map's
          // input coordinates (Sampler::inputPrimitiveAttribute()). Written
          // first, like the geometry's UserData: it is a separate object the
          // map only has to point at.
          rdl2::SceneObject *inputObj = nullptr;
          if (const char *input = sampler->inputPrimitiveAttribute()) {
            inputObj = createObject("AttributeMap",
                objectName("sampler", samplerClass, sampler) + "/input");
            rdl2::SceneObject::UpdateGuard guard(inputObj);
            inputObj->set("map_type", 0); // primitive attribute
            inputObj->set("primitive_attribute_name", std::string(input));
            inputObj->set("primitive_attribute_type", 1); // vec2f
            // ImageMap reads a negative third component as "no coordinate"
            // and returns black; a geometry without the attribute samples the
            // start of the image instead.
            inputObj->set("default_value", rdl2::Rgb(0.f, 0.f, 0.f));
            inputObj->set("warn_when_unavailable", false);
          }
          rdl2::SceneObject::UpdateGuard guard(mapObj);
          sampler->syncRdl2(*mapObj);
          if (inputObj)
            mapObj->setBinding("input_texture_coordinates", inputObj);
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
      rdl2::SceneObject *materialObj = syncedObject(materialClass,
          objectName("material", materialClass, material),
          material->lastUpdated(),
          write);
      if (write) {
        rdl2::SceneObject::UpdateGuard guard(materialObj);
        material->syncRdl2(*materialObj);
        for (const auto &b : bindings)
          materialObj->setBinding(b.first, b.second);
      }
      auto *rdlMaterial = materialObj->asA<rdl2::Material>();

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
          rdl2::SceneObject::UpdateGuard guard(geometryObj);
          geometryObj->set("node_xform", toRdl2(instance->xfm()));
        }
        auto *rdlGeometry = geometryObj->asA<rdl2::Geometry>();
        renderGeometries.push_back(rdlGeometry);
        assignments.push_back(
            {rdlGeometry, rdlMaterial, nullptr, false, geometryName});
        continue;
      }

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
      rdl2::SceneObject *instancerObj = syncedObject("RdlInstancerGeometry",
          objectName("instancer", "RdlInstancerGeometry", group, surface),
          newest(geometry->lastUpdated(), instancesVersion),
          instancerWrite);

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

      if (instancerWrite) {
        rdl2::Mat4dVector xforms;
        xforms.reserve(instances.size());
        for (auto *instance : instances)
          xforms.push_back(toRdl2(instance->xfm()));

        rdl2::SceneObjectVector references;
        references.push_back(rdlPrototype);

        rdl2::SceneObject::UpdateGuard guard(instancerObj);
        // 2 == "xform list": the per-instance transforms come from
        // xform_list rather than from positions/orientations/scales.
        instancerObj->set("method", 2);
        instancerObj->set("xform_list", xforms);
        // The prototype's node_xform is the identity above; not applying it
        // keeps the ANARI instance transform the only transform in play.
        instancerObj->set("use_reference_xforms", false);
        instancerObj->set(rdl2::Geometry::sReferenceGeometries, references);
      }
      auto *rdlInstancer = instancerObj->asA<rdl2::Geometry>();

      renderGeometries.push_back(rdlInstancer);
      assignments.push_back({rdlInstancer,
          rdlMaterial,
          nullptr,
          false,
          instancerObj->getName()});
      assignments.push_back(
          {rdlPrototype, rdlMaterial, nullptr, false, prototypeName});
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
            geometryObj->getName()});
      }
    }
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
    if (it == m_layerAssignments.end())
      m_layerAssignments.push_back(a);
    else
      *it = a; // the same geometry, possibly with a different shader
  }

  for (auto &slot : m_layerAssignments) {
    const bool live = std::any_of(assignments.begin(),
        assignments.end(),
        [&](const LayerAssignment &a) { return a.geometry == slot.geometry; });
    if (live == !slot.retired)
      continue; // already in the state it should be in

    slot.retired = !live;

    // A retired geometry gives its data back: RDL2 cannot delete the object,
    // but resetting it to its defaults releases the vertex arrays it is
    // holding (and the UserData objects named after it, which carry the
    // primitive attributes). Dropping the cache entries with them is what
    // makes this reversible - a geometry that comes back is then written from
    // scratch instead of being trusted to still hold its data.
    if (!live)
      retireObject(slot.name);

    // The keys rather than the attribute names: these are declared on RDL2's
    // Geometry base class, so every geometry has them, and the names are not
    // always what the key is called (sVisiblePhase is "visible_volume").
    rdl2::SceneObject::UpdateGuard guard(slot.geometry);
    slot.geometry->set(rdl2::Geometry::sVisibleCamera, live);
    slot.geometry->set(rdl2::Geometry::sVisibleShadow, live);
    slot.geometry->set(rdl2::Geometry::sVisibleDiffuseReflection, live);
    slot.geometry->set(rdl2::Geometry::sVisibleDiffuseTransmission, live);
    slot.geometry->set(rdl2::Geometry::sVisibleGlossyReflection, live);
    slot.geometry->set(rdl2::Geometry::sVisibleGlossyTransmission, live);
    slot.geometry->set(rdl2::Geometry::sVisibleMirrorReflection, live);
    slot.geometry->set(rdl2::Geometry::sVisibleMirrorTransmission, live);
    slot.geometry->set(rdl2::Geometry::sVisiblePhase, live);
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
        rdl2::SceneObject::UpdateGuard guard(lightObj);
        light->syncRdl2(*lightObj, instance->xfm());
      }
      lights.push_back(lightObj->asA<rdl2::Light>());
    }
  }

  rdl2::LightSet *lightSet =
      createObject("LightSet", "/anari/lightSet")->asA<rdl2::LightSet>();
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

  // --- surfaces: geometry + material + layer assignment -------------------
  rdl2::SceneObject *geometrySet =
      createObject("GeometrySet", "/anari/geometrySet");
  rdl2::SceneObject *layer = createObject("Layer", "/anari/layer");

  syncSurfaces(world, lightSet, geometrySet, layer);

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
  m_sceneChanged = !m_hasRendered || m_objectsUpdated > 0 || m_layerRebuilt
      || lightsChanged || cameraObj != m_lastCamera;
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
