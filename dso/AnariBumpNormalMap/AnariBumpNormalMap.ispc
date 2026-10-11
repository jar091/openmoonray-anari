// Copyright 2026 openmoonray-anari contributors
// SPDX-License-Identifier: Apache-2.0

/// @file AnariBumpNormalMap.ispc

#include "attributes.isph"

#include <moonray/rendering/shading/ispc/MapApi.isph>

// Keep in sync with AnariBumpNormalMap.cc
#define ANARI_BUMP_NUM_ATTRIBUTES 4

struct AnariBumpNormalMap
{
    const uniform NormalMap * uniform mInputNormal;
    uniform intptr_t mSampleNormalFunc;
    const uniform Xform * uniform mXform;
    // The primitive attributes the device exports the generic ANARI
    // attributes as, which the maps of the height may read.
    uniform int mAttributeKeys[ANARI_BUMP_NUM_ATTRIBUTES];
    uniform int mInstanceTransformKey;
};
ISPC_UTIL_EXPORT_UNIFORM_STRUCT_TO_HEADER(AnariBumpNormalMap);

// The height at the point (ds, dt) away from the shading point in the
// parameters of the surface.
//
// MoonRay has no call for evaluating a map somewhere else, so the map gets a
// copy of the shading state with what depends on the position moved along:
// the position, the texture coordinates and the primitive attributes of
// mAttributeKeys. Those are stored outside the state; they are changed in
// place and put back.
static varying float
heightAt(const uniform NormalMap * uniform map,
         const uniform AnariBumpNormalMap * uniform me,
         uniform ShadingTLState * uniform tls,
         const varying State &state,
         const varying float ds,
         const varying float dt)
{
    varying Intersection shifted = asAnIntersection(state);
    shifted.mP = state.mP + state.mdPds * ds + state.mdPdt * dt;
    shifted.mSt.x = state.mSt.x + ds;
    shifted.mSt.y = state.mSt.y + dt;

    uniform uint8_t * varying base = (uniform uint8_t * varying)Address64_get(state.mData);
    varying Vec3f saved[ANARI_BUMP_NUM_ATTRIBUTES];
    varying bool moved[ANARI_BUMP_NUM_ATTRIBUTES];
    for (uniform int i = 0; i < ANARI_BUMP_NUM_ATTRIBUTES; ++i) {
        const uniform int key = me->mAttributeKeys[i];
        moved[i] = isProvided(state, key) && isdsProvided(state, key) && isdtProvided(state, key);
        if (moved[i]) {
            uniform float * varying value = (uniform float * varying)(base + tls->mAttributeOffsets[key]);
            const varying Vec3f v = Vec3f_ctor(value[0], value[1], value[2]);
            const varying Vec3f dvds = Vec3f_ctor(value[3], value[4], value[5]);
            const varying Vec3f dvdt = Vec3f_ctor(value[6], value[7], value[8]);
            saved[i] = v;
            const varying Vec3f there = v + dvds * ds + dvdt * dt;
            value[0] = there.x;
            value[1] = there.y;
            value[2] = there.z;
        }
    }

    const varying float height = evalAttrInputHeight(map, tls, asAState(shifted));

    for (uniform int i = 0; i < ANARI_BUMP_NUM_ATTRIBUTES; ++i) {
        if (moved[i]) {
            uniform float * varying value =
                (uniform float * varying)(base + tls->mAttributeOffsets[me->mAttributeKeys[i]]);
            value[0] = saved[i].x;
            value[1] = saved[i].y;
            value[2] = saved[i].z;
        }
    }
    return height;
}

static Vec3f
sampleNormal(const uniform NormalMap * uniform map,
             uniform ShadingTLState * uniform tls,
             const varying State &state)
{
    const uniform AnariBumpNormalMap * uniform me = NORMALMAP_GET_ISPC_PTR(AnariBumpNormalMap, map);

    // The normal to bump
    varying Vec3f N = getN(state);
    if (me->mInputNormal != NULL && me->mSampleNormalFunc != 0) {
        SampleNormalPtr sampleNormalFn = (SampleNormalPtr)me->mSampleNormalFunc;
        varying Vec3f input = N;
        sampleNormalFn(me->mInputNormal, tls, &state, &input);
        const varying float inputLength = length(input);
        if (inputLength > 0.0f) {
            N = input / inputLength;
        }
    }

    // The two points next to the shading point, in the parameters of the
    // surface: a fraction of the footprint of the pixel
    const uniform float filterWidth = getAttrFilterWidth(map);
    varying float dsx = getdSdx(state) * filterWidth;
    varying float dtx = getdTdx(state) * filterWidth;
    varying float dsy = getdSdy(state) * filterWidth;
    varying float dty = getdTdy(state) * filterWidth;
    varying Vec3f dPx = state.mdPds * dsx + state.mdPdt * dtx;
    varying Vec3f dPy = state.mdPds * dsy + state.mdPdt * dty;
    {
        // Without a footprint (no ray differentials) or with a degenerate
        // one: a small step along each parameter
        const varying float crossSqr = lengthSqr(cross(dPx, dPy));
        if (!(crossSqr > 1e-8f * lengthSqr(dPx) * lengthSqr(dPy))) {
            const uniform float step = 1.0f / 2048.0f;
            dsx = step;
            dtx = 0.0f;
            dsy = 0.0f;
            dty = step;
            dPx = state.mdPds * step;
            dPy = state.mdPdt * step;
        }
    }

    const varying float h = evalAttrInputHeight(map, tls, state);
    const varying float hx = heightAt(map, me, tls, state, dsx, dtx);
    const varying float hy = heightAt(map, me, tls, state, dsy, dty);

    // The space lengths are measured in
    const uniform bool objectSpace = getAttrObjectSpace(map);
    varying Vec3f Nb = N;
    if (objectSpace) {
        dPx = transformVector(me->mXform, SHADING_SPACE_RENDER, SHADING_SPACE_OBJECT, state, dPx);
        dPy = transformVector(me->mXform, SHADING_SPACE_RENDER, SHADING_SPACE_OBJECT, state, dPy);
        Nb = transformNormal(me->mXform, SHADING_SPACE_RENDER, SHADING_SPACE_OBJECT, state, N);
        if (isProvided(state, me->mInstanceTransformKey)) {
            const varying Mat4f instanceToWorld = getMat4fAttribute(tls, state, me->mInstanceTransformKey);
            const varying Mat4f worldToInstance = inverse(instanceToWorld);
            dPx = transformVector(worldToInstance, dPx);
            dPy = transformVector(worldToInstance, dPy);
            // normals transform with the transposed inverse
            Nb = transformVector(transpose(instanceToWorld), Nb);
        }
        const varying float NbLength = length(Nb);
        if (NbLength > 0.0f) {
            Nb = Nb / NbLength;
        }
    }

    // The gradient of the height on the surface (Mikkelsen, "Bump Mapping
    // Unparametrized Surfaces on the GPU"): the normal tilts against it
    const varying Vec3f Rx = cross(dPy, Nb);
    const varying Vec3f Ry = cross(Nb, dPx);
    const varying float det = dot(dPx, Rx);
    const varying Vec3f gradient = Rx * (hx - h) + Ry * (hy - h);

    varying float distance = evalAttrDistance(map, tls, state);
    if (getAttrInvert(map)) {
        distance = -distance;
    }
    varying Vec3f bumped = Nb * abs(det) - gradient * (distance * (det < 0.0f ? -1.0f : 1.0f));

    if (objectSpace) {
        if (isProvided(state, me->mInstanceTransformKey)) {
            const varying Mat4f instanceToWorld = getMat4fAttribute(tls, state, me->mInstanceTransformKey);
            bumped = transformVector(transpose(inverse(instanceToWorld)), bumped);
        }
        bumped = transformNormal(me->mXform, SHADING_SPACE_OBJECT, SHADING_SPACE_RENDER, state, bumped);
    }

    const varying float bumpedLengthSqr = lengthSqr(bumped);
    if (bumpedLengthSqr > 0.0f && !isnan(bumpedLengthSqr)) {
        bumped = bumped * rsqrt(bumpedLengthSqr);
    } else {
        bumped = N;
    }

    const varying float strength = max(evalAttrStrength(map, tls, state), 0.0f);
    varying Vec3f result = bumped * strength + N * (1.0f - strength);
    const varying float resultLengthSqr = lengthSqr(result);
    if (resultLengthSqr > 0.0f && !isnan(resultLengthSqr)) {
        result = result * rsqrt(resultLengthSqr);
    } else {
        result = N;
    }
    return result;
}

DEFINE_NORMALMAP_SHADER(AnariBumpNormalMap, sampleNormal)
