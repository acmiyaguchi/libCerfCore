#pragma once

#include <string>

namespace cerfcore {

namespace host_platform {

enum class LiteralEntry { kAbsent, kNamed, kUnnamed };

LiteralEntry LookUpLiteralEntry(const std::string& dir, const std::string& name, std::string& stored);

}

}
