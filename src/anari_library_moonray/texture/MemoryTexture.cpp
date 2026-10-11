// Copyright 2026 openmoonray-anari contributors
// SPDX-License-Identifier: Apache-2.0

#include "MemoryTexture.h"
#include "Parallel.h"

// OpenImageIO
#include <OpenImageIO/imagecache.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/ustring.h>
// std
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <system_error>
#include <unordered_map>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace moonray_anari {
namespace texture {

namespace {

// The format the reader is declared under, and the file extension that
// selects it in OIIO::ImageInput::open().
const char *const kFormatName = "anari_moonray_memory";
const char *const kExtension = "anarimem";

// 64 is what maketx writes, and what the scratch-file textures had.
constexpr int kTileSize = 64;

// One MIP level, in the texture's storage format.
struct Level
{
  int width{0};
  int height{0};
  std::vector<uint8_t> data;
};

struct Image
{
  OIIO::TypeDesc format{OIIO::TypeDesc::FLOAT};
  int channels{4};
  bool tiled{true};
  std::vector<Level> levels;

  size_t pixelBytes() const
  {
    return size_t(channels) * format.size();
  }

  size_t bytes() const
  {
    size_t total = 0;
    for (const auto &level : levels)
      total += level.data.size();
    return total;
  }
};

struct Registry
{
  std::mutex mutex;
  std::unordered_map<std::string, std::shared_ptr<const Image>> images;
};

// Never destroyed: OpenImageIO may still ask for a texture while the process
// is shutting down, after this library's statics would have gone.
Registry &registry()
{
  static Registry *r = new Registry;
  return *r;
}

std::shared_ptr<const Image> findImage(const std::string &name)
{
  Registry &r = registry();
  std::lock_guard<std::mutex> guard(r.mutex);
  auto it = r.images.find(name);
  return it == r.images.end() ? nullptr : it->second;
}

// --- the reader --------------------------------------------------------------

// Serves a registered Image to OpenImageIO. The instance holds a reference to
// the pixels, so a texture released while a render is still sampling it stays
// readable until OpenImageIO closes this reader.
class MemoryInput final : public OIIO::ImageInput
{
 public:
  const char *format_name() const override
  {
    return kFormatName;
  }

  int supports(OIIO::string_view feature) const override
  {
    // "procedural": the name is not a file, so nobody should look for one.
    return feature == "procedural";
  }

  bool valid_file(const std::string &name) const override
  {
    return findImage(name) != nullptr;
  }

  bool open(const std::string &name, OIIO::ImageSpec &newspec) override
  {
    m_image = findImage(name);
    if (!m_image) {
      errorfmt("\"{}\" is not a registered in-memory texture", name);
      return false;
    }
    m_level = 0;
    m_spec = levelSpec(0);
    newspec = m_spec;
    return true;
  }

  bool open(const std::string &name,
      OIIO::ImageSpec &newspec,
      const OIIO::ImageSpec & /*config*/) override
  {
    return open(name, newspec);
  }

  bool close() override
  {
    m_image.reset();
    m_level = 0;
    return true;
  }

  int current_subimage() const override
  {
    return 0;
  }

  int current_miplevel() const override
  {
    return m_level;
  }

  bool seek_subimage(int subimage, int miplevel) override
  {
    if (!m_image || subimage != 0 || miplevel < 0
        || miplevel >= int(m_image->levels.size()))
      return false;
    m_level = miplevel;
    m_spec = levelSpec(miplevel);
    return true;
  }

  bool read_native_scanline(
      int subimage, int miplevel, int y, int /*z*/, void *data) override
  {
    const Level *level = findLevel(subimage, miplevel);
    if (!level || y < 0 || y >= level->height)
      return false;
    const size_t rowBytes = size_t(level->width) * m_image->pixelBytes();
    std::memcpy(data, level->data.data() + size_t(y) * rowBytes, rowBytes);
    return true;
  }

  bool read_native_tile(int subimage,
      int miplevel,
      int x,
      int y,
      int /*z*/,
      void *data) override
  {
    const Level *level = findLevel(subimage, miplevel);
    if (!level || !m_image->tiled || x < 0 || y < 0 || x >= level->width
        || y >= level->height)
      return false;

    // A tile on the right or bottom edge reaches past the image; that part
    // is zero filled.
    const size_t pixelBytes = m_image->pixelBytes();
    const size_t tileRowBytes = size_t(kTileSize) * pixelBytes;
    const int copyWidth = std::min(kTileSize, level->width - x);
    const int copyHeight = std::min(kTileSize, level->height - y);
    auto *out = static_cast<uint8_t *>(data);
    if (copyWidth < kTileSize || copyHeight < kTileSize)
      std::memset(out, 0, size_t(kTileSize) * tileRowBytes);
    for (int row = 0; row < copyHeight; ++row) {
      const uint8_t *src = level->data.data()
          + (size_t(y + row) * size_t(level->width) + size_t(x)) * pixelBytes;
      std::memcpy(
          out + size_t(row) * tileRowBytes, src, size_t(copyWidth) * pixelBytes);
    }
    return true;
  }

 private:
  const Level *findLevel(int subimage, int miplevel) const
  {
    if (!m_image || subimage != 0 || miplevel < 0
        || miplevel >= int(m_image->levels.size()))
      return nullptr;
    return &m_image->levels[size_t(miplevel)];
  }

  OIIO::ImageSpec levelSpec(int miplevel) const
  {
    const Level &level = m_image->levels[size_t(miplevel)];
    OIIO::ImageSpec spec(
        level.width, level.height, m_image->channels, m_image->format);
    if (m_image->tiled) {
      spec.tile_width = kTileSize;
      spec.tile_height = kTileSize;
      spec.tile_depth = 1;
    }
    return spec;
  }

  std::shared_ptr<const Image> m_image;
  int m_level{0};
};

OIIO::ImageInput *createMemoryInput()
{
  return new MemoryInput;
}

void declareFormat()
{
  static std::once_flag once;
  std::call_once(once, [] {
    static const char *extensions[] = {kExtension, nullptr};
    OIIO::declare_imageio_format(kFormatName,
        createMemoryInput,
        extensions,
        /*output_creator=*/nullptr,
        /*output_extensions=*/nullptr,
        /*lib_version=*/"1");
  });
}

// --- names -------------------------------------------------------------------

uint64_t nextSerial()
{
  static std::atomic<uint64_t> serial{0};
  return ++serial;
}

unsigned long long processId()
{
#if defined(_WIN32)
  return (unsigned long long)::_getpid();
#else
  return (unsigned long long)::getpid();
#endif
}

// A name std::filesystem::exists() accepts although no such file is ever
// created, because MoonRay tests exactly that before it asks OpenImageIO
// (texture::TextureSampler::getHandle()). OpenImageIO keys its cache on the
// string itself, so the names stay distinct even though they all resolve to
// the same directory.
std::string textureSystemName(uint64_t serial, std::string &error)
{
  std::error_code ec;
  const std::filesystem::path base = std::filesystem::temp_directory_path(ec);
  if (ec) {
    error = "the system temp directory is not available: " + ec.message();
    return {};
  }

#if defined(_WIN32)
  // Win32 collapses "<component>\.." lexically, before the file system is
  // asked, so the component does not have to exist.
  const std::string name =
      (base / ("anari_moonray_memory_" + std::to_string(processId()) + "_"
                  + std::to_string(serial))
          / "..")
          .string();
#else
  // POSIX resolves every component, so the unique part can only be made of
  // components that are no-ops: the serial number is spelled in binary, lowest
  // bit first, as "/." for a one and "/" for a zero.
  std::string name = base.string();
  while (!name.empty() && name.back() == '/')
    name.pop_back();
  for (uint64_t bits = serial; bits != 0; bits >>= 1)
    name += (bits & 1u) ? "/." : "/";
#endif

  if (!std::filesystem::exists(name, ec)) {
    error = "'" + name + "' does not resolve to an existing directory, so "
        "MoonRay would not ask OpenImageIO for it";
    return {};
  }
  return name;
}

// A name for OIIO::ImageInput::open(), which finds the reader by extension.
std::string imageInputName(uint64_t serial)
{
  return "anari_moonray_memory_" + std::to_string(processId()) + "_"
      + std::to_string(serial) + "." + kExtension;
}

// --- pixels ------------------------------------------------------------------

// Box-filters `src` (channels floats per pixel) down to dstWidth x dstHeight.
// Each destination pixel averages the source area it covers, which for an
// exact halving is the usual 2x2 average.
std::vector<float> downsample(const std::vector<float> &src,
    int srcWidth,
    int srcHeight,
    int dstWidth,
    int dstHeight,
    int channels)
{
  const size_t c = size_t(channels);

  // The common case, every level of a power-of-two image: an exact halving
  // along each axis (or an axis that is already down to one pixel).
  const bool exactX = srcWidth == 2 * dstWidth || srcWidth == dstWidth;
  const bool exactY = srcHeight == 2 * dstHeight || srcHeight == dstHeight;
  if (exactX && exactY) {
    const size_t stepX = srcWidth == dstWidth ? 0 : c;
    const size_t stepY =
        srcHeight == dstHeight ? 0 : size_t(srcWidth) * c;
    std::vector<float> dst(size_t(dstWidth) * size_t(dstHeight) * c);
    parallelRanges(size_t(dstHeight),
        std::max<size_t>(1, 65536 / size_t(dstWidth)),
        [&](size_t rowBegin, size_t rowEnd) {
          for (size_t y = rowBegin; y < rowEnd; ++y) {
            const float *in = src.data()
                + y * (srcHeight == dstHeight ? 1 : 2) * size_t(srcWidth) * c;
            float *out = dst.data() + y * size_t(dstWidth) * c;
            for (int x = 0; x < dstWidth; ++x, in += c + stepX, out += c) {
              for (size_t ch = 0; ch < c; ++ch) {
                out[ch] = 0.25f
                    * (in[ch] + in[ch + stepX] + in[ch + stepY]
                        + in[ch + stepX + stepY]);
              }
            }
          }
        });
    return dst;
  }

  struct Tap
  {
    int first;
    int count;
    std::vector<float> weights;
  };
  auto taps = [](int srcSize, int dstSize) {
    std::vector<Tap> result(static_cast<size_t>(dstSize));
    const double scale = double(srcSize) / double(dstSize);
    for (int d = 0; d < dstSize; ++d) {
      const double begin = d * scale;
      const double end = std::min(double(srcSize), (d + 1) * scale);
      Tap &tap = result[size_t(d)];
      tap.first = std::min(srcSize - 1, int(begin));
      const int last = std::min(srcSize - 1, int(std::ceil(end)) - 1);
      tap.count = std::max(1, last - tap.first + 1);
      tap.weights.resize(size_t(tap.count));
      double sum = 0.0;
      for (int i = 0; i < tap.count; ++i) {
        const double lo = std::max(begin, double(tap.first + i));
        const double hi = std::min(end, double(tap.first + i + 1));
        const double w = std::max(0.0, hi - lo);
        tap.weights[size_t(i)] = float(w);
        sum += w;
      }
      if (sum <= 0.0) {
        tap.weights.assign(size_t(tap.count), 1.f / float(tap.count));
      } else {
        for (auto &w : tap.weights)
          w = float(w / sum);
      }
    }
    return result;
  };

  const auto xTaps = taps(srcWidth, dstWidth);
  const auto yTaps = taps(srcHeight, dstHeight);

  // Horizontal pass into srcHeight rows of dstWidth pixels.
  std::vector<float> rows(size_t(dstWidth) * size_t(srcHeight) * c);
  for (int y = 0; y < srcHeight; ++y) {
    const float *in = src.data() + size_t(y) * size_t(srcWidth) * c;
    float *out = rows.data() + size_t(y) * size_t(dstWidth) * c;
    for (int x = 0; x < dstWidth; ++x) {
      const Tap &tap = xTaps[size_t(x)];
      for (size_t ch = 0; ch < c; ++ch) {
        float v = 0.f;
        for (int i = 0; i < tap.count; ++i)
          v += tap.weights[size_t(i)] * in[size_t(tap.first + i) * c + ch];
        out[size_t(x) * c + ch] = v;
      }
    }
  }

  // Vertical pass.
  std::vector<float> dst(size_t(dstWidth) * size_t(dstHeight) * c);
  const size_t rowFloats = size_t(dstWidth) * c;
  for (int y = 0; y < dstHeight; ++y) {
    const Tap &tap = yTaps[size_t(y)];
    float *out = dst.data() + size_t(y) * rowFloats;
    std::fill(out, out + rowFloats, 0.f);
    for (int i = 0; i < tap.count; ++i) {
      const float w = tap.weights[size_t(i)];
      const float *in = rows.data() + size_t(tap.first + i) * rowFloats;
      for (size_t k = 0; k < rowFloats; ++k)
        out[k] += w * in[k];
    }
  }
  return dst;
}

// Moves `pixels` into `level` in the image's storage format.
void storeLevel(Level &level,
    int width,
    int height,
    const std::vector<float> &pixels,
    const Image &image)
{
  level.width = width;
  level.height = height;
  const size_t values = size_t(width) * size_t(height) * size_t(image.channels);
  level.data.resize(values * image.format.size());
  if (image.format == OIIO::TypeDesc::FLOAT) {
    std::memcpy(level.data.data(), pixels.data(), level.data.size());
  } else {
    const size_t valueBytes = image.format.size();
    parallelRanges(values, 1 << 18, [&](size_t begin, size_t end) {
      OIIO::convert_pixel_values(OIIO::TypeDesc::FLOAT,
          pixels.data() + begin,
          image.format,
          level.data.data() + begin * valueBytes,
          int(end - begin));
    });
  }
}

} // namespace

std::string registerMemoryTexture(const MemoryTextureDesc &desc,
    std::vector<float> &&pixelsIn,
    std::string &error)
{
  error.clear();

  std::vector<float> pixels = std::move(pixelsIn);
  const int channels = desc.channels;
  if (desc.width == 0 || desc.height == 0) {
    error = "the image is empty";
    return {};
  }
  if (channels < 1 || channels > 4) {
    error = "a texture has one to four channels";
    return {};
  }
  if (desc.width > size_t(1) << 20 || desc.height > size_t(1) << 20) {
    error = "the image is too large";
    return {};
  }
  if (pixels.size() != desc.width * desc.height * size_t(channels)) {
    error = "the pixel buffer does not match the image size";
    return {};
  }

  auto image = std::make_shared<Image>();
  image->format =
      desc.halfFloat ? OIIO::TypeDesc::HALF : OIIO::TypeDesc::FLOAT;
  image->channels = channels;
  image->tiled = desc.use == MemoryTextureUse::TextureSystem;

  int width = int(desc.width);
  int height = int(desc.height);

  if (image->tiled && width == 1 && height == 1) {
    // The texture system refuses a texture without MIP levels, and a single
    // pixel has none: it is stored as a 2x2 block of itself.
    std::vector<float> block;
    block.reserve(size_t(4 * channels));
    for (int i = 0; i < 4; ++i)
      block.insert(block.end(), pixels.begin(), pixels.end());
    pixels = std::move(block);
    width = height = 2;
  }

  image->levels.emplace_back();
  storeLevel(image->levels.back(), width, height, pixels, *image);

  if (image->tiled) {
    // The full pyramid, down to 1x1, each level filtered from the one above.
    while (width > 1 || height > 1) {
      const int nextWidth = std::max(1, width / 2);
      const int nextHeight = std::max(1, height / 2);
      pixels =
          downsample(pixels, width, height, nextWidth, nextHeight, channels);
      width = nextWidth;
      height = nextHeight;
      image->levels.emplace_back();
      storeLevel(image->levels.back(), width, height, pixels, *image);
    }
  }
  pixels = std::vector<float>();

  declareFormat();

  const uint64_t serial = nextSerial();
  const std::string name = image->tiled ? textureSystemName(serial, error)
                                        : imageInputName(serial);
  if (name.empty())
    return {};

  {
    Registry &r = registry();
    std::lock_guard<std::mutex> guard(r.mutex);
    r.images[name] = image;
  }

  if (image->tiled) {
    // Tell the image cache behind the shared texture system - the one MoonRay
    // samples through - who reads this name. This opens the texture once, so
    // a problem shows up here rather than as a black surface.
    auto cache = OIIO::ImageCache::create(/*shared=*/true);
    if (!cache
        || !cache->add_file(OIIO::ustring(name),
            createMemoryInput,
            /*config=*/nullptr,
            /*replace=*/true)) {
      error = "OpenImageIO did not accept the in-memory texture";
      if (cache) {
        const std::string reason = cache->geterror();
        if (!reason.empty())
          error += ": " + reason;
      }
      Registry &r = registry();
      std::lock_guard<std::mutex> guard(r.mutex);
      r.images.erase(name);
      return {};
    }
  }

  return name;
}

void releaseMemoryTexture(const std::string &name)
{
  if (name.empty())
    return;

  bool tiled = false;
  {
    Registry &r = registry();
    std::lock_guard<std::mutex> guard(r.mutex);
    auto it = r.images.find(name);
    if (it == r.images.end())
      return;
    tiled = it->second->tiled;
    r.images.erase(it);
  }

  if (tiled) {
    // Drops the cached tiles and closes the reader, which holds the last
    // reference to the pixels. OpenImageIO allows this while other threads
    // are sampling; they keep the tiles they already hold.
    if (auto cache = OIIO::ImageCache::create(/*shared=*/true))
      cache->invalidate(OIIO::ustring(name), /*force=*/true);
  }
}

bool isMemoryTexture(const std::string &name)
{
  return findImage(name) != nullptr;
}

size_t memoryTextureCount()
{
  Registry &r = registry();
  std::lock_guard<std::mutex> guard(r.mutex);
  return r.images.size();
}

size_t memoryTextureBytes()
{
  Registry &r = registry();
  std::lock_guard<std::mutex> guard(r.mutex);
  size_t total = 0;
  for (const auto &entry : r.images)
    total += entry.second->bytes();
  return total;
}

} // namespace texture
} // namespace moonray_anari
