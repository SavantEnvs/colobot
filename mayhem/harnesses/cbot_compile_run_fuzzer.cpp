// Fuzz the CBot scripting language end to end, the same path tools/cbot-console and the game take
// for a user/level script: CBotProgram::Compile (tokenizer + every CBotInstr compiler) and then
// Start/Run of every `extern void` entry point on the CBot stack interpreter (incl. stdlib string
// and math functions).
//
// State kept across inputs on purpose: CBotProgram::Init() (built-in constants, external call table,
// built-in classes) runs once and is never written to by a script. User classes a script declares
// are owned by the per-input CBotProgram and freed with it.
#include "CBot/CBot.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// CBot runs a program in slices of `kInstrPerSlice` instructions per Run() call (the game's own
// scheduling model). An instruction budget, not a wall-clock timer, so the result only depends
// on the input; infinite loops in a script just stop after the budget.
static constexpr int kInstrPerSlice = 100;
static constexpr int kMaxSlices = 2000;

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    CBot::CBotProgram::Init();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    std::string code(reinterpret_cast<const char*>(data), size);
    std::vector<std::string> externs;
    auto program = std::make_unique<CBot::CBotProgram>(nullptr);
    if (!program->Compile(code, externs, nullptr))
        return 0;
    for (const std::string& entry : externs)
    {
        if (!program->Start(entry))
            continue;
        for (int i = 0; i < kMaxSlices; ++i)
            if (program->Run(nullptr, kInstrPerSlice))
                break;
        program->Stop();
    }
    return 0;
}
