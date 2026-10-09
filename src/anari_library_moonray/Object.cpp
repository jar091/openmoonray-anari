// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Object.h"

// std
#include <atomic>
#include <string>

namespace moonray_anari {

namespace {

uint64_t nextSyncId()
{
  // Never reused, never zero: zero is left free so a name built from an
  // unset id is obviously wrong rather than plausibly right.
  static std::atomic<uint64_t> counter{0};
  return ++counter;
}

} // namespace

// Object definitions /////////////////////////////////////////////////////////

Object::Object(ANARIDataType type, MoonrayDeviceGlobalState *s)
    : helium::BaseObject(type, s), m_syncId(nextSyncId())
{}

void Object::commitParameters()
{
  // no-op
}

void Object::finalize()
{
  // no-op
}

bool Object::getProperty(
    const std::string_view &name, ANARIDataType type, void *ptr, uint64_t size, uint32_t flags)
{
  if (name == "valid" && type == ANARI_BOOL) {
    helium::writeToVoidP(ptr, isValid());
    return true;
  }

  return false;
}

bool Object::isValid() const
{
  return true;
}

MoonrayDeviceGlobalState *Object::deviceState() const
{
  return (MoonrayDeviceGlobalState *)helium::BaseObject::m_state;
}

// UnknownObject definitions //////////////////////////////////////////////////

UnknownObject::UnknownObject(ANARIDataType type, MoonrayDeviceGlobalState *s)
    : Object(type, s)
{}

bool UnknownObject::isValid() const
{
  return false;
}

void reportUnknownSubtype(
    const helium::BaseObject *object, const char *kind, std::string_view subtype)
{
  const std::string name(subtype);
  object->reportMessage(ANARI_SEVERITY_WARNING,
      "unsupported %s subtype '%s': the MoonRay device does not implement it, "
      "so the object is invalid and will not be rendered",
      kind,
      name.c_str());
}

} // namespace moonray_anari

Moonray_ANARI_TYPEFOR_DEFINITION(moonray_anari::Object *);
