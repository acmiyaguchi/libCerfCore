#include "../src/storage/disk_image.h"

#include <cerfcore/platform/host_file.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kSector  = cerfcore::DiskImage::kSectorSize;
constexpr uint32_t kSectors = 64;

void Check(bool ok, const char* name, int& failures) {
    if (ok) return;
    std::printf("FAIL %s\n", name);
    ++failures;
}

std::vector<uint8_t> Pattern(uint8_t seed, uint32_t sectors) {
    std::vector<uint8_t> v(sectors * kSector);
    for (size_t i = 0; i < v.size(); i++) v[i] = static_cast<uint8_t>(seed + i * 7u);
    return v;
}

bool WriteFile(const std::string& path, const std::vector<uint8_t>& data) {
    cerfcore::host_platform::HostFile f;
    uint32_t                          put = 0;
    return f.Open(path, cerfcore::host_platform::FileMode::kWriteNew) &&
           f.Write(data.data(), static_cast<uint32_t>(data.size()), put) && put == data.size();
}

std::vector<uint8_t> ReadFile(const std::string& path) {
    cerfcore::host_platform::HostFile f;
    uint64_t                          size = 0;
    if (!f.Open(path, cerfcore::host_platform::FileMode::kReadExisting) || !f.Size(size)) return {};
    std::vector<uint8_t> v(static_cast<size_t>(size));
    uint32_t             got = 0;
    if (!f.Read(v.data(), static_cast<uint32_t>(v.size()), got)) return {};
    return v;
}

std::vector<uint8_t> Slice(const std::vector<uint8_t>& v, uint32_t lba, uint32_t count) {
    return {v.begin() + lba * kSector, v.begin() + (lba + count) * kSector};
}

}

int main() {
    int failures = 0;
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / fs::path(u8"cerf_overlay_ü日本");
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string base    = reinterpret_cast<const char*>((dir / "base.img").u8string().c_str());
    const std::string overlay = reinterpret_cast<const char*>((dir / "base.cow").u8string().c_str());

    const std::vector<uint8_t> original = Pattern(0x11, kSectors);
    Check(WriteFile(base, original), "create base", failures);

    const std::vector<uint8_t> patch = Pattern(0xA0, 6);
    std::vector<uint8_t>       expect = original;
    {
        cerfcore::DiskImage disk;
        Check(disk.OpenOverlay(base, overlay, 0), "open overlay", failures);
        Check(disk.SectorCount() == kSectors, "sector count from base", failures);

        std::vector<uint8_t> buf(kSectors * kSector);
        Check(disk.ReadSectors(0, kSectors, buf.data()) && buf == original, "fresh reads base",
              failures);

        Check(disk.WriteSectors(5, 6, patch.data()), "write across map byte", failures);
        std::copy(patch.begin(), patch.end(), expect.begin() + 5 * kSector);
        Check(disk.PunchHole(20, 2), "punch hole", failures);
        std::fill(expect.begin() + 20 * kSector, expect.begin() + 22 * kSector, uint8_t{0});

        Check(disk.ReadSectors(0, kSectors, buf.data()) && buf == expect, "mixed read", failures);
        Check(disk.ReadSectors(4, 3, buf.data()) &&
                  std::vector<uint8_t>(buf.begin(), buf.begin() + 3 * kSector) == Slice(expect, 4, 3),
              "partial run read", failures);
        Check(!disk.ReadSectors(kSectors - 1, 2, buf.data()), "out of range read", failures);
    }

    Check(ReadFile(base) == original, "base unchanged", failures);

    {
        cerfcore::DiskImage reopened;
        std::vector<uint8_t> buf(kSectors * kSector);
        Check(reopened.OpenOverlay(base, overlay, 0), "reopen overlay", failures);
        Check(reopened.ReadSectors(0, kSectors, buf.data()) && buf == expect, "overlay persists",
              failures);
    }

    fs::remove_all(dir);
    if (failures == 0) std::printf("disk_image_overlay_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
