#include "host_literal_entry.h"

#include "host_shared_entry.h"

#include <climits>
#include <cstdint>
#include <cstring>
#include <sys/attr.h>
#include <unistd.h>

namespace cerfcore {

namespace host_platform {

LiteralEntry LookUpLiteralEntry(const std::string& dir, const std::string& name, std::string& stored) {
    struct NameAttributes {
        uint32_t        length;
        attrreference_t name;
        char            text[NAME_MAX * 3 + 1];
    } attributes{};
    attrlist request{};
    request.bitmapcount = ATTR_BIT_MAP_COUNT;
    request.commonattr  = ATTR_CMN_NAME;
    const std::string path = JoinSharedPath(dir, name);
    if (getattrlist(path.c_str(), &request, &attributes, sizeof(attributes), FSOPT_NOFOLLOW) != 0) {
        return LiteralEntry::kAbsent;
    }
    const char* const text = reinterpret_cast<const char*>(&attributes.name) + attributes.name.attr_dataoffset;
    stored.assign(text, strnlen(text, attributes.name.attr_length));
    return LiteralEntry::kNamed;
}

}

}
