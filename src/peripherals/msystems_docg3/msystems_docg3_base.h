#pragma once

#include <cerfcore/core/service.h>

#include <cstddef>
#include <cstdint>

/* Board-supplied wiring for the M-Systems DiskOnChip G3 controller: where its
   register window sits in physical space (the board chip-select), how many NAND
   blocks the part has, and the medium contents to back it with. The
   MsystemsDocG3 controller resolves one of these per board in OnReady. */
class MsystemsDocG3Base : public Service {
public:
    using Service::Service;

    virtual uint32_t WindowPa()   const = 0;
    virtual uint32_t BlockCount() const = 0;

    /* Fill the 0xFF-erased NAND backing with the part's medium image. A board
       with no provisioned image leaves it erased (a blank DiskOnChip). */
    virtual void LoadInto(uint8_t* nand, std::size_t size) = 0;
};
