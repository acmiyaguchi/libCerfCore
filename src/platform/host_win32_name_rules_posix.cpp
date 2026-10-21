#include "host_win32_name_rules.h"

#include "host_win32_upcase_table.h"

#include <algorithm>
#include <iterator>

namespace cerfcore {

namespace host_platform {

namespace {

constexpr char16_t kDosStar = u'<';
constexpr char16_t kDosQm   = u'>';
constexpr char16_t kDosDot  = u'"';

bool IsSeparator(char16_t c) { return c == u'\\' || c == u'/'; }

void TrimSinglePeriod(std::u16string& s) {
    if (s.size() >= 2u && s.back() == u'.' && s[s.size() - 2u] != u'.') s.pop_back();
}

void TrimPeriodsAndSpaces(std::u16string& s) {
    while (!s.empty() && (s.back() == u'.' || s.back() == u' ')) s.pop_back();
}

size_t SkipQmRun(std::u16string_view mask, size_t m) {
    while (m < mask.size() && mask[m] == kDosQm) ++m;
    return m;
}

bool MatchFrom(std::u16string_view name, size_t n, std::u16string_view mask, size_t m);

bool MatchDosStar(std::u16string_view name, size_t n, std::u16string_view mask, size_t m) {
    const size_t last_dot = name.rfind(u'.');
    const bool   dotted   = last_dot != std::u16string_view::npos && last_dot >= n;
    const size_t limit    = dotted ? last_dot : name.size();
    for (size_t k = n; k <= limit; ++k) {
        if (MatchFrom(name, k, mask, m)) return true;
    }
    return dotted && last_dot + 1u == name.size() && MatchFrom(name, name.size(), mask, m);
}

bool MatchFrom(std::u16string_view name, size_t n, std::u16string_view mask, size_t m) {
    while (m < mask.size()) {
        const char16_t c = mask[m];
        if (c == u'*') {
            for (size_t k = n; k <= name.size(); ++k) {
                if (MatchFrom(name, k, mask, m + 1u)) return true;
            }
            return false;
        }
        if (c == kDosStar) return MatchDosStar(name, n, mask, m + 1u);
        if (c == kDosQm) {
            if (n == name.size()) {
                m = SkipQmRun(mask, m);
            } else if (name[n] == u'.') {
                m = SkipQmRun(mask, m);
                if (m == mask.size()) ++n;
            } else {
                ++n;
                ++m;
            }
            continue;
        }
        if (c == kDosDot) {
            if (n == name.size()) {
                ++m;
                continue;
            }
            if (name[n] != u'.') return false;
        } else if (n == name.size() || Win32Upcase(name[n]) != Win32Upcase(c)) {
            return false;
        }
        ++n;
        ++m;
    }
    return n == name.size();
}

}

Win32FullPath NormalizeWin32Path(std::vector<Win32PathSegment> base, std::u16string_view tail) {
    Win32FullPath out;
    out.segments = std::move(base);
    size_t i     = 0;
    if (!tail.empty() && !IsSeparator(tail[0])) {
        while (i < tail.size() && !IsSeparator(tail[i])) ++i;
        if (out.segments.empty()) out.segments.emplace_back();
        out.segments.back().text.append(tail.substr(0, i));
        out.segments.back().from_tail = true;
    }
    out.trailing_separator = !tail.empty() && IsSeparator(tail.back());
    while (i < tail.size()) {
        while (i < tail.size() && IsSeparator(tail[i])) ++i;
        const size_t start = i;
        while (i < tail.size() && !IsSeparator(tail[i])) ++i;
        if (start == i) break;
        const std::u16string_view segment = tail.substr(start, i - start);
        if (segment == u".") continue;
        if (segment == u"..") {
            if (!out.segments.empty()) out.segments.pop_back();
            continue;
        }
        out.segments.push_back(Win32PathSegment{std::u16string(segment), true});
    }
    for (Win32PathSegment& s : out.segments) {
        if (s.from_tail) TrimSinglePeriod(s.text);
    }
    if (!out.trailing_separator && !out.segments.empty() && out.segments.back().from_tail) {
        TrimPeriodsAndSpaces(out.segments.back().text);
        if (out.segments.back().text.empty()) {
            out.segments.pop_back();
            out.trailing_separator = true;
        }
    }
    return out;
}

char16_t Win32Upcase(char16_t c) {
    const Win32UpcaseRun* const end = std::end(kWin32UpcaseRuns);
    const Win32UpcaseRun* const run =
        std::lower_bound(std::begin(kWin32UpcaseRuns), end, c,
                         [](const Win32UpcaseRun& r, char16_t v) { return r.last < v; });
    if (run == end || c < run->first || (c - run->first) % run->step != 0) return c;
    return static_cast<char16_t>(c + run->delta);
}

bool SameWin32Name(std::u16string_view a, std::u16string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (Win32Upcase(a[i]) != Win32Upcase(b[i])) return false;
    }
    return true;
}

bool Win32NameLess(std::u16string_view a, std::u16string_view b) {
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i) {
        const char16_t ua = Win32Upcase(a[i]);
        const char16_t ub = Win32Upcase(b[i]);
        if (ua != ub) return ua < ub;
    }
    return a.size() < b.size();
}

bool IsWin32FileName(std::u16string_view name) {
    if (name.empty()) return false;
    for (const char16_t c : name) {
        if (c < 0x20u) return false;
        switch (c) {
            case u'"': case u'*': case u'/': case u':': case u'<':
            case u'>': case u'?': case u'\\': case u'|':
                return false;
            default:
                break;
        }
    }
    return true;
}

bool HasWin32Wildcard(std::u16string_view mask) {
    return mask.find_first_of(u"*?<>") != std::u16string_view::npos;
}

std::u16string FixWin32Mask(std::u16string_view mask) {
    std::u16string out(mask);
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i] == u'.' && i + 1u < out.size() && (mask[i + 1u] == u'*' || mask[i + 1u] == u'?')) {
            out[i] = kDosDot;
        } else if (out[i] == u'?') {
            out[i] = kDosQm;
        }
    }
    return out;
}

bool MatchWin32Mask(std::u16string_view name, std::u16string_view fixed_mask) {
    if (name == u"..") name = name.substr(0, 1);
    return MatchFrom(name, 0u, fixed_mask, 0u);
}

}

}
