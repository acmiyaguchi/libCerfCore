#pragma once

#include "cpu_config_database.h"
#include <cerfcore/core/service.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace cerfcore {

class CpuConfig : public Service {
public:
    using Service::Service;

    void OnReady() override;

    bool               Boolean(std::string_view id) const;
    uint32_t           Number(std::string_view id) const;
    const std::string& EnumId(std::string_view id) const;
    const std::string& Origin(std::string_view id) const;

    template <typename T, size_t N>
    T Enum(std::string_view id, const std::pair<std::string_view, T> (&table)[N]) const {
        const std::string& value = EnumId(id);
        for (const auto& [name, mapped] : table)
            if (name == value) return mapped;
        UnmappedEnum(id, value);
    }

private:
    const CpuConfigValue& Value(std::string_view id,
                                std::initializer_list<CpuConfigParamType> types) const;
    [[noreturn]] void     MissingValue(std::string_view id) const;
    [[noreturn]] void     UnmappedEnum(std::string_view id, const std::string& value) const;
    void                  ApplyUserOverrides();

    CpuConfigValues                                 values_;
    std::map<std::string, std::string, std::less<>> origins_;
};

}
