#include "siemens_simpad_boot.h"

#include <cerfcore/boards/board_context.h>
#include "siemens_simpad_family_id.h"
#include "../../boot/board_boot_placer.h"
#include <cerfcore/boot/rom_parser_queries.h>
#include <cerfcore/boot/rom_parser_service.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>

#include <cstdint>
#include <span>

namespace {

class SiemensSimpadBootPlacer : public BoardBootPlacer {
public:
    using BoardBootPlacer::BoardBootPlacer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimpad;
    }

    void PlaceAfterRom() override {
        auto& parser = emu_.Get<RomParserService>();
        if (!parser.Ok() || parser.Primary().xips.empty()) {
            LOG(Caution, "SiemensSimpadBootPlacer: no parsed XIP; cannot place "
                         "boot head copy-source\n");
            return;
        }

        const uint32_t physfirst =
            parser.Primary().xips[0].toc.romhdr.physfirst;
        std::span<const uint8_t> head =
            emu_.Get<RomParserQueries>().ReadVa(physfirst,
                                               siemens_simpad::kHeadLen);
        if (head.size() < siemens_simpad::kHeadLen) {
            LOG(Caution, "SiemensSimpadBootPlacer: head VA 0x%08X len 0x%X not "
                         "fully in any partition (got 0x%zX)\n",
                physfirst, siemens_simpad::kHeadLen, head.size());
            return;
        }

        emu_.Get<EmulatedMemory>().CopyIn(
            siemens_simpad::kHeadCopySrcPa, head.data(), head.size());
        LOG(Boot, "SiemensSimpadBootPlacer: image head 0x%X bytes -> flash "
                  "copy-source pa=0x%08X (boot stub copies it to DRAM)\n",
            siemens_simpad::kHeadLen, siemens_simpad::kHeadCopySrcPa);
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(SiemensSimpadBootPlacer, BoardBootPlacer);
