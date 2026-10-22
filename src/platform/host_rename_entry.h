#pragma once

namespace cerfcore {

namespace host_platform {

int  RenameEntry(const char* from, const char* to, bool no_replace);
bool RenameNoReplaceUnsupported(int err);

}

}
