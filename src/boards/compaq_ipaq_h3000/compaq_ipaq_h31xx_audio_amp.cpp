#include "compaq_ipaq_h3000_audio_amp.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include "../../socs/sa11xx/sa11xx_gpio.h"
#include "ipaq_h31xx_id.h"

#include <atomic>
#include <cstdint>

namespace {

/* GPIO_H3100_AUD_ON = GPIO 6: Linux v2.6.14 include/asm-arm/arch-sa1100/
   h3600_gpio.h. Bundle ipaq_h3100_ppc2002, wavedev.dll sub_F557BC sets it. */
constexpr uint32_t kAudOn = 1u << 6;

class CompaqIpaqH31xxAudioAmp : public CompaqIpaqH3000AudioAmp {
public:
    using CompaqIpaqH3000AudioAmp::CompaqIpaqH3000AudioAmp;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardId() == BoardId::IpaqH31xx;
    }

    void OnReady() override {
        emu_.Get<Sa11xxGpio>().RegisterOutputObserver(
            [this](uint32_t levels, uint32_t out_mask, bool resync) {
                const bool on = (levels & out_mask & kAudOn) != 0u;
                if (powered_.exchange(on, std::memory_order_acq_rel) == on && !resync)
                    return;
                LOG(Periph, "[CompaqIpaqH31xxAudioAmp] GPIO 6 %s -> audio amp %s\n",
                    (out_mask & kAudOn) ? "output" : "not output", on ? "on" : "off");
            });
    }

    bool Powered() const override {
        return powered_.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> powered_{false};
};

}

REGISTER_SERVICE_AS(CompaqIpaqH31xxAudioAmp, CompaqIpaqH3000AudioAmp);
