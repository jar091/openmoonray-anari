// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "MoonrayDeviceGlobalState.h"
#include "MoonrayMath.h"
// helium
#include "helium/BaseObject.h"
#include "helium/utility/ChangeObserverPtr.h"
// std
#include <cstdint>
#include <string_view>

namespace moonray_anari {

// This is the base of all objects other than arrays and frames, where all
// truely device-wide generic object data and behaviors can be consolidated. The
// main addition this has over helium::BaseObject is the concept of "object
// validity" (isValid() virtual function).
struct Object : public helium::BaseObject
{
  Object(ANARIDataType type, MoonrayDeviceGlobalState *s);
  virtual ~Object() = default;

  virtual bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint64_t size,
      uint32_t flags) override;

  virtual void commitParameters() override;
  virtual void finalize() override;

  // Returns whether this object is valid or not. Validity is defined as being
  // a known object subtype and it is sufficiently parameterized to do something
  // useful in rendering a frame. When assembling objects in the scene behind
  // the API, it is useful to skip invalid objects as invalid/unknown object
  // usage should not be fatal to the application.
  bool isValid() const override;

  // Convenience function to get the generic helium::DeviceGlobalState as this
  // device's specific global state type.
  MoonrayDeviceGlobalState *deviceState() const;

  // A number that identifies this object for as long as the process runs.
  //
  // The RDL2 objects the translation builds are named after this rather than
  // after the object's address, because addresses are recycled: a new ANARI
  // object allocated where a destroyed one used to be would otherwise inherit
  // the destroyed one's RDL2 object, along with every attribute that one set
  // and this one does not. RDL2 has no way to delete a SceneObject, so the
  // old one stays behind either way - but with a distinct name it stays
  // behind *inert* instead of being mistaken for this object.
  uint64_t syncId() const
  {
    return m_syncId;
  }

 private:
  const uint64_t m_syncId;
};

// This gets instantiated for all object subtypes which are not known
struct UnknownObject : public Object
{
  UnknownObject(ANARIDataType type, MoonrayDeviceGlobalState *s);
  ~UnknownObject() override = default;
  bool isValid() const override;
};

// Reports that `subtype` is not implemented for objects of `kind` ("geometry",
// "sampler", ...). The factories still return an (invalid) object, so the
// handle stays usable, but without this message the object would simply be
// missing from the render and nothing would say why.
void reportUnknownSubtype(
    const helium::BaseObject *object, const char *kind, std::string_view subtype);

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_SPECIALIZATION(moonray_anari::Object *, ANARI_OBJECT);
