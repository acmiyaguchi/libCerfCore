#include "../../peripherals/casio_dictionary_rom/casio_dictionary_rom.h"

#include <cerfcore/core/cerf_emulator.h>
#include "casio_jx670_family_id.h"

#include <string_view>

namespace {

class CasioJx670DictionaryRom : public CasioDictionaryRomWindow {
public:
    using CasioDictionaryRomWindow::CasioDictionaryRomWindow;

protected:
    std::string_view WindowBoardFamilyId() const override { return BoardFamilyId::CasioJx670; }
    const char*      WindowTag()     const override { return "EM500 dictionary ROM"; }
};

}

REGISTER_SERVICE(CasioJx670DictionaryRom);
