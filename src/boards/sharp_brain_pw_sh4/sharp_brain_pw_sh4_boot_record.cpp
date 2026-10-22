#include <cerfcore/core/service.h>

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/execution/jit_runner.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../peripherals/mmc/emmc_card_base.h"
#include "sharp_brain_pw_sh4_family_id.h"

#include <cstdint>
#include <cstring>

namespace cerfcore {

namespace {

/* sharp_brain_pw_sh4 boot_dec.bin 100bc-10198, called from 1128c. */
constexpr uint32_t kRecordPa     = 0x400EA400u;
constexpr uint32_t kRecordSector = 17u;
constexpr uint32_t kStamp        = 0x3037u;
constexpr uint32_t kOffFlags     = 4u;
constexpr uint32_t kOffSum       = 8u;
constexpr uint32_t kSumBytes     = 8u;
constexpr uint32_t kInvalid      = 0xFFFFFFFFu;

class SharpBrainPwSh4BootRecord : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpBrainPwSh4;
    }

    void OnReady() override {
        emu_.Get<JitRunner>().RegisterPreStartHook([this] { Apply(); });
        emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] {
            if (!emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) Apply();
        });
    }

private:
    void Apply() {
        uint8_t* rec =
            emu_.Get<EmulatedMemory>().TryTranslateRange(kRecordPa, cerf_mmc::kBlockBytes, true);
        if (rec == nullptr) emu_.Get<Fatal>().Die("PW-SH4 boot record: no RAM at %08X", kRecordPa);
        auto* card = dynamic_cast<EmmcCardBase*>(emu_.TryGet<MmcCard>());
        uint32_t w[3];
        if (card != nullptr) {
            card->ReadSector(kRecordSector, rec);
            std::memcpy(w, rec, sizeof(w));
            uint32_t sum = 0u;
            for (uint32_t i = 0; i < kSumBytes; ++i) sum = (sum + rec[i]) & 0xFFFFu;
            const uint32_t check = w[kOffSum / 4u];
            if ((check & 0xFFFFu) != sum || (check >> 16) != 0xFFFFu - sum) w[0] = ~kStamp;
        } else {
            w[0] = ~kStamp;
        }
        if (w[0] != kStamp) {
            w[0]              = kInvalid;
            w[kOffFlags / 4u] = kInvalid;
        }
        std::memcpy(rec, w, 2u * sizeof(uint32_t));
        LOG(Boot, "PW-SH4 boot record: LBA %u stamp %08X flags %08X\n", kRecordSector, w[0],
            w[kOffFlags / 4u]);
    }
};

}

REGISTER_SERVICE(SharpBrainPwSh4BootRecord);

}
