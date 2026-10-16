#pragma once
#include "service.h"
#include "cerf_config.h"

#include <servicelocator/service_locator.h>

#include <type_traits>

namespace cerfengine { class EngineService; }

namespace cerfcore {

class CerfEmulator : public servicelocator::ServiceLocator<CerfEmulator, 2048> {
    using Locator = servicelocator::ServiceLocator<CerfEmulator, 2048>;

public:
    CerfEmulator(const CerfConfig& config, int argc, char** argv);
    ~CerfEmulator();

    template <class T>
    T& Get() {
        static_assert(!std::is_base_of_v<cerfengine::EngineService, T>,
                      "an engine service is not a CerfEmulator service");
        return Locator::Get<T>();
    }

    template <class T>
    T* TryGet() {
        static_assert(!std::is_base_of_v<cerfengine::EngineService, T>,
                      "an engine service is not a CerfEmulator service");
        return Locator::TryGet<T>();
    }

    void Boot();
    void WaitForExit();

    [[noreturn]] static void OnLocatorError(const char* message);
    static void OnServiceDestroy(const servicelocator::Service& service);

    const CerfConfig& Config() const { return config_; }
    int    Argc() const { return argc_; }
    char** Argv() const { return argv_; }

private:
    CerfConfig config_;
    int        argc_ = 0;
    char**     argv_ = nullptr;
};

#define REGISTER_SERVICE(Type) SERVICELOCATOR_REGISTER(::cerfcore::CerfEmulator, Type)
#define REGISTER_SERVICE_AS(Type, Base) \
    SERVICELOCATOR_REGISTER_AS(::cerfcore::CerfEmulator, Type, Base)
#define REGISTER_SERVICE_AS_FALLBACK(Type, Base) \
    SERVICELOCATOR_REGISTER_AS_FALLBACK(::cerfcore::CerfEmulator, Type, Base)

}
