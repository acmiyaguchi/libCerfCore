#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/execution/jit_runner.h>

#include <typeinfo>

namespace cerfcore {

CerfEmulator::CerfEmulator(const CerfConfig& config, int argc, char** argv)
    : config_(config), argc_(argc), argv_(argv)
{
}

CerfEmulator::~CerfEmulator() {
    TearDown();
}

void CerfEmulator::OnLocatorError(const char* message) {
    LOG(Caution, "%s\n", message);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

void CerfEmulator::OnServiceDestroy(const servicelocator::Service& service) {
    LOG(Boot, "[TEARDOWN] destroying %s\n", typeid(service).name());
}

void CerfEmulator::Boot() {
    Bootstrap();
    LOG(Boot, "services up; starting jit...\n");
    Get<JitRunner>().Start();
}

void CerfEmulator::WaitForExit() {
    Get<JitRunner>().Join();
}

}
