#include <cerfcore/core/utf16.h>

namespace {

constexpr char32_t kReplacement = 0xFFFD;

/* Unicode 15.0 § 3.9 Table 3-7 (well-formed byte ranges) and D93b: an
   ill-formed sequence advances by its maximal subpart. */
size_t DecodeUtf8(std::string_view s, size_t i, char32_t& cp) {
    const unsigned char b0 = static_cast<unsigned char>(s[i]);
    if (b0 <= 0x7F) {
        cp = b0;
        return 1;
    }
    size_t len = 0;
    unsigned char lo = 0x80;
    unsigned char hi = 0xBF;
    if (b0 >= 0xC2 && b0 <= 0xDF) {
        len = 2;
        cp  = b0 & 0x1F;
    } else if (b0 >= 0xE0 && b0 <= 0xEF) {
        len = 3;
        cp  = b0 & 0x0F;
        if (b0 == 0xE0) lo = 0xA0;
        if (b0 == 0xED) hi = 0x9F;
    } else if (b0 >= 0xF0 && b0 <= 0xF4) {
        len = 4;
        cp  = b0 & 0x07;
        if (b0 == 0xF0) lo = 0x90;
        if (b0 == 0xF4) hi = 0x8F;
    } else {
        cp = kReplacement;
        return 1;
    }
    for (size_t k = 1; k < len; ++k) {
        if (i + k >= s.size()) {
            cp = kReplacement;
            return k;
        }
        const unsigned char b = static_cast<unsigned char>(s[i + k]);
        const unsigned char min = (k == 1) ? lo : 0x80;
        const unsigned char max = (k == 1) ? hi : 0xBF;
        if (b < min || b > max) {
            cp = kReplacement;
            return k;
        }
        cp = (cp << 6) | (b & 0x3F);
    }
    return len;
}

/* Unicode 15.0 § 3.9 Table 3-6 "UTF-8 Bit Distribution". */
void AppendUtf8(std::string& out, char32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

}

/* Unicode 15.0 § 3.9 Table 3-5 "UTF-16 Bit Distribution". */
std::u16string Utf8ToUtf16(std::string_view utf8) {
    std::u16string out;
    out.reserve(utf8.size());
    size_t i = 0;
    while (i < utf8.size()) {
        char32_t cp = 0;
        i += DecodeUtf8(utf8, i, cp);
        if (cp >= 0x10000) {
            const char32_t v = cp - 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (v >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (v & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
    }
    return out;
}

/* Unicode 15.0 § 3.9 D91: an isolated D800..DFFF code unit is ill-formed;
   D93b replaces it with one U+FFFD. */
std::string Utf16ToUtf8(std::u16string_view utf16) {
    std::string out;
    out.reserve(utf16.size());
    for (size_t i = 0; i < utf16.size(); ++i) {
        const char16_t u = utf16[i];
        char32_t cp = u;
        if (IsHighSurrogate(u) && i + 1 < utf16.size() && IsLowSurrogate(utf16[i + 1])) {
            cp = 0x10000 + ((char32_t(u) - 0xD800) << 10) + (char32_t(utf16[i + 1]) - 0xDC00);
            ++i;
        } else if (IsHighSurrogate(u) || IsLowSurrogate(u)) {
            cp = kReplacement;
        }
        AppendUtf8(out, cp);
    }
    return out;
}
