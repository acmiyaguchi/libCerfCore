#pragma once

#include <string>
#include <string_view>

namespace cerfcore {

std::u16string Utf8ToUtf16(std::string_view utf8);
std::string    Utf16ToUtf8(std::u16string_view utf16);

/* Unicode 15.0 § 3.9 Table 3-5: 110110wwwwxxxxxx 110111xxxxxxxxxx */
constexpr bool IsHighSurrogate(char16_t u) { return u >= 0xD800 && u <= 0xDBFF; }
constexpr bool IsLowSurrogate(char16_t u)  { return u >= 0xDC00 && u <= 0xDFFF; }

}
