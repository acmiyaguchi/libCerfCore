#include "host_file_mapping.h"

#include "host_unimplemented.h"

namespace cerfcore {

namespace host_platform {

bool OpenMappedFile(const std::string&, FileMapping&) { NoHostImplementation("OpenMappedFile"); }
void CloseMappedFile(FileMapping&) { NoHostImplementation("CloseMappedFile"); }

uint8_t* MapFileView(const FileMapping&, uint64_t, size_t) {
    NoHostImplementation("MapFileView");
}

void UnmapFileView(uint8_t*) { NoHostImplementation("UnmapFileView"); }

}

}
