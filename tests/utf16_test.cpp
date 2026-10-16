#include <cerfcore/core/utf16.h>

#include <cstdio>
#include <initializer_list>
#include <string>

namespace {

std::string Bytes(std::initializer_list<unsigned char> bytes) {
    return std::string(bytes.begin(), bytes.end());
}

void Check(bool ok, const char* name, int& failures) {
    if (ok) return;
    std::printf("FAIL %s\n", name);
    ++failures;
}

}

int main() {
    int failures = 0;
    constexpr char16_t kRep = 0xFFFD;

    /* Unicode 15.0 § 3.9 D91 and D92 examples */
    const std::u16string d91 = {0x004D, 0x0430, 0x4E8C, 0xD800, 0xDF02};
    const std::string    d92 = Bytes({0x4D, 0xD0, 0xB0, 0xE4, 0xBA, 0x8C, 0xF0, 0x90, 0x8C, 0x82});
    Check(Utf8ToUtf16(d92) == d91, "D92 to UTF-16", failures);
    Check(Utf16ToUtf8(d91) == d92, "D91 to UTF-8", failures);

    /* Unicode 15.0 § 3.9 Constraints on Conversion Processes, <C2 41 42> */
    Check(Utf8ToUtf16(Bytes({0xC2, 0x41, 0x42})) == std::u16string{kRep, 0x0041, 0x0042},
          "C2 41 42", failures);

    /* Unicode 15.0 § 3.9 Table 3-8 */
    Check(Utf8ToUtf16(Bytes({0xC0, 0xAF, 0xE0, 0x80, 0xBF, 0xF0, 0x81, 0x82, 0x41})) ==
              std::u16string{kRep, kRep, kRep, kRep, kRep, kRep, kRep, kRep, 0x0041},
          "Table 3-8", failures);

    /* Unicode 15.0 § 3.9 Table 3-9 */
    Check(Utf8ToUtf16(Bytes({0xED, 0xA0, 0x80, 0xED, 0xBF, 0xBF, 0xED, 0xAF, 0x41})) ==
              std::u16string{kRep, kRep, kRep, kRep, kRep, kRep, kRep, kRep, 0x0041},
          "Table 3-9", failures);

    /* Unicode 15.0 § 3.9 Table 3-10 */
    Check(Utf8ToUtf16(Bytes({0xF4, 0x91, 0x92, 0x93, 0xFF, 0x41, 0x80, 0xBF, 0x42})) ==
              std::u16string{kRep, kRep, kRep, kRep, kRep, 0x0041, kRep, kRep, 0x0042},
          "Table 3-10", failures);

    /* Unicode 15.0 § 3.9 Table 3-11 */
    Check(Utf8ToUtf16(Bytes({0xE1, 0x80, 0xE2, 0xF0, 0x91, 0x92, 0xF1, 0xBF, 0x41})) ==
              std::u16string{kRep, kRep, kRep, kRep, 0x0041},
          "Table 3-11", failures);

    /* Unicode 15.0 § 3.9 D91: an isolated D800..DFFF code unit is ill-formed */
    Check(Utf16ToUtf8(std::u16string{0xD800, 0x0041}) == Bytes({0xEF, 0xBF, 0xBD, 0x41}),
          "isolated surrogate", failures);

    /* Unicode 15.0 § 3.9 Table 3-5 */
    Check(!IsHighSurrogate(0xD7FF) && IsHighSurrogate(0xD800) && IsHighSurrogate(0xDBFF) &&
              !IsHighSurrogate(0xDC00),
          "high surrogate range", failures);
    Check(!IsLowSurrogate(0xDBFF) && IsLowSurrogate(0xDC00) && IsLowSurrogate(0xDFFF) &&
              !IsLowSurrogate(0xE000),
          "low surrogate range", failures);

    if (failures == 0) std::printf("utf16_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
