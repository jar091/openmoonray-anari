// Cross-platform framebuffer reference tool for Milestone 3.
//
// Renders the canonical smoke scene in-process (the same camera + mesh +
// material + light as test_smoke_render.cpp) and either writes the framebuffer
// as an OpenEXR reference or compares it against a stored one.
//
// Comparison is RMSE + max-absolute-difference against a tolerance, never a
// byte compare: TESTING.md requires metric-based checks because MSVC, GCC and
// AppleClang do not produce identical floating point, and the ISPC targets
// differ in width too (avx2-i32x8 on x86, neon-i32x4 on Apple silicon).
//
//   render_reference --mode scalar|vectorized [--width N] [--height N]
//                    (--write <out.exr> | --compare <ref.exr>)
//                    [--rmse-tol X] [--max-tol Y] [--mean-tol Z] [--selfcheck]
//
// --selfcheck renders twice in the same process and requires the two
// framebuffers to be bit-identical. Cross-platform tolerances are meaningless
// if a single platform is not deterministic to begin with, so the comparison
// tests run this first.
//
// Required environment: RDL2_DSO_PATH (set by the CTest wiring).

#include <moonray/rendering/rndr/RenderContext.h>

#include <scene_rdl2/common/fb_util/FbTypes.h>
#include <scene_rdl2/scene/rdl2/rdl2.h>

// Apple silicon only, and the include order here is not free to change.
//
// scene_rdl2/render/util/AtomicFloat.h explicitly specializes
// std::atomic<float>/<double>, so it has to be seen before anything
// instantiates them - OpenImageIO/atomic.h does, which rules out including
// OpenImageIO first. But scene_rdl2/common/platform/Platform.hh defines
// __SSE3__/__SSSE3__/__SSE4_* on __aarch64__ so that its bundled sse2neon.h
// can emulate them, and OpenImageIO's bundled farmhash reads __SSSE3__ as
// "this is x86" and pulls in <immintrin.h> - which refuses to compile for
// arm64 and then collides with sse2neon on every intrinsic it redefines.
//
// So: MoonRay first, then hide the emulated feature macros from OpenImageIO.
// Every MoonRay header this file needs has already been parsed at this point,
// so undefining them here affects nothing but the OpenImageIO include below.
#if defined(__aarch64__)
#  undef __SSE3__
#  undef __SSSE3__
#  undef __SSE4_1__
#  undef __SSE4_2__
#  undef __AVX__
#  undef __AVX2__
#endif

#include <OpenImageIO/imageio.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

// Build the canonical scene and render it. Returns interleaved RGBA floats.
std::vector<float>
renderScene(const char *dsoPath, int width, int height, const std::string &execMode)
{
    using namespace scene_rdl2::rdl2;

    moonray::rndr::RenderOptions options;
    options.setDsoPath(dsoPath);
    options.setThreads(4);
    // Pin the execution mode: "auto" would pick a different path per machine
    // and make a stored reference meaningless.
    options.setDesiredExecutionMode(execMode);

    std::stringstream initMessages;
    moonray::rndr::initGlobalDriver(options);

    std::vector<float> out;
    {
        moonray::rndr::RenderContext ctx(options, &initMessages);
        SceneContext &sc = ctx.getSceneContext();

        SceneObject *cameraObj = sc.createSceneObject("PerspectiveCamera", "/camera");

        SceneObject *meshObj = sc.createSceneObject("RdlMeshGeometry", "/triangle");
        Geometry *mesh = meshObj->asA<Geometry>();
        {
            SceneObject::UpdateGuard guard(meshObj);
            meshObj->set("face_vertex_count", IntVector{3});
            meshObj->set("vertices_by_index", IntVector{0, 1, 2});
            meshObj->set("vertex_list_0",
                         Vec3fVector{{-3.0f, -3.0f, -3.0f},
                                     {3.0f, -3.0f, -3.0f},
                                     {0.0f, 3.0f, -3.0f}});
        }

        SceneObject *materialObj = sc.createSceneObject("UsdPreviewSurface", "/material");
        Material *material = materialObj->asA<Material>();

        SceneObject *lightObj = sc.createSceneObject("EnvLight", "/light");
        Light *light = lightObj->asA<Light>();

        SceneObject *lightSetObj = sc.createSceneObject("LightSet", "/lightSet");
        LightSet *lightSet = lightSetObj->asA<LightSet>();
        {
            SceneObject::UpdateGuard guard(lightSetObj);
            lightSet->add(light);
        }

        SceneObject *geoSetObj = sc.createSceneObject("GeometrySet", "/geometrySet");
        GeometrySet *geometrySet = geoSetObj->asA<GeometrySet>();
        {
            SceneObject::UpdateGuard guard(geoSetObj);
            geometrySet->add(mesh);
        }

        SceneObject *layerObj = sc.createSceneObject("Layer", "/layer");
        Layer *layer = layerObj->asA<Layer>();
        {
            SceneObject::UpdateGuard guard(layerObj);
            layer->assign(mesh, "", material, lightSet);
        }

        SceneVariables &vars = sc.getSceneVariables();
        {
            SceneObject::UpdateGuard guard(&vars);
            vars.set(SceneVariables::sImageWidth, width);
            vars.set(SceneVariables::sImageHeight, height);
            vars.set(SceneVariables::sPixelSamplesSqrt, 2);
            // UNIFORM: adaptive sampling would let thread timing change the
            // sample count per pixel, which is not reproducible.
            vars.set(SceneVariables::sSamplingMode, 0);
            vars.set(SceneVariables::sCamera, cameraObj);
            vars.set(SceneVariables::sLayer, layerObj);
        }

        ctx.initialize(initMessages);
        ctx.setRenderMode(moonray::rndr::RenderMode::BATCH);

        ctx.startFrame();
        while (!ctx.isFrameComplete()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        ctx.stopFrame();

        scene_rdl2::fb_util::RenderBuffer buffer;
        ctx.snapshotRenderBuffer(&buffer, /*untile=*/true, /*parallel=*/true,
                                 /*usePrimaryAov=*/true);

        const unsigned w = buffer.getWidth();
        const unsigned h = buffer.getHeight();
        const scene_rdl2::fb_util::RenderColor *pixels = buffer.getData();

        out.resize(static_cast<std::size_t>(w) * h * 4);
        for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i) {
            for (int c = 0; c < 4; ++c) {
                out[i * 4 + c] = pixels[i][c];
            }
        }
    } // ~RenderContext before cleanUpGlobalDriver (ADR 0002)

    moonray::rndr::cleanUpGlobalDriver();
    return out;
}

bool
writeExr(const std::string &path, const std::vector<float> &rgba, int width, int height)
{
    auto out = OIIO::ImageOutput::create(path);
    if (!out) {
        std::printf("FAIL: no OIIO plugin for '%s': %s\n", path.c_str(),
                    OIIO::geterror().c_str());
        return false;
    }
    // Half would quantise the reference below the tolerances we want to assert.
    OIIO::ImageSpec spec(width, height, 4, OIIO::TypeDesc::FLOAT);
    spec.channelnames = {"R", "G", "B", "A"};
    spec.attribute("compression", "zip");
    if (!out->open(path, spec)) {
        std::printf("FAIL: cannot open '%s': %s\n", path.c_str(), out->geterror().c_str());
        return false;
    }
    const bool ok = out->write_image(OIIO::TypeDesc::FLOAT, rgba.data());
    if (!ok) {
        std::printf("FAIL: cannot write '%s': %s\n", path.c_str(), out->geterror().c_str());
    }
    out->close();
    return ok;
}

bool
readExr(const std::string &path, std::vector<float> &rgba, int &width, int &height)
{
    auto in = OIIO::ImageInput::open(path);
    if (!in) {
        std::printf("FAIL: cannot open reference '%s': %s\n", path.c_str(),
                    OIIO::geterror().c_str());
        return false;
    }
    const OIIO::ImageSpec &spec = in->spec();
    width = spec.width;
    height = spec.height;
    if (spec.nchannels < 4) {
        std::printf("FAIL: reference '%s' has %d channels, expected 4\n",
                    path.c_str(), spec.nchannels);
        in->close();
        return false;
    }
    rgba.resize(static_cast<std::size_t>(width) * height * 4);
    const bool ok = in->read_image(0, 0, 0, 4, OIIO::TypeDesc::FLOAT, rgba.data());
    if (!ok) {
        std::printf("FAIL: cannot read '%s': %s\n", path.c_str(), in->geterror().c_str());
    }
    in->close();
    return ok;
}

struct Metrics
{
    double rmse = 0.0;
    double maxAbs = 0.0;
    std::size_t worstIndex = 0;
    // How many pixels carry a difference worth looking at, and how big the
    // typical one is. A handful of hot pixels (edge coverage flipping) and a
    // difference spread over the whole image are very different diagnoses,
    // and rmse + max alone cannot tell them apart.
    std::size_t pixelsOverTol = 0;
    std::size_t pixelsDiffer = 0;
    // Mean RGB of each image. Per-pixel differences between two platforms are
    // dominated by Monte Carlo noise (the sample sequence is not the same on
    // both), which is zero-mean; a real shading regression is not. Comparing
    // the means is therefore the part of this comparison that can carry a
    // tight tolerance.
    double meanA = 0.0;
    double meanB = 0.0;
    double meanDiff = 0.0;
};

// RGB only: alpha is coverage and compares trivially, and including it would
// dilute the RMSE of the channels that actually carry the shading result.
Metrics
compareRgb(const std::vector<float> &a, const std::vector<float> &b, double tol = 0.0)
{
    Metrics m;
    double sumSq = 0.0;
    std::size_t n = 0;
    for (std::size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += 4) {
        double pixelMax = 0.0;
        for (int c = 0; c < 3; ++c) {
            const double d = static_cast<double>(a[i + c]) - static_cast<double>(b[i + c]);
            sumSq += d * d;
            ++n;
            const double ad = std::fabs(d);
            if (ad > pixelMax) pixelMax = ad;
            if (ad > m.maxAbs) {
                m.maxAbs = ad;
                m.worstIndex = i / 4;
            }
        }
        if (pixelMax > 0.0) ++m.pixelsDiffer;
        if (pixelMax > tol) ++m.pixelsOverTol;
        for (int c = 0; c < 3; ++c) {
            m.meanA += a[i + c];
            m.meanB += b[i + c];
        }
    }
    m.rmse = (n > 0) ? std::sqrt(sumSq / static_cast<double>(n)) : 0.0;
    if (n > 0) {
        m.meanA /= static_cast<double>(n);
        m.meanB /= static_cast<double>(n);
    }
    m.meanDiff = std::fabs(m.meanA - m.meanB);
    return m;
}

[[noreturn]] void
usage()
{
    std::printf("usage: render_reference --mode scalar|vectorized [--width N] [--height N]\n"
                "                        (--write <out.exr> | --compare <ref.exr>)\n"
                "                        [--rmse-tol X] [--max-tol Y] [--mean-tol Z]\n"
                "                        [--selfcheck [--selfcheck-tol Z]]\n");
    std::exit(2);
}

} // namespace

int main(int argc, char **argv)
{
    int width = 64;
    int height = 64;
    std::string mode;
    std::string writePath;
    std::string comparePath;
    double rmseTol = 1e-4;
    double maxTol = 1e-3;
    double meanTol = 1e-3;
    // Default 0 == bit-identical. SCALAR meets that; VECTORIZED does not,
    // because the per-lane/per-tile accumulation order follows thread
    // scheduling (measured ~7e-8, about one ULP near 1.0).
    double selfcheckTol = 0.0;
    bool selfcheck = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](const char *what) -> std::string {
            if (i + 1 >= argc) {
                std::printf("FAIL: %s needs a value\n", what);
                usage();
            }
            return argv[++i];
        };
        if (arg == "--width")            width = std::atoi(next("--width").c_str());
        else if (arg == "--height")      height = std::atoi(next("--height").c_str());
        else if (arg == "--mode")        mode = next("--mode");
        else if (arg == "--write")       writePath = next("--write");
        else if (arg == "--compare")     comparePath = next("--compare");
        else if (arg == "--rmse-tol")    rmseTol = std::atof(next("--rmse-tol").c_str());
        else if (arg == "--max-tol")     maxTol = std::atof(next("--max-tol").c_str());
        else if (arg == "--mean-tol")    meanTol = std::atof(next("--mean-tol").c_str());
        else if (arg == "--selfcheck")   selfcheck = true;
        else if (arg == "--selfcheck-tol") selfcheckTol = std::atof(next("--selfcheck-tol").c_str());
        else usage();
    }

    if (mode != "scalar" && mode != "vectorized") usage();
    if (writePath.empty() == comparePath.empty()) usage();

    const char *dsoPath = std::getenv("RDL2_DSO_PATH");
    if (!dsoPath || !*dsoPath) {
        std::printf("FAIL: RDL2_DSO_PATH is not set - cannot locate MoonRay DSOs\n");
        return 1;
    }

    const std::vector<float> rendered = renderScene(dsoPath, width, height, mode);
    const std::size_t expected = static_cast<std::size_t>(width) * height * 4;
    if (rendered.size() != expected) {
        std::printf("FAIL: rendered %zu floats, expected %zu (%dx%d RGBA)\n",
                    rendered.size(), expected, width, height);
        return 1;
    }

    if (selfcheck) {
        const std::vector<float> again = renderScene(dsoPath, width, height, mode);
        const Metrics m = compareRgb(rendered, again);
        // Always report the measured value, so run-to-run drift is visible even
        // while it stays under the bar.
        std::printf("selfcheck: two %dx%d %s renders differ by rmse=%.9g max=%.9g "
                    "(tol %.9g)%s\n",
                    width, height, mode.c_str(), m.rmse, m.maxAbs, selfcheckTol,
                    (m.maxAbs == 0.0) ? " [bit-identical]" : "");
        if (m.maxAbs > selfcheckTol) {
            std::printf("FAIL: this platform is not reproducible in %s mode at the "
                        "required tolerance (worst pixel %zu), so comparing it against "
                        "another platform means nothing\n", mode.c_str(), m.worstIndex);
            return 1;
        }
    }

    if (!writePath.empty()) {
        if (!writeExr(writePath, rendered, width, height)) return 1;
        std::printf("wrote %s (%dx%d, %s)\nPASS\n",
                    writePath.c_str(), width, height, mode.c_str());
        return 0;
    }

    std::vector<float> reference;
    int refW = 0, refH = 0;
    if (!readExr(comparePath, reference, refW, refH)) return 1;

    if (refW != width || refH != height) {
        std::printf("FAIL: reference is %dx%d, rendered %dx%d\n", refW, refH, width, height);
        return 1;
    }

    const Metrics m = compareRgb(rendered, reference, maxTol);
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    const std::size_t wx = m.worstIndex % static_cast<std::size_t>(width);
    const std::size_t wy = m.worstIndex / static_cast<std::size_t>(width);
    std::printf("%dx%d %s vs %s: rmse=%.9g (tol %.9g)  max=%.9g (tol %.9g)\n"
                "  %zu/%zu pixels differ, %zu over the max tolerance\n"
                "  worst pixel %zu (%zu,%zu): rendered %.6f %.6f %.6f  "
                "reference %.6f %.6f %.6f\n",
                width, height, mode.c_str(), comparePath.c_str(),
                m.rmse, rmseTol, m.maxAbs, maxTol,
                m.pixelsDiffer, pixels, m.pixelsOverTol,
                m.worstIndex, wx, wy,
                rendered[m.worstIndex * 4 + 0], rendered[m.worstIndex * 4 + 1],
                rendered[m.worstIndex * 4 + 2],
                reference[m.worstIndex * 4 + 0], reference[m.worstIndex * 4 + 1],
                reference[m.worstIndex * 4 + 2]);
    std::printf("  mean RGB: rendered %.9g  reference %.9g  |diff| %.9g (tol %.9g)\n",
                m.meanA, m.meanB, m.meanDiff, meanTol);

    if (m.rmse > rmseTol || m.maxAbs > maxTol || m.meanDiff > meanTol) {
        std::printf("%d check(s) FAILED\n",
                    (m.rmse > rmseTol) + (m.maxAbs > maxTol) + (m.meanDiff > meanTol));
        return 1;
    }
    std::printf("PASS\n");
    return 0;
}
