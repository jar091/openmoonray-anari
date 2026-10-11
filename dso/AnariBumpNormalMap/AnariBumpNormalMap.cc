// Copyright 2026 openmoonray-anari contributors
// SPDX-License-Identifier: Apache-2.0

/// @file AnariBumpNormalMap.cc
///
/// A normal map which turns a height into a normal: bump mapping.
///
/// The height is whatever map is bound to "input_height". It is evaluated
/// three times - at the shading point and at two points next to it, a fraction
/// of the footprint of the pixel away - and the normal is tilted against the
/// slope of the height between them. "distance" is the length a height of 1
/// stands for, "strength" blends between the given and the bumped normal.
/// The height is along the normal of the shading state, which faces the
/// viewer: a bump is a bump from both sides of a surface.
///
/// MoonRay has no call for evaluating a map at another point. The two extra
/// evaluations are given a copy of the shading state in which the position,
/// the texture coordinates and the primitive attributes anari_attribute0..3
/// (the generic attributes of the ANARI geometry, e.g. generated coordinates)
/// are moved to that point. What else a map reads - normals, the viewing
/// direction, other primitive attributes - stays as it is at the shading
/// point: a height that depends on those only gets no bump from them.
///
/// The copy relies on the layout of the shading state, which MoonRay shares
/// between C++ and ISPC in <moonray/rendering/shading/ispc/Intersection.hh>.

#include "attributes.cc"
#include "AnariBumpNormalMap_ispc_stubs.h"

#include <moonray/rendering/shading/MapApi.h>
#include <moonray/rendering/shading/ispc/Intersection.hh>

#include <cmath>
#include <cstring>
#include <memory>
#include <string>

using namespace scene_rdl2::math;

namespace {

// Keep in sync with AnariBumpNormalMap.ispc
const int sNumAttributes = 4;

// The spaces of moonray::shading::Xform
const int sSpaceRender = 0;
const int sSpaceObject = 4;

// The members of moonray::shading::Intersection. Its own header is not part of
// an installed MoonRay, the macro which declares its members is.
namespace layout {
struct AttributeTable;
struct Flags { uint32_t mBits; };
struct alignas(64) Intersection { INTERSECTION_MEMBERS; };
} // namespace layout

} // namespace

RDL2_DSO_CLASS_BEGIN(AnariBumpNormalMap, NormalMap)
public:
    AnariBumpNormalMap(SceneClass const &sceneClass, std::string const &name);
    void update() override;

private:
    static void sampleNormal(const NormalMap *self,
                             moonray::shading::TLState *tls,
                             const moonray::shading::State &state,
                             Vec3f *sample);

    float heightAt(moonray::shading::TLState *tls,
                   const moonray::shading::State &state,
                   float ds, float dt) const;

    ispc::AnariBumpNormalMap mIspc;
    NormalMap *mInputNormal;
    std::unique_ptr<moonray::shading::Xform> mXform;
    moonray::shading::TypedAttributeKey<Vec3f> mAttributeKeys[sNumAttributes];

RDL2_DSO_CLASS_END(AnariBumpNormalMap)

AnariBumpNormalMap::AnariBumpNormalMap(const SceneClass &sceneClass, const std::string &name) :
    Parent(sceneClass, name),
    mInputNormal(nullptr)
{
    mSampleNormalFunc = AnariBumpNormalMap::sampleNormal;
    mSampleNormalFuncv = (SampleNormalFuncv)ispc::AnariBumpNormalMap_getSampleFunc();

    for (int i = 0; i < sNumAttributes; ++i) {
        // With their derivatives: that is what moves them to another point
        mAttributeKeys[i] = moonray::shading::TypedAttributeKey<Vec3f>(
            "anari_attribute" + std::to_string(i), true);
        mIspc.mAttributeKeys[i] = mAttributeKeys[i];
    }
    mIspc.mInstanceTransformKey = moonray::shading::StandardAttributes::sInstanceTransformLevel0;
    mIspc.mInputNormal = nullptr;
    mIspc.mSampleNormalFunc = 0;
    mIspc.mXform = nullptr;
}

void
AnariBumpNormalMap::update()
{
    mInputNormal = get(attrInputNormal) ? get(attrInputNormal)->asA<NormalMap>() : nullptr;
    mIspc.mInputNormal = mInputNormal;
    mIspc.mSampleNormalFunc = mInputNormal ? (intptr_t)mInputNormal->mSampleNormalFuncv : (intptr_t)0;

    // The object space of the geometry being shaded
    mXform = std::make_unique<moonray::shading::Xform>(this, nullptr, nullptr, nullptr);
    mIspc.mXform = mXform->getIspcXform();

    mOptionalAttributes.clear();
    for (int i = 0; i < sNumAttributes; ++i) {
        mOptionalAttributes.push_back(mAttributeKeys[i]);
    }
    mOptionalAttributes.push_back(moonray::shading::StandardAttributes::sInstanceTransformLevel0);
}

// The height at the point (ds, dt) away from the shading point in the
// parameters of the surface, see the head of this file.
float
AnariBumpNormalMap::heightAt(moonray::shading::TLState *tls,
                             const moonray::shading::State &state,
                             float ds, float dt) const
{
    layout::Intersection shifted;
    std::memcpy(static_cast<void *>(&shifted), state.getIntersection(), sizeof(shifted));
    shifted.mP = state.getP() + state.getdPds() * ds + state.getdPdt() * dt;
    shifted.mSt = state.getSt() + Vec2f(ds, dt);

    // The primitive attributes are stored outside the state: they are changed
    // in place and put back.
    Vec3f saved[sNumAttributes];
    Vec3f *moved[sNumAttributes];
    for (int i = 0; i < sNumAttributes; ++i) {
        moved[i] = nullptr;
        const auto &key = mAttributeKeys[i];
        Vec3f dvds, dvdt;
        if (state.isProvided(key) && state.getdVec3fAttrds(key, dvds) && state.getdVec3fAttrdt(key, dvdt)) {
            moved[i] = const_cast<Vec3f *>(&state.getAttribute(key));
            saved[i] = *moved[i];
            *moved[i] = saved[i] + dvds * ds + dvdt * dt;
        }
    }

    const moonray::shading::State shiftedState(
        reinterpret_cast<const moonray::shading::Intersection *>(&shifted));
    const float height = evalFloat(this, attrInputHeight, tls, shiftedState);

    for (int i = 0; i < sNumAttributes; ++i) {
        if (moved[i]) {
            *moved[i] = saved[i];
        }
    }
    return height;
}

void
AnariBumpNormalMap::sampleNormal(const NormalMap *self,
                                 moonray::shading::TLState *tls,
                                 const moonray::shading::State &state,
                                 Vec3f *sample)
{
    const AnariBumpNormalMap *me = static_cast<const AnariBumpNormalMap *>(self);

    // The normal to bump
    Vec3f N = state.getN();
    if (me->mInputNormal) {
        Vec3f input = N;
        me->mInputNormal->sampleNormal(tls, state, &input);
        const float inputLength = length(input);
        if (inputLength > 0.0f) {
            N = input / inputLength;
        }
    }

    // The two points next to the shading point, in the parameters of the
    // surface: a fraction of the footprint of the pixel
    const float filterWidth = me->get(attrFilterWidth);
    float dsx = state.getdSdx() * filterWidth;
    float dtx = state.getdTdx() * filterWidth;
    float dsy = state.getdSdy() * filterWidth;
    float dty = state.getdTdy() * filterWidth;
    Vec3f dPx = state.getdPds() * dsx + state.getdPdt() * dtx;
    Vec3f dPy = state.getdPds() * dsy + state.getdPdt() * dty;
    {
        // Without a footprint (no ray differentials) or with a degenerate
        // one: a small step along each parameter
        const float crossSqr = lengthSqr(cross(dPx, dPy));
        if (!(crossSqr > 1e-8f * lengthSqr(dPx) * lengthSqr(dPy))) {
            const float step = 1.0f / 2048.0f;
            dsx = step;
            dtx = 0.0f;
            dsy = 0.0f;
            dty = step;
            dPx = state.getdPds() * step;
            dPy = state.getdPdt() * step;
        }
    }

    const float h = evalFloat(me, attrInputHeight, tls, state);
    const float hx = me->heightAt(tls, state, dsx, dtx);
    const float hy = me->heightAt(tls, state, dsy, dty);

    // The space lengths are measured in
    const bool objectSpace = me->get(attrObjectSpace);
    const moonray::shading::TypedAttributeKey<Mat4f> instanceKey =
        moonray::shading::StandardAttributes::sInstanceTransformLevel0;
    const bool instanced = objectSpace && state.isProvided(instanceKey);
    Mat4f instanceToWorld(one);
    if (instanced) {
        instanceToWorld = state.getAttribute(instanceKey);
    }
    Vec3f Nb = N;
    if (objectSpace) {
        dPx = me->mXform->transformVector(sSpaceRender, sSpaceObject, state, dPx);
        dPy = me->mXform->transformVector(sSpaceRender, sSpaceObject, state, dPy);
        Nb = me->mXform->transformNormal(sSpaceRender, sSpaceObject, state, N);
        if (instanced) {
            const Mat4f worldToInstance = instanceToWorld.inverse();
            dPx = transformVector(worldToInstance, dPx);
            dPy = transformVector(worldToInstance, dPy);
            // normals transform with the transposed inverse
            Nb = transformVector(instanceToWorld.transposed(), Nb);
        }
        const float NbLength = length(Nb);
        if (NbLength > 0.0f) {
            Nb = Nb / NbLength;
        }
    }

    // The gradient of the height on the surface (Mikkelsen, "Bump Mapping
    // Unparametrized Surfaces on the GPU"): the normal tilts against it
    const Vec3f Rx = cross(dPy, Nb);
    const Vec3f Ry = cross(Nb, dPx);
    const float det = dot(dPx, Rx);
    const Vec3f gradient = Rx * (hx - h) + Ry * (hy - h);

    float distance = evalFloat(me, attrDistance, tls, state);
    if (me->get(attrInvert)) {
        distance = -distance;
    }
    Vec3f bumped = Nb * scene_rdl2::math::abs(det) - gradient * (distance * (det < 0.0f ? -1.0f : 1.0f));

    if (objectSpace) {
        if (instanced) {
            bumped = transformVector(instanceToWorld.inverse().transposed(), bumped);
        }
        bumped = me->mXform->transformNormal(sSpaceObject, sSpaceRender, state, bumped);
    }

    const float bumpedLengthSqr = lengthSqr(bumped);
    if (bumpedLengthSqr > 0.0f && std::isfinite(bumpedLengthSqr)) {
        bumped = bumped / scene_rdl2::math::sqrt(bumpedLengthSqr);
    } else {
        bumped = N;
    }

    const float strength = max(evalFloat(me, attrStrength, tls, state), 0.0f);
    Vec3f result = bumped * strength + N * (1.0f - strength);
    const float resultLengthSqr = lengthSqr(result);
    if (resultLengthSqr > 0.0f && std::isfinite(resultLengthSqr)) {
        result = result / scene_rdl2::math::sqrt(resultLengthSqr);
    } else {
        result = N;
    }
    *sample = result;
}
