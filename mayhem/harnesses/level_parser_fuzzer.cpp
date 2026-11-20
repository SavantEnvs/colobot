// Fuzz Colobot's scene/level file parser: CLevelParser::Load() (the format of every scene.txt,
// chapter/level file, savegame data file and mod level, read through PhysFS) followed by the
// typed CLevelParserParam getters the game applies to the parsed values (ints, floats, bools,
// colours, points, arrays, enum names such as object/drive/tool/water/camera/pyro types).
//
// Setup kept on purpose (all read-only per input): the CLogger and CSystemUtils singletons, a
// CApplication (CLevelParser::Load asks it for the current language letter; it is never
// Create()d, so no window, GL or sound is touched), and a PhysFS mount of a private scratch
// directory holding one file, scene.txt, rewritten for every input.
#include "app/app.h"
#include "common/logger.h"
#include "common/resources/resourcemanager.h"
#include "common/system/system.h"
#include "level/parser/parser.h"
#include "level/parser/parserexceptions.h"
#include "level/parser/parserline.h"
#include "level/parser/parserparam.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

static std::filesystem::path g_dir;

// Parameter names the game's level loader (CRobotMain, CObjectManager) reads most often.
static const char* const kParams[] = {
    "image", "type", "name", "color", "pos", "water", "max", "text", "id", "filename", "air",
    "min", "delay", "mode", "factor", "zoom", "up", "track", "team", "table", "speed", "sleep",
    "select", "script", "satcom", "rank", "radius", "magnetic", "lookat", "level", "hard", "eye",
    "enable", "editor", "down", "dist", "dir", "dim", "diffuse", "center", "file", "power",
    "option", "angle", "value", "class", "x", "y", "z",
};

template <typename F>
static void Try(F&& f)
{
    try { f(); } catch (const std::exception&) {}
}

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
    static CLogger logger;
    logger.SetLogLevel(LogLevel::LOG_ERROR);
    std::vector<std::string> args((*argv), (*argv) + 1);
    auto& systemUtils = CSystemUtils::GetInstance();
    systemUtils.Init(args);
    static CResourceManager resources((*argv)[0]);
    static CApplication app(&systemUtils);

    const char* tmp = std::getenv("TMPDIR");
    std::string templ = std::string(tmp && *tmp ? tmp : "/tmp") + "/colobot-level-XXXXXX";
    if (mkdtemp(templ.data()) == nullptr)
    {
        std::perror("mkdtemp");
        std::abort();
    }
    g_dir = templ;
    if (!CResourceManager::AddLocation(g_dir))
        std::abort();
    return 0;
}

static void Exercise(CLevelParserParam* p)
{
    // Floats go into a separate double sink: converting a parsed float
    // (inf, 1e300) to an integer is UB in the harness, not in Colobot.
    volatile long sink = 0;
    volatile double fsink = 0;
    Try([&] { sink += p->AsInt(); });
    Try([&] { fsink += p->AsFloat(); });
    Try([&] { sink += static_cast<long>(p->AsString().size()); });
    Try([&] { sink += p->AsBool(); });
    Try([&] { auto c = p->AsColor(); fsink += c.r; fsink += c.g; fsink += c.b; fsink += c.a; });
    Try([&] { auto v = p->AsPoint(); fsink += v.x; fsink += v.y; fsink += v.z; });
    Try([&] { sink += static_cast<long>(p->AsObjectType()); });
    Try([&] { sink += static_cast<long>(p->AsDriveType()); });
    Try([&] { sink += static_cast<long>(p->AsToolType()); });
    Try([&] { sink += static_cast<long>(p->AsWaterType()); });
    Try([&] { sink += static_cast<long>(p->AsTerrainType()); });
    Try([&] { sink += p->AsBuildFlag(); });
    Try([&] { sink += p->AsResearchFlag(); });
    Try([&] { sink += static_cast<long>(p->AsSortType()); });
    Try([&] { sink += static_cast<long>(p->AsPyroType()); });
    Try([&] { sink += static_cast<long>(p->AsCameraType()); });
    Try([&] { sink += static_cast<long>(p->AsMissionType()); });
    Try([&] { sink += static_cast<long>(p->AsPlanetType()); });
    Try([&] { sink += static_cast<long>(p->AsPath("").native().size()); });
    Try([&] {
        for (const auto& e : p->AsArray())
        {
            Try([&] { fsink += e->AsFloat(); });
            Try([&] { sink += static_cast<long>(e->AsString().size()); });
        }
    });
    (void)sink;
    (void)fsink;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    {
        std::ofstream f(g_dir / "scene.txt", std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
    CLevelParser parser("scene.txt");
    try
    {
        parser.Load();
    }
    catch (const std::exception&)
    {
        return 0;
    }
    for (const auto& line : parser.GetLines())
    {
        volatile size_t sink = line->GetCommand().size();
        (void)sink;
        for (const char* name : kParams)
        {
            CLevelParserParam* p = nullptr;
            try { p = line->GetParam(name); } catch (const std::exception&) { continue; }
            if (p != nullptr && p->IsDefined())
                Exercise(p);
        }
    }
    return 0;
}
