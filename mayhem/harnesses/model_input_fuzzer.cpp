// Fuzz Colobot's 3D model loaders through their public entry point, Gfx::ModelInput::Read(path),
// which dispatches on the file extension to the old binary format (.mod), the text format (.txt)
// or glTF (.gltf, nlohmann::json + external buffer files). Models come from game data and from
// mods/user levels, read through PhysFS (CInputStream), exactly as here.
//
// Input layout: byte 0 selects the format (% 3: .mod, .txt, .gltf). For .mod/.txt the rest is the
// model file. For .gltf the rest is split at the first NUL byte: the JSON document goes to m.gltf
// and the bytes after the NUL to m.bin, so a glTF "buffers" entry with "uri": "m.bin" reads them.
//
// State kept on purpose: the CLogger singleton (log level fixed at Error) and the PhysFS mount of a
// private scratch directory. Both files are rewritten (or removed) for every input, so each
// result depends only on that input.
#include "common/logger.h"
#include "common/resources/resourcemanager.h"
#include "graphics/model/model.h"
#include "graphics/model/model_input.h"
#include "graphics/model/model_io_exception.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

static std::filesystem::path g_dir;

static void WriteFile(const std::filesystem::path& p, const uint8_t* data, size_t size)
{
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
}

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
    static CLogger logger;
    logger.SetLogLevel(LogLevel::LOG_ERROR);
    static CResourceManager resources((*argv)[0]);

    const char* tmp = std::getenv("TMPDIR");
    std::string templ = std::string(tmp && *tmp ? tmp : "/tmp") + "/colobot-model-XXXXXX";
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

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 1) return -1;
    static const char* const kExt[] = { "m.mod", "m.txt", "m.gltf" };
    const char* name = kExt[data[0] % 3];
    const uint8_t* body = data + 1;
    size_t bodySize = size - 1;

    std::error_code ec;
    std::filesystem::remove(g_dir / "m.mod", ec);
    std::filesystem::remove(g_dir / "m.txt", ec);
    std::filesystem::remove(g_dir / "m.gltf", ec);
    std::filesystem::remove(g_dir / "m.bin", ec);

    if (data[0] % 3 == 2)
    {
        const void* nul = std::memchr(body, 0, bodySize);
        size_t jsonSize = nul ? static_cast<size_t>(static_cast<const uint8_t*>(nul) - body) : bodySize;
        WriteFile(g_dir / name, body, jsonSize);
        if (nul)
            WriteFile(g_dir / "m.bin", body + jsonSize + 1, bodySize - jsonSize - 1);
    }
    else
    {
        WriteFile(g_dir / name, body, bodySize);
    }

    try
    {
        auto model = Gfx::ModelInput::Read(name);
        if (model)
        {
            volatile size_t sink = model->GetMeshCount() + model->GetCrashSphereCount();
            (void)sink;
        }
    }
    catch (const std::exception&)
    {
        // CModelIOException, json parse errors, std::stoi/stof failures: rejected input.
    }
    return 0;
}
