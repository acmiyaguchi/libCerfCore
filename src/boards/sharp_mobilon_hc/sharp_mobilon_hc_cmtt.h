#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

/* Cmtt storage-companion data/command port at CS3+0x00 (PA 0x10800000, shared
   window with the OEM debug 16550). Guest driver: nk.exe present-check
   sub_91019CD0 (getchar()==a1 && read4()==0x1A0AA55A), consumer file-open
   sub_910046AC. */
class SharpMobilonHcCmtt : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    /* CS3+0x00, 32-bit. */
    void     WriteDataPort(uint32_t value);
    uint32_t ReadDataPort();

    /* CS3+0x03, 8-bit (sub_910236E4 status read). */
    uint8_t ReadStatusByte();

    /* MFIODIN bit 16 read-data-ready, polled by sub_910235E0. */
    bool ReadReady() const;

private:
    /* sub_910236E4 polls CS3+0x00 bit 0x200 SET after 0x17, CLEAR after data. */
    bool write_ready_ = false;
};

}
