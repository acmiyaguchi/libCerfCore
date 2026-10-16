#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include "msm8255_clock_reset.h"
#include "msm8255_sdcc_data_path.h"
#include "msm8255_sdcc_regs.h"
#include "msm8255_sdcc_restored_register.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../irq_controller.h"
#include "../../peripherals/mmc/mmc_card.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>

#include <atomic>
#include <cstdint>

namespace cerf_msm8255_sdcc_detail {

template <uint32_t kBase, uint32_t kSize, uint32_t kResetClock,
          uint32_t kSlotIndex, uint32_t kIrqSource0, uint32_t kIrqSource1,
          uint32_t kCrci>
class Msm8255SdccWindowBase : public Peripheral {
public:
    using DataPath = Msm8255SdccDataPath<kBase, kResetClock, kCrci>;
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        data_path_ = &emu_.Get<DataPath>();
        emu_.Get<GuestCpuReset>().RegisterResetListener(
            [this](ResetLineKind) { ResetState(); });
        emu_.Get<Msm8255ClockReset>().RegisterListener(kResetClock,
                                                      [this] { ResetState(); });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - kBase;
        if (off >= kFifo && off < kFifo + kFifoBytes) {
            uint32_t events = 0u;
            const uint32_t v =
                data_path_->ReadFifo(events, [this] { return CardForSlot(); });
            LatchDataEvents(events);
            return v;
        }
        switch (off) {
        case kPower:     return Load(power_);
        case kClock:     return Load(clock_);
        case kMask0:     return Load(mask0_);
        case kMask1:     return Load(mask1_);
        case kStatus:    return Load(status_);
        case kResponse0: return Load(response_[0]);
        case kResponse1: return Load(response_[1]);
        case kResponse2: return Load(response_[2]);
        case kResponse3: return Load(response_[3]);
        default:         break;
        }
        HaltUnsupportedAccess("ReadWord", addr, 0u);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - kBase;
        if (off >= kFifo && off < kFifo + kFifoBytes) {
            LatchDataEvents(
                data_path_->WriteFifo(value, [this] { return CardForSlot(); }));
            return;
        }
        switch (off) {
        case kPower:
            if ((value & ~kPowerWritable) == 0u) {
                Store(power_, value);
                return;
            }
            break;
        case kClock:
            if ((value & ~kClockWritable) == 0u) {
                Store(clock_, value);
                return;
            }
            break;
        case kMask0:
            if ((value & ~kMaskWritable) == 0u) {
                Store(mask0_, value);
                UpdateIrq();
                return;
            }
            break;
        case kMask1:
            if ((value & ~kMaskWritable) == 0u) {
                Store(mask1_, value);
                UpdateIrq();
                return;
            }
            break;
        case kArgument:
            Store(argument_, value);
            return;
        case kCommand:
            if (value == kQuiescent) {
                return;
            }
            if ((value & kCmdEnable) != 0u && (value & ~kCmdModelled) == 0u) {
                IssueCommand(value);
                return;
            }
            break;
        case kDataTimer:
            data_path_->WriteDataTimer(value);
            return;
        case kDataLength:
            data_path_->WriteDataLength(value);
            return;
        case kDataCtrl:
            if ((value & ~kDataCtrlModelled) == 0u) {
                data_path_->StartDataPhase(value, CardForSlot());
                return;
            }
            break;
        case kClear:
            if ((value & ~kClearStaticMask) == 0u) {
                Store(status_, Load(status_) & ~value);
                UpdateIrq();
                return;
            }
            break;
        default:
            break;
        }
        HaltUnsupportedAccess("WriteWord", addr, value);
    }

    void SaveState(StateWriter& w) override {
        w.Write<uint32_t>("power", Load(power_));
        w.Write<uint32_t>("clock", Load(clock_));
        w.Write<uint32_t>("mask0", Load(mask0_));
        w.Write<uint32_t>("mask1", Load(mask1_));
        w.Write<uint32_t>("argument", Load(argument_));
        w.Write<uint32_t>("status", Load(status_));
        for (auto& word : response_) w.Write<uint32_t>("response", Load(word));
        data_path_->SaveState(w);
        if (auto* card = CardForSlot()) card->SaveState(w);
    }

    void RestoreState(StateReader& r) override {
        RestoreField(r, "power", power_, kPowerWritable, kPower);
        RestoreField(r, "clock", clock_, kClockWritable, kClock);
        RestoreField(r, "mask0", mask0_, kMaskWritable, kMask0);
        RestoreField(r, "mask1", mask1_, kMaskWritable, kMask1);
        RestoreField(r, "argument", argument_, 0xFFFFFFFFu, kArgument);
        RestoreField(r, "status", status_, kStatusLatchable, kStatus);
        for (uint32_t i = 0; i < 4u; ++i) {
            RestoreField(r, "response", response_[i], 0xFFFFFFFFu, kResponse0 + i * 4u);
        }
        data_path_->RestoreState(r, CardForSlot());
        if (auto* card = CardForSlot()) card->RestoreState(r);
    }

    void PostRestore() override {
        if (auto* card = CardForSlot()) card->PostRestore();
        data_path_->PostRestore();
        UpdateIrq();
    }

private:
    static uint32_t Load(const std::atomic<uint32_t>& reg) {
        return reg.load(std::memory_order_acquire);
    }

    static void Store(std::atomic<uint32_t>& reg, uint32_t value) {
        reg.store(value, std::memory_order_release);
    }

    MmcCard* CardForSlot() {
        auto* card = emu_.TryGet<MmcCard>();
        return (card != nullptr && card->SlotIndex() == kSlotIndex) ? card
                                                                    : nullptr;
    }

    void LatchStatus(uint32_t event) {
        Store(status_, Load(status_) | event);
        UpdateIrq();
    }

    void LatchDataEvents(uint32_t events) {
        if (events != 0u) LatchStatus(events);
    }

    void UpdateIrq() {
        const uint32_t status = Load(status_);
        auto& vic = emu_.Get<IrqController>();
        DriveLine(vic, kIrqSource0, (status & Load(mask0_)) != 0u);
        DriveLine(vic, kIrqSource1, (status & Load(mask1_)) != 0u);
    }

    static void DriveLine(IrqController& vic, uint32_t source, bool high) {
        if (high) {
            vic.AssertIrq(source);
        } else {
            vic.DeAssertIrq(source);
        }
    }

    void IssueCommand(uint32_t value) {
        const bool wants_response = (value & kCmdResponse) != 0u;
        const bool wants_long     = (value & kCmdLongRsp) != 0u;
        const bool wants_progena  = (value & kCmdProgEna) != 0u;
        const uint32_t done = wants_progena
                                  ? (kStatusCmdRespEnd | kStatusProgDone)
                                  : kStatusCmdRespEnd;

        MmcCard* card = CardForSlot();
        if (card == nullptr) {
            if (wants_progena) HaltProgEnaWithoutResponse(value);
            LatchStatus(wants_response ? kStatusCmdTimeout : kStatusCmdSent);
            return;
        }

        uint32_t resp[4] = {0u, 0u, 0u, 0u};
        const MmcCommandResult result = card->Command(
            static_cast<uint8_t>(value & kCmdIndex), Load(argument_), resp);

        switch (result) {
        case MmcCommandResult::NoResponse:
            if (wants_progena) HaltProgEnaWithoutResponse(value);
            LatchStatus(wants_response ? kStatusCmdTimeout : kStatusCmdSent);
            return;
        case MmcCommandResult::Short:
            if (wants_response && !wants_long) {
                Store(response_[0], resp[0]);
                LatchStatus(done);
                data_path_->BindDataPhase(*card);
                return;
            }
            break;
        case MmcCommandResult::Long:
            if (wants_response && wants_long) {
                for (uint32_t i = 0; i < 4u; ++i) Store(response_[i], resp[i]);
                LatchStatus(done);
                data_path_->BindDataPhase(*card);
                return;
            }
            break;
        }

        emu_.Get<Fatal>().Die(
            "Peripheral at 0x%08X: CMD%u answered with response class %u while "
            "the command word 0x%08X asked for a different one",
            kBase, static_cast<unsigned>(value & kCmdIndex),
            static_cast<unsigned>(result), value);
    }

    [[noreturn]] void HaltProgEnaWithoutResponse(uint32_t value) {
        emu_.Get<Fatal>().Die(
            "Peripheral at 0x%08X: CMD%u asks for programming-done but no card "
            "answers it (command word 0x%08X)",
            kBase, static_cast<unsigned>(value & kCmdIndex), value);
    }

    void ResetState() {
        Store(power_, kUngroundedPowerOn);
        Store(clock_, kUngroundedPowerOn);
        Store(mask0_, kUngroundedPowerOn);
        Store(mask1_, kUngroundedPowerOn);
        Store(argument_, kUngroundedPowerOn);
        Store(status_, 0u);
        for (auto& word : response_) Store(word, 0u);
        UpdateIrq();
    }

    void RestoreField(StateReader& r, const char* name, std::atomic<uint32_t>& reg,
                      uint32_t writable, uint32_t offset) {
        Store(reg, ReadRestoredRegister(r, name, writable, kBase, offset));
    }

    DataPath*             data_path_ = nullptr;
    std::atomic<uint32_t> power_{kUngroundedPowerOn};
    std::atomic<uint32_t> clock_{kUngroundedPowerOn};
    std::atomic<uint32_t> mask0_{kUngroundedPowerOn};
    std::atomic<uint32_t> mask1_{kUngroundedPowerOn};
    std::atomic<uint32_t> argument_{kUngroundedPowerOn};
    std::atomic<uint32_t> status_{0};
    std::atomic<uint32_t> response_[4]{};
};

}  // namespace cerf_msm8255_sdcc_detail
