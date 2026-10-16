#include "s3c2410_sub_source_levels.h"

#include <cerfcore/boards/board_context.h>
#include "s3c2410_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../irq_controller.h"

namespace cerfcore {

REGISTER_SERVICE(S3C2410SubSourceLevels);

bool S3C2410SubSourceLevels::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::S3c2410;
}

void S3C2410SubSourceLevels::Register(int main_source_bit, int sub_source_bit,
                                      S3C2410LevelSubSource* source) {
    std::lock_guard<std::mutex> lk(mutex_);
    for (const Entry& e : entries_) {
        if (e.sub_bit == sub_source_bit)
            emu_.Get<Fatal>().Die(
                "S3C2410 sub-source level: bit %d already has a level source",
                sub_source_bit);
    }
    entries_.push_back(Entry{main_source_bit, sub_source_bit, source});
    registered_mask_.fetch_or(1u << sub_source_bit, std::memory_order_release);
}

void S3C2410SubSourceLevels::ReassertStillHeld(uint32_t cleared_bits) {
    if ((cleared_bits & registered_mask_.load(std::memory_order_acquire)) == 0)
        return;

    std::vector<Entry> snapshot;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        snapshot = entries_;
    }

    auto& intc = emu_.Get<IrqController>();
    for (const Entry& e : snapshot) {
        if ((cleared_bits & (1u << e.sub_bit)) == 0) continue;
        if (e.source->SubSourceAsserted(e.sub_bit))
            intc.AssertSubIrq(e.main_bit, e.sub_bit);
    }
}

}
