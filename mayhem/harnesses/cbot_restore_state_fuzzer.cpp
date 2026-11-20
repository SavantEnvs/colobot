// Fuzz restoring a saved CBot execution state (what a savegame stores for every running robot
// program): CBotProgram::RestoreState + CBotClass::RestoreStaticState over the fuzz bytes, in the
// order CBotProgram/CBotClass SaveState write them, then continue running the restored stack.
//
// The program whose state is restored is a fixed built-in script (classes, statics, arrays,
// strings, loops, try/catch, calls) compiled fresh for every input, so each input's result depends
// only on that input. CBotProgram::Init() runs once (read-only afterwards).
#include "CBot/CBot.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

static constexpr int kInstrPerSlice = 100;
static constexpr int kMaxSlices = 2000;

static const char* const kProgram = R"CBOT(
public class Counter
{
    static int total = 0;
    int value = 0;
    string name = "c";
    float[] hist;
    void Counter() { hist[0] = 0.5; }
    int Add(int n) { value += n; total += n; hist[sizeof(hist)] = value; return value; }
}

int Fib(int n) { if (n < 2) return n; return Fib(n - 1) + Fib(n - 2); }

string Join(string[] parts)
{
    string out = "";
    for (int i = 0; i < sizeof(parts); ++i) { out += parts[i]; if (i < sizeof(parts) - 1) out += ","; }
    return out;
}

extern void Main()
{
    Counter c();
    Counter d = new Counter();
    int[] nums;
    string[] words;
    float acc = 0;
    for (int i = 0; i < 20; i++)
    {
        nums[i] = Fib(i % 10);
        c.Add(nums[i]);
        d.Add(i);
        acc += sqrt(abs(nums[i])) * 0.5;
        words[i] = strmid("abcdefghijklmnop", i % 8, 3) + i;
        try
        {
            if (i % 7 == 6) throw 2000 + i;
            int z = 10 / (i % 5);
        }
        catch (CBotErrZeroDiv) { acc -= 1; }
        catch (i > 0) { acc += 2; }
        finally { acc += 0.25; }
    }
    string s = Join(words);
    int p = strfind(s, "def");
    s = strupper(s) + strlower(strleft(s, 5)) + strright(s, 3) + strlen(s) + p;
    boolean b = (acc > 1 && c.total > 10) || s == "";
    do { acc *= 0.5; } while (acc > 1);
    switch (nums[5]) { case 5: acc += 1; break; default: acc -= 1; }
}
)CBOT";

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    CBot::CBotProgram::Init();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    std::vector<std::string> externs;
    auto program = std::make_unique<CBot::CBotProgram>(nullptr);
    if (!program->Compile(kProgram, externs, nullptr))
        __builtin_trap(); // the built-in program must compile; a failure is a harness bug
    std::istringstream in(std::string(reinterpret_cast<const char*>(data), size));
    if (program->RestoreState(in) && CBot::CBotClass::RestoreStaticState(in))
    {
        for (int i = 0; i < kMaxSlices; ++i)
            if (program->Run(nullptr, kInstrPerSlice))
                break;
    }
    program->Stop();
    return 0;
}
