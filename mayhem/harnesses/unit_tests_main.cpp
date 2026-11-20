// gtest main for upstream's CBot + stringutils unit tests (test/src/main.cpp minus the
// Colobot-Base logger, which these tests do not use).
#include <gtest/gtest.h>

#include <clocale>
#include <string>

extern bool g_cbotTestSaveState;

int main(int argc, char* argv[])
{
    ::testing::InitGoogleTest(&argc, argv);
    setlocale(LC_ALL, "C.UTF-8");
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--CBotUT_TestSaveState")
            g_cbotTestSaveState = true;
    return RUN_ALL_TESTS();
}
