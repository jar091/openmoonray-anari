# Copyright 2026 openmoonray-anari contributors
# SPDX-License-Identifier: Apache-2.0

# AnariBumpNormalMap.cc, AnariBumpNormalMap.ispc and AnariBumpNormalMap.json
moonray_ispc_dso(AnariBumpNormalMap
    DEPENDENCIES
        Moonray::rendering_shading
        Moonray::shading_ispc
        SceneRdl2::scene_rdl2)

AnariMoonrayDso_cxx_compile_options(AnariBumpNormalMap)
