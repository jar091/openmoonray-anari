// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "MoonrayDevice.h"

#include "array/Array1D.h"
#include "array/Array2D.h"
#include "array/Array3D.h"
#include "array/ObjectArray.h"
#include "backend/MoonrayScene.h"
#include "frame/Frame.h"
#include "spatial_field/SpatialField.h"

#include "anari_library_moonray_queries.h"

namespace moonray_anari {

// API Objects ////////////////////////////////////////////////////////////////

ANARIArray1D MoonrayDevice::newArray1D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems)
{
  initDevice();

  Array1DMemoryDescriptor md;
  md.appMemory = appMemory;
  md.deleter = deleter;
  md.deleterPtr = userData;
  md.elementType = type;
  md.numItems = numItems;

  if (anari::isObject(type))
    return (ANARIArray1D) new ObjectArray(deviceState(), md);
  else
    return (ANARIArray1D) new Array1D(deviceState(), md);
}

ANARIArray2D MoonrayDevice::newArray2D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2)
{
  initDevice();

  Array2DMemoryDescriptor md;
  md.appMemory = appMemory;
  md.deleter = deleter;
  md.deleterPtr = userData;
  md.elementType = type;
  md.numItems1 = numItems1;
  md.numItems2 = numItems2;

  return (ANARIArray2D) new Array2D(deviceState(), md);
}

ANARIArray3D MoonrayDevice::newArray3D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3)
{
  initDevice();

  Array3DMemoryDescriptor md;
  md.appMemory = appMemory;
  md.deleter = deleter;
  md.deleterPtr = userData;
  md.elementType = type;
  md.numItems1 = numItems1;
  md.numItems2 = numItems2;
  md.numItems3 = numItems3;

  return (ANARIArray3D) new Array3D(deviceState(), md);
}

ANARICamera MoonrayDevice::newCamera(const char *subtype)
{
  initDevice();
  return (ANARICamera)Camera::createInstance(subtype, deviceState());
}

ANARIFrame MoonrayDevice::newFrame()
{
  initDevice();
  return (ANARIFrame) new Frame(deviceState());
}

ANARIGeometry MoonrayDevice::newGeometry(const char *subtype)
{
  initDevice();
  return (ANARIGeometry)Geometry::createInstance(subtype, deviceState());
}

ANARIGroup MoonrayDevice::newGroup()
{
  initDevice();
  return (ANARIGroup) new Group(deviceState());
}

ANARIInstance MoonrayDevice::newInstance(const char *subtype)
{
  initDevice();
  auto *instance = new Instance(deviceState());
  // Only the 'transform' instance exists; any other subtype (motion
  // transforms, transform arrays) is still built as one, which has to be said
  // since its subtype-specific parameters are ignored.
  const std::string_view type = subtype ? subtype : "";
  if (!type.empty() && type != "transform") {
    instance->reportMessage(ANARI_SEVERITY_WARNING,
        "unsupported instance subtype '%s': the MoonRay device only "
        "implements 'transform' and treats the instance as one",
        subtype);
  }
  return (ANARIInstance)instance;
}

ANARILight MoonrayDevice::newLight(const char *subtype)
{
  initDevice();
  return (ANARILight)Light::createInstance(subtype, deviceState());
}

ANARIMaterial MoonrayDevice::newMaterial(const char *subtype)
{
  initDevice();
  return (ANARIMaterial)Material::createInstance(subtype, deviceState());
}

ANARIRenderer MoonrayDevice::newRenderer(const char *subtype)
{
  initDevice();
  return (ANARIRenderer)Renderer::createInstance(subtype, deviceState());
}

ANARISampler MoonrayDevice::newSampler(const char *subtype)
{
  initDevice();
  return (ANARISampler)Sampler::createInstance(subtype, deviceState());
}

ANARISpatialField MoonrayDevice::newSpatialField(const char *subtype)
{
  initDevice();
  return (ANARISpatialField)SpatialField::createInstance(
      subtype, deviceState());
}

ANARISurface MoonrayDevice::newSurface()
{
  initDevice();
  return (ANARISurface) new Surface(deviceState());
}

ANARIVolume MoonrayDevice::newVolume(const char *subtype)
{
  initDevice();
  return (ANARIVolume)Volume::createInstance(subtype, deviceState());
}

ANARIWorld MoonrayDevice::newWorld()
{
  initDevice();
  return (ANARIWorld) new World(deviceState());
}

// Query functions ////////////////////////////////////////////////////////////

const char **MoonrayDevice::getObjectSubtypes(ANARIDataType objectType)
{
  return moonray_anari::query_object_types(objectType);
}

const void *MoonrayDevice::getObjectInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *infoName,
    ANARIDataType infoType)
{
  return moonray_anari::query_object_info(
      objectType, objectSubtype, infoName, infoType);
}

const void *MoonrayDevice::getParameterInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *parameterName,
    ANARIDataType parameterType,
    const char *infoName,
    ANARIDataType infoType)
{
  return moonray_anari::query_param_info(objectType,
      objectSubtype,
      parameterName,
      parameterType,
      infoName,
      infoType);
}

// Other MoonrayDevice definitions /////////////////////////////////////////////

MoonrayDevice::MoonrayDevice(ANARIStatusCallback cb, const void *ptr)
    : helium::BaseDevice(cb, ptr)
{
  m_state = std::make_unique<MoonrayDeviceGlobalState>(this_device());
  deviceCommitParameters();
}

MoonrayDevice::MoonrayDevice(ANARILibrary l) : helium::BaseDevice(l)
{
  m_state = std::make_unique<MoonrayDeviceGlobalState>(this_device());
  deviceCommitParameters();
}

MoonrayDevice::~MoonrayDevice()
{
  auto &state = *deviceState();
  state.commitBuffer.clear();
  reportMessage(ANARI_SEVERITY_DEBUG, "destroying Moonray device (%p)", this);
}

void MoonrayDevice::initDevice()
{
  if (m_initialized)
    return;
  reportMessage(ANARI_SEVERITY_DEBUG, "initializing Moonray device (%p)", this);

  auto &state = *deviceState();
  state.scene = std::make_unique<MoonrayScene>(&state);
  state.scene->setExecutionMode(m_executionMode);
  if (!state.scene->valid()) {
    // Not fatal for object creation: the device stays usable for
    // introspection and reports the failure again when a frame is rendered.
    reportMessage(ANARI_SEVERITY_ERROR,
        "MoonRay could not be initialized: %s",
        state.scene->error().c_str());
  } else {
    reportMessage(ANARI_SEVERITY_DEBUG,
        "MoonRay renderer ready (RDL2 DSO path '%s')",
        state.scene->dsoPath().c_str());
  }

  m_initialized = true;
}

void MoonrayDevice::deviceCommitParameters()
{
  helium::BaseDevice::deviceCommitParameters();

  // Which of MoonRay's execution modes to ask for: "auto" (the default),
  // "scalar", "vectorized" or "xpu". It reaches the renderer through the
  // process-global RenderOptions, so it has to be set before the first frame -
  // and, MoonRay being one renderer per process, the first device to render
  // decides for every device in the process (docs/adr/0005).
  const std::string mode = getParamString("moonray.executionMode", "auto");
  if (mode != "auto" && mode != "scalar" && mode != "vectorized"
      && mode != "xpu") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "unknown 'moonray.executionMode' \"%s\" - using \"auto\" (valid: "
        "auto, scalar, vectorized, xpu)",
        mode.c_str());
    m_executionMode = "auto";
  } else {
    m_executionMode = mode;
  }

  auto *state = deviceState();
  if (state && state->scene)
    state->scene->setExecutionMode(m_executionMode);
}

int MoonrayDevice::deviceGetProperty(
    const char *name, ANARIDataType type, void *mem, uint64_t size, uint32_t mask)
{
  std::string_view prop = name;
  if (prop == "extension" && type == ANARI_STRING_LIST) {
    helium::writeToVoidP(mem, query_extensions());
    return 1;
  } else if (prop == "Moonray" && type == ANARI_BOOL) {
    helium::writeToVoidP(mem, true);
    return 1;
  }

  // Vendor properties describing what the last scene sync had to do. They
  // exist so "an unchanged scene is not re-translated" is something an
  // application can check rather than take on trust (SUPPORTED_FEATURES.md);
  // they say nothing about the image and are not part of any ANARI extension.
  auto *scene = deviceState()->scene.get();
  if (prop == "moonray.objectsUpdatedLastSync" && type == ANARI_INT32) {
    helium::writeToVoidP(mem, scene ? scene->objectsUpdatedLastSync() : 0);
    return 1;
  } else if (prop == "moonray.objectsUnchangedLastSync" && type == ANARI_INT32) {
    helium::writeToVoidP(mem, scene ? scene->objectsUnchangedLastSync() : 0);
    return 1;
  } else if (prop == "moonray.layerRebuiltLastSync" && type == ANARI_BOOL) {
    helium::writeToVoidP(mem, scene ? scene->layerRebuiltLastSync() : false);
    return 1;
  } else if (prop == "moonray.syncMillisecondsLastFrame"
      && type == ANARI_FLOAT32) {
    // The translation's own cost, separated from the render's: this is the
    // time syncScene() took, not the time the frame took. Without it "the
    // adapter is cheap" is only ever measurable through MoonRay's noise
    // (docs/PERFORMANCE.md).
    helium::writeToVoidP(mem, scene ? scene->syncMillisecondsLastFrame() : 0.f);
    return 1;
  } else if (prop == "moonray.executionMode" && type == ANARI_STRING) {
    // What MoonRay *is* running, which is only known once a frame has been
    // rendered - before that there is nothing to report but the request.
    m_executionModeProperty =
        scene && !scene->executionMode().empty() ? scene->executionMode()
                                                 : m_executionMode;
    helium::writeToVoidP(mem, m_executionModeProperty.c_str());
    return 1;
  }

  return 0;
}

MoonrayDeviceGlobalState *MoonrayDevice::deviceState() const
{
  return (MoonrayDeviceGlobalState *)helium::BaseDevice::m_state.get();
}

} // namespace moonray_anari
