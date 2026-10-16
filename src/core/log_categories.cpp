#include <cerfcore/core/log.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

namespace cerfcore {

uint64_t Log::ParseCategories(const char* str) {
    std::string s(str);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    if (s == "ALL")  return MASK_ALL;
    if (s == "NONE") return MASK_NONE;

    uint64_t mask = 0;
    size_t start = 0;
    while (start < s.size()) {
        size_t end = s.find(',', start);
        if (end == std::string::npos) end = s.size();
        std::string token = s.substr(start, end - start);

        if (!token.empty()) {
            bool matched = false;
            for (size_t i = 0; i < (size_t)Cat::COUNT; i++) {
                if (token == kCategories[i].slug) {
                    mask |= 1ULL << i;
                    matched = true;
                    break;
                }
            }
            if (!matched)
                fprintf(stderr, "Warning: unknown log category '%s'\n", token.c_str());
        }

        start = end + 1;
    }
    return mask;
}

void Log::PrintCategoryList() {
    printf("Log categories (use with --log= / --no-log=, comma-separated):\n");
    printf("  %-12s %s\n", "ALL",  "every category");
    printf("  %-12s %s\n", "NONE", "no categories");
    for (size_t i = 0; i < (size_t)Cat::COUNT; i++)
        printf("  %-12s %s\n", kCategories[i].slug, kCategories[i].desc);
}

}
