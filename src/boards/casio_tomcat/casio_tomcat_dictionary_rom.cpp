#include "../../peripherals/casio_dictionary_rom/casio_dictionary_rom.h"

#include <cerfcore/core/cerf_emulator.h>
#include "casio_tomcat_family_id.h"

#include <string_view>

namespace cerfcore {

namespace {

class CasioTomcatDictionaryRom : public CasioDictionaryRomWindow {
public:
    using CasioDictionaryRomWindow::CasioDictionaryRomWindow;

protected:
    std::string_view WindowBoardFamilyId() const override { return BoardFamilyId::CasioTomcat; }
    const char*      WindowTag()     const override { return "E55 dictionary ROM"; }
};

}

REGISTER_SERVICE(CasioTomcatDictionaryRom);

}
