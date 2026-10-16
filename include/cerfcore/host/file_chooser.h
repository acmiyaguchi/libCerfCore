#pragma once

#include "../core/service.h"

#include <string>
#include <vector>

namespace cerfcore {

struct FileChooserFilter {
    std::string description;
    std::string patterns;
};

class FileChooser : public Service {
public:
    using Service::Service;

    virtual std::string ChooseFile(const std::string& title,
                                   const std::vector<FileChooserFilter>& filters) = 0;
    virtual std::vector<std::string> ChooseFiles(const std::string& title,
                                                 const std::vector<FileChooserFilter>& filters) = 0;
};

}
