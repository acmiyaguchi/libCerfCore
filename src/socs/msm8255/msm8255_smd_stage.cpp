#include "msm8255_smd_stage.h"

#include "msm8255_ram_partitions.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>

#include <cstdint>

namespace {

constexpr uint32_t kStageAlign = 0x100000u;

}

REGISTER_SERVICE(Msm8255SmdStage);

bool Msm8255SmdStage::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

void Msm8255SmdStage::OnReady() {
    auto& parts = emu_.Get<Msm8255RamPartitions>();
    uint32_t top = 0u;
    for (uint32_t i = 0; i < parts.PartitionCount(); ++i) {
        const Msm8255RamPartition part = parts.Partition(i);
        const uint32_t end = part.start + part.size;
        if (end > top) top = end;
    }
    base_pa_ = (top + kStageAlign - 1u) & ~(kStageAlign - 1u);
    emu_.Get<EmulatedMemory>().AddRegion(base_pa_,
                                         kStageBytes + kWriteStageBytes,
                                         MemoryKind::kRam);
}

/* Linux arch/arm/mach-msm smd.c ch_read_buffer answers with the run from tail
   to the end of the fifo when tail is above head, and ch_read loops so a read
   that spans the end lands contiguous in the caller's buffer. */
uint32_t Msm8255SmdStage::Linearize(uint32_t fifo_pa, uint32_t half,
                                    uint32_t tail, uint32_t avail) {
    auto& mem = emu_.Get<EmulatedMemory>();

    const uint32_t staged = avail < kStageBytes ? avail : kStageBytes;
    const uint32_t run    = half - tail;
    const uint32_t first  = staged < run ? staged : run;

    mem.CopyOut(fifo_pa + tail, buf_, first);
    if (staged > first) {
        mem.CopyOut(fifo_pa, buf_ + first, staged - first);
    }
    mem.CopyIn(base_pa_, buf_, staged);
    return staged;
}

/* Linux arch/arm/mach-msm smd.c ch_write_buffer answers with the run from head
   to the end of the fifo, and smd_stream_write loops over those runs, so a
   write that spans the end lands in two pieces with the second at the base. */
void Msm8255SmdStage::Scatter(uint32_t ring_pa, uint32_t half, uint32_t head,
                              uint32_t bytes) {
    auto& mem = emu_.Get<EmulatedMemory>();

    if (bytes > kWriteStageBytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 smd stage: a producer wrote %u bytes into the %u-byte "
            "write stage", bytes, kWriteStageBytes);
    }

    const uint32_t run   = half - head;
    const uint32_t first = bytes < run ? bytes : run;

    mem.CopyOut(WriteBasePa(), write_buf_, bytes);
    mem.CopyIn(ring_pa + head, write_buf_, first);
    if (bytes > first) {
        mem.CopyIn(ring_pa, write_buf_ + first, bytes - first);
    }
}
