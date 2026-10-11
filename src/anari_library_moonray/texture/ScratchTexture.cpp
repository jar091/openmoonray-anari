// Copyright 2024-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "ScratchTexture.h"
#include "ScratchFile.h"

// OpenImageIO
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imageio.h>
// std
#include <sstream>

namespace moonray_anari {
namespace texture {

std::string writeScratchTexture(const char *tag,
    const void *owner,
    size_t width,
    size_t height,
    int channels,
    const float *pixels,
    bool mipmapped,
    std::string &error)
{
  error.clear();

  if (width == 0 || height == 0 || !pixels) {
    error = "the image is empty";
    return {};
  }

  const std::string path = scratch::newFilePath(tag, owner, "exr", error);
  if (path.empty())
    return {};

  OIIO::ImageSpec spec(
      int(width), int(height), channels, OIIO::TypeDesc::FLOAT);
  OIIO::ImageBuf source(spec, const_cast<float *>(pixels));

  OIIO::ImageSpec config;
  // Level 0 must stay exactly the pixels the application handed over: no
  // resizing to a power of two, no color conversion.
  config.attribute("maketx:resize", 0);
  config.attribute("maketx:filtername", "box");
  config.attribute("maketx:updatemode", 0);
  config.attribute("maketx:oiio_options", 1);
  config.attribute("maketx:fileformatname", "exr");
  // An environment map needs no pyramid at all: EnvLight opens the file
  // itself instead of going through the texture system, and a pyramid that
  // reaches 1x1 makes it log "1x1 mip level not supported at present" for
  // every map it opens.
  if (!mipmapped)
    config.attribute("maketx:nomipmap", 1);

  std::ostringstream makeTxLog;
  if (!OIIO::ImageBufAlgo::make_texture(OIIO::ImageBufAlgo::MakeTxTexture,
          source,
          path,
          config,
          &makeTxLog)) {
    error = "could not write the texture '" + path + "': " + makeTxLog.str();
    scratch::removeFile(path);
    return {};
  }

  return path;
}

void removeScratchTexture(const std::string &path)
{
  scratch::removeFile(path);
}

} // namespace texture
} // namespace moonray_anari
