#include "msm8255_crci_bus.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>

namespace cerfcore {

REGISTER_SERVICE(Msm8255CrciBus);

bool Msm8255CrciBus::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

void Msm8255CrciBus::Register(Msm8255CrciClient* client) {
    if (client_ != nullptr) {
        emu_.Get<Fatal>().Die(
            "msm8255 crci: a second engine claims the crci lines, and sharing "
            "them between engines is not modeled");
    }
    client_ = client;
}

void Msm8255CrciBus::DeclareFifo(uint32_t crci, uint32_t pa, uint32_t bytes) {
    if (crci == 0u || crci >= kCrciCount) {
        emu_.Get<Fatal>().Die(
            "msm8255 crci: a peripheral claims crci %u, which is outside the "
            "lines this engine carries", crci);
    }
    if (fifos_[crci].bytes != 0u) {
        emu_.Get<Fatal>().Die(
            "msm8255 crci: a second peripheral claims crci %u, and sharing one "
            "line between peripherals is not modeled", crci);
    }
    fifos_[crci] = {pa, bytes};
}

bool Msm8255CrciBus::Declared(uint32_t crci) const {
    if (crci == 0u || crci >= kCrciCount) return false;
    return fifos_[crci].bytes != 0u;
}

bool Msm8255CrciBus::FifoCovers(uint32_t crci, uint32_t pa) const {
    if (crci == 0u || crci >= kCrciCount) return false;
    const Fifo& f = fifos_[crci];
    return f.bytes != 0u && pa >= f.pa && pa < f.pa + f.bytes;
}

void Msm8255CrciBus::Assert(uint32_t crci) {
    if (client_ == nullptr) {
        emu_.Get<Fatal>().Die(
            "msm8255 crci: a peripheral raised crci %u with no engine "
            "listening for it", crci);
    }
    if (crci == 0u || crci >= kCrciCount) {
        emu_.Get<Fatal>().Die(
            "msm8255 crci: a peripheral raised crci %u, which is outside the "
            "lines this engine carries", crci);
    }
    level_[crci] = true;
    client_->AssertCrci(crci);
}

void Msm8255CrciBus::Deassert(uint32_t crci) {
    if (crci == 0u || crci >= kCrciCount) {
        emu_.Get<Fatal>().Die(
            "msm8255 crci: a peripheral lowered crci %u, which is outside the "
            "lines this engine carries", crci);
    }
    level_[crci] = false;
}

bool Msm8255CrciBus::LevelHigh(uint32_t crci) const {
    if (crci == 0u || crci >= kCrciCount) return false;
    return level_[crci];
}

}
