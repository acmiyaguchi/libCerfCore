#pragma once

#include <cstddef>
#include <string_view>

namespace cerf {

constexpr char AsciiLower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

constexpr bool AsciiEqualIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (AsciiLower(a[i]) != AsciiLower(b[i])) return false;
    }
    return true;
}

}
