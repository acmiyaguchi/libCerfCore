#pragma once

#include "../core/utf16.h"

#include <string>
#include <string_view>

static_assert(sizeof(wchar_t) == sizeof(char16_t));

inline std::wstring Utf8ToWide(const char* utf8) {
    if (utf8 == nullptr) return {};
    const std::u16string text = Utf8ToUtf16(utf8);
    return std::wstring(text.begin(), text.end());
}

inline std::string WideToUtf8(const std::wstring& wide) {
    const std::wstring_view text(wide.c_str());
    return Utf16ToUtf8(std::u16string(text.begin(), text.end()));
}

inline const wchar_t* AsWide(const std::u16string& s) {
    return reinterpret_cast<const wchar_t*>(s.c_str());
}

inline std::u16string WideToUtf16(const std::wstring& w) {
    return std::u16string(w.begin(), w.end());
}
