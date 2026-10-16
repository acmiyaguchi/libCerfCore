#include "share_folder_mount_name.h"

#include <cerfcore/core/utf16.h>

#include <string_view>

namespace cerfcore {

/* smartbook_g138_ce4_2 filesys.exe 0x127D4: the RegisterAFSName name check. */
bool IsValidShareFolderMountName(const std::string& utf8) {
    const std::u16string name = Utf8ToUtf16(utf8);
    if (name.empty() || name.size() > kShareFolderMountNameMaxWchars)
        return false;
    constexpr std::u16string_view kInvalid = u"\\/:*?\"<>|";
    for (char16_t c : name) {
        if (c < 0x20 || kInvalid.find(c) != std::u16string_view::npos)
            return false;
    }
    return true;
}

}
