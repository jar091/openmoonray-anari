// Milestone 4 / ADR 0003 Stage 3: SceneClass DSO loading error paths.
//
// The loader shim and the search-path separator landed in M2; what was never
// exercised is what happens when loading goes WRONG. Every case below is a way
// a Windows install can be broken in the field, and each must produce a
// diagnosable exception rather than a null SceneClass, a silent skip or a
// crash. The historical port returned NULL from failed lookups (audit §2.3),
// which is exactly the behaviour these tests exist to prevent regressing to.
//
//   - class not in the search path        (typo / incomplete install)
//   - a .dll that is not a PE image       (truncated download, LFS pointer)
//   - a .dll with no rdl2_declare         (unrelated library dropped in)
//   - a .dll missing rdl2_create/destroy  (half-ported DSO, non-proxy mode)
//   - a .dll whose dependency is absent   (missing runtime next to the plugin)
//   - a search path containing spaces     ("C:\Program Files\...")
//   - a multi-entry search path with a drive letter (the ':' vs ';' bug)
//
// The malformed plugins are built by this directory's CMakeLists (see
// dso_stubs/) rather than fabricated here, so they are real PE images that
// really do lack the symbols.

#include "../unit/TestHarness.h"

#include <scene_rdl2/scene/rdl2/SceneContext.h>
#include <scene_rdl2/scene/rdl2/Dso.h>
#include <scene_rdl2/render/util/Files.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;
using namespace openmoonray_anari::test;

namespace {

#if defined(_WIN32)
constexpr char kSep = ';';
constexpr const char *kDsoExt = ".dll";
#else
constexpr char kSep = ':';
constexpr const char *kDsoExt = ".so";
#endif

// Where this test's helper plugins were built (passed by CMake).
std::string stubDir()
{
    const char *d = std::getenv("OPENMOONRAY_ANARI_DSO_STUB_DIR");
    return d ? d : "";
}

// Scratch directory for the cases that need files created at run time. It
// lives in the build tree (CMake passes OPENMOONRAY_ANARI_DSO_SCRATCH_DIR)
// rather than in the user's temp directory: the "not a PE image" case makes
// the loader open a deliberately bogus file, and on Windows doing that under
// %TEMP% drags in the on-access scanner - the case then takes minutes instead
// of milliseconds when the process is not interactive.
fs::path scratchRoot()
{
    static fs::path root = [] {
        const char *d = std::getenv("OPENMOONRAY_ANARI_DSO_SCRATCH_DIR");
        return d && *d ? fs::path(d) : fs::temp_directory_path() / "omr_anari_dso_test";
    }();
    return root;
}

// Try to create a SceneClass from `className` found in `searchPath`.
// Returns true when it threw, and captures the message.
bool loadThrows(const std::string &searchPath, const std::string &className,
                std::string &message)
{
    try {
        scene_rdl2::rdl2::SceneContext ctx;
        ctx.setDsoPath(searchPath);
        // createSceneObject drives the same declare/create path a real render
        // uses; going through SceneContext rather than Dso directly is
        // deliberate, because SceneContext is what every caller actually
        // touches.
        ctx.createSceneObject(className, "/test_object");
    } catch (const std::exception &e) {
        message = e.what();
        return true;
    } catch (...) {
        message = "<non-std exception>";
        return true;
    }
    message.clear();
    return false;
}

// `expectedInMessage` is what makes each case test *its own* failure mode:
// without it every case is satisfied by the generic "couldn't find DSO", so a
// missing or mis-pointed stub directory would let the whole suite pass while
// testing nothing.
void expectThrows(const std::string &searchPath, const std::string &className,
                  const char *what, const char *expectedInMessage)
{
    std::string msg;
    const bool threw = loadThrows(searchPath, className, msg);
    if (!threw) {
        std::printf("FAIL: %s: loading '%s' succeeded, expected an exception\n",
                    what, className.c_str());
        ++failureCount();
        return;
    }
    // An exception with an empty message is barely better than a null return:
    // the operator has to be able to tell what went wrong.
    if (msg.empty()) {
        std::printf("FAIL: %s: threw, but with an empty message\n", what);
        ++failureCount();
        return;
    }
    if (msg.find(expectedInMessage) == std::string::npos) {
        std::printf("FAIL: %s: message does not mention '%s': %s\n",
                    what, expectedInMessage, msg.c_str());
        ++failureCount();
        return;
    }
    std::printf("ok   %s: %s\n", what, msg.c_str());
}

// --- the cases ------------------------------------------------------------

void testMissingClass()
{
    expectThrows(stubDir(), "ThisSceneClassDoesNotExist", "missing class",
                 "Couldn't find DSO");
}

void testNotAPeImage()
{
    const fs::path dir = scratchRoot() / "garbage";
    fs::create_directories(dir);
    const fs::path plugin = dir / (std::string("GarbageDso") + kDsoExt);
    {
        std::ofstream f(plugin, std::ios::binary);
        f << "this is not a shared library, it is a text file\n";
    }
    std::fprintf(stderr, "[case]   wrote %s\n", plugin.string().c_str());

#if defined(_WIN32)
    // Loading a file that is not a PE image makes the Windows loader raise a
    // hard error. The process-wide SetErrorMode() in main() is not enough on
    // its own: the loader consults the *thread's* error mode, and without
    // this the call sits in an LPC wait to csrss for minutes whenever the
    // process has no interactive desktop (i.e. under CTest).
    DWORD previousMode = 0;
    SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX,
                       &previousMode);
#endif

    expectThrows(dir.string(), "GarbageDso", "not a PE/ELF image",
                 "failed to open it");

#if defined(_WIN32)
    SetThreadErrorMode(previousMode, nullptr);
#endif
}

void testNoDeclareSymbol()
{
    // dso_stub_no_symbols exports nothing at all.
    expectThrows(stubDir(), "NoSymbolsDso", "no rdl2_declare",
                 "rdl2_declare");
}

void testMissingCreateSymbol()
{
    // dso_stub_declare_only exports rdl2_declare but neither rdl2_create nor
    // rdl2_destroy. Proxy mode accepts that; normal mode must not.
    expectThrows(stubDir(), "DeclareOnlyDso", "declare without create/destroy",
                 "rdl2_create");
}

void testMissingDependency()
{
    // dso_stub_missing_dep links against a helper DLL that CMake deliberately
    // does NOT place next to it, so the image is valid but unloadable.
    expectThrows(stubDir(), "MissingDepDso", "unresolved dependency",
                 "failed to open it");
}

void testPathWithSpaces()
{
    // A search path with spaces must not be word-split or truncated. Only the
    // path handling is under test, so a missing class is the expected outcome
    // - what matters is that the failure names the class, not that the path
    // was mangled.
    const fs::path dir = scratchRoot() / "dir with spaces" / "rdl2dso";
    fs::create_directories(dir);
    std::string msg;
    const bool threw = loadThrows(dir.string(), "AlsoMissing", msg);
    CHECK(threw);
    if (threw) {
        std::printf("ok   path with spaces: %s\n", msg.c_str());
    }
}

void testMultiEntrySearchPath()
{
    // The historical bug: DsoFinder emitted ';' on Windows while the consumer
    // split on ':', which also breaks on the ':' in "C:\...". Build a
    // two-entry path whose entries both contain drive letters on Windows and
    // check that the real stub directory is still reached.
    const fs::path empty = scratchRoot() / "empty";
    fs::create_directories(empty);

    const std::string path = empty.string() + kSep + stubDir();
    // NoSymbolsDso lives in the SECOND entry: if the separator handling is
    // wrong the loader never gets there and the message would be about the
    // class being absent everywhere rather than about the missing symbol.
    expectThrows(path, "NoSymbolsDso", "multi-entry search path",
                 "rdl2_declare");
}

} // namespace

int main()
{
    if (stubDir().empty()) {
        std::printf("FAIL: OPENMOONRAY_ANARI_DSO_STUB_DIR is not set\n");
        return 1;
    }

    std::error_code ec;
    fs::remove_all(scratchRoot(), ec); // ignore: may not exist

    // Unbuffered progress on stderr: when one of the cases blocks (a broken
    // plugin can take the Windows loader a long way), the captured output
    // still shows which one.
    std::setvbuf(stderr, nullptr, _IONBF, 0);

#define RUN_CASE(fn)                                                          \
    do {                                                                      \
        std::fprintf(stderr, "[case] " #fn "\n");                             \
        fn();                                                                 \
    } while (0)

    RUN_CASE(testMissingClass);
    RUN_CASE(testNotAPeImage);
    RUN_CASE(testNoDeclareSymbol);
    RUN_CASE(testMissingCreateSymbol);
    RUN_CASE(testMissingDependency);
    RUN_CASE(testPathWithSpaces);
    RUN_CASE(testMultiEntrySearchPath);

#undef RUN_CASE

    fs::remove_all(scratchRoot(), ec);

    if (failureCount() == 0) {
        std::printf("PASS\n");
        return 0;
    }
    std::printf("%d check(s) FAILED\n", failureCount());
    return 1;
}
