#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace cerfcore {

namespace host_platform {

struct Win32PathSegment {
    std::u16string text;
    bool           from_tail = false;
};

struct Win32FullPath {
    std::vector<Win32PathSegment> segments;
    bool                          trailing_separator = false;
};

Win32FullPath NormalizeWin32Path(std::vector<Win32PathSegment> base, std::u16string_view tail);

char16_t Win32Upcase(char16_t c);
bool     SameWin32Name(std::u16string_view a, std::u16string_view b);
bool     Win32NameLess(std::u16string_view a, std::u16string_view b);
bool     IsWin32FileName(std::u16string_view name);

bool           HasWin32Wildcard(std::u16string_view mask);
std::u16string FixWin32Mask(std::u16string_view mask);
bool           MatchWin32Mask(std::u16string_view name, std::u16string_view fixed_mask);

}

}
