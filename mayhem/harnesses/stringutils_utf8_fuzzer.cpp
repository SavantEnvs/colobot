// Fuzz Colobot-Common's public StrUtils API (UTF-8 decoding/encoding, case mapping, comment
// stripping, splitting, hex parsing, path conversion). These functions are applied to every
// script, level file, translation and UI string the game handles. Invalid UTF-8 is reported by
// throwing std::invalid_argument (documented behaviour), which the harness accepts.
#include "common/codepoint.h"
#include "common/stringutils.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    std::string text(reinterpret_cast<const char*>(data), size);
    std::string_view view(text);

    volatile size_t sink = 0;
    try { sink += StrUtils::UTF8StringLength(view); } catch (const std::invalid_argument&) {}
    try { if (!view.empty()) sink += StrUtils::UTF8CharLength(view); } catch (const std::invalid_argument&) {}
    try
    {
        // Walk the string code point by code point like the text renderer does.
        std::string_view rest = view;
        while (!rest.empty())
        {
            int len = StrUtils::UTF8CharLength(rest);
            if (len <= 0 || static_cast<size_t>(len) > rest.size()) break;
            auto cp = StrUtils::ReadUTF8(rest);
            char32_t u = StrUtils::ToUTF32(cp);
            sink += StrUtils::ToLower(u) + StrUtils::ToUpper(u);
            auto back = StrUtils::ToUTF8(u);
            sink += back.Size();
            rest.remove_prefix(len);
        }
    }
    catch (const std::invalid_argument&) {}
    try
    {
        std::u32string wide = StrUtils::ToUTF32(view);
        sink += StrUtils::ToUTF8(wide).size();
    }
    catch (const std::invalid_argument&) {}
    try { sink += StrUtils::ToLower(view).size(); } catch (const std::invalid_argument&) {}
    try { sink += StrUtils::ToUpper(view).size(); } catch (const std::invalid_argument&) {}
    try { sink += StrUtils::HexStringToInt(view.substr(0, 8)); } catch (const std::invalid_argument&) {}

    std::string copy = text;
    StrUtils::RemoveComments(copy);
    StrUtils::Trim(copy);
    sink += copy.size();
    sink += StrUtils::Split(text, " ,;\n").size();
    sink += StrUtils::Replace(text, "ab", "xyz").size();

    bool ok = false;
    try
    {
        auto path = StrUtils::ToPath(view);
        sink += StrUtils::ToString(path, &ok).size();
    }
    catch (const std::exception&) {}
    (void)sink;
    return 0;
}
