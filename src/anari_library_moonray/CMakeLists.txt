# anari_library_moonray — the ANARI device backed by the OpenMoonRay renderer.
#
# Derived from ANARI-SDK v0.16.0 examples/empty_helium_device (Apache-2.0,
# Copyright The Khronos Group), renamed per docs/adr/0004; the MoonRay backend
# (backend/, and the object subtypes that translate into RDL2) landed with
# Milestone 7. MoonrayDefinitions.json advertises exactly what is implemented.
#
# The target name MUST stay `anari_library_moonray` — the ANARI loader derives
# the module filename from it (anari_library_moonray.dll /
# libanari_library_moonray.so / .dylib).

if(NOT OPENMOONRAY_ANARI_MOONRAY_FOUND)
    message(STATUS
        "anari_library_moonray not built: it is the MoonRay device and needs "
        "an installed MoonRay (see cmake/OpenMoonRay.cmake)")
    return()
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(anari_library_moonray SHARED
  Diagnostics.cpp
  MoonrayDevice.cpp
  MoonrayDeviceGlobalState.cpp
  MoonrayLibrary.cpp
  Object.cpp
  backend/MoonrayBackend.cpp
  backend/MoonrayScene.cpp
  camera/Camera.cpp
  camera/Orthographic.cpp
  camera/Perspective.cpp
  frame/Frame.cpp
  geometry/Curve.cpp
  geometry/Cylinder.cpp
  geometry/Geometry.cpp
  geometry/Sphere.cpp
  geometry/Triangle.cpp
  light/Directional.cpp
  light/HDRI.cpp
  light/Light.cpp
  light/Point.cpp
  light/Spot.cpp
  light/Quad.cpp
  material/Material.cpp
  material/Matte.cpp
  material/PhysicallyBased.cpp
  material/Rdla.cpp
  renderer/Renderer.cpp
  sampler/Image1D.cpp
  sampler/Image2D.cpp
  sampler/OutputTransform.cpp
  sampler/Sampler.cpp
  sampler/Transform.cpp
  spatial_field/SpatialField.cpp
  spatial_field/StructuredRegular.cpp
  surface/Surface.cpp
  texture/MemoryTexture.cpp
  texture/Pixels.cpp
  texture/ScratchFile.cpp
  texture/ScratchTexture.cpp
  texture/Texture.cpp
  volume/TransferFunction1D.cpp
  volume/Volume.cpp
  world/Group.cpp
  world/Instance.cpp
  world/World.cpp
)

target_link_libraries(anari_library_moonray
PRIVATE
  anari::helium
  anari::anari
  Moonray::rendering_rndr
  SceneRdl2::scene_rdl2
  # The samplers and the hdri light hand MoonRay their pixels through
  # OpenImageIO, which is what MoonRay reads every texture with: from memory
  # (texture/MemoryTexture.cpp) or, as a fallback, from scratch files.
  OpenImageIO::OpenImageIO
  # The transferFunction1D volume writes MoonRay's volume files: VdbGeometry
  # reads an OpenVDB file, and there is no in-memory entry point.
  ${OPENMOONRAY_ANARI_OPENVDB_TARGET}
)

# MoonRay is built as C++17 with its pinned toolchain set; match upstream.
target_compile_features(anari_library_moonray PRIVATE cxx_std_17)

include(GenerateExportHeader)
generate_export_header(anari_library_moonray EXPORT_MACRO_NAME "MOONRAY_EXPORT")

target_include_directories(anari_library_moonray
PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)

set_target_properties(anari_library_moonray PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  C_VISIBILITY_PRESET hidden
)

if(WIN32)
  target_compile_definitions(anari_library_moonray PRIVATE
    _USE_MATH_DEFINES
    NOMINMAX
    WIN32_LEAN_AND_MEAN
  )
endif()

anari_generate_queries(
  DEVICE_TARGET anari_library_moonray
  CPP_NAMESPACE moonray_anari
  JSON_DEFINITIONS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/MoonrayDefinitions.json
)

install(TARGETS anari_library_moonray
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
