#include <cerfcore/state/state_stream.h>

#include <cerfcore/core/log.h>
#include "../platform/host_error.h"

#include <cstdarg>
#include <cstddef>
#include <cstdio>

namespace cerfcore {

namespace {
constexpr uint32_t kIoChunk = 0x40000000u;

const char* FieldKindName(uint8_t kind) {
    switch (kind) {
        case kStateFieldPod:   return "a value";
        case kStateFieldBytes: return "a byte block";
        default:               return "an unknown field kind";
    }
}
}

StateWriter::StateWriter(const std::string& path)
    : final_path_(path), temp_path_(path + ".tmp") {
    if (!file_.Open(temp_path_, host_platform::FileMode::kWriteNew)) {
        const uint32_t gle = host_platform::LastErrorCode();
        LOG(Caution, "StateWriter: CreateFile('%s') failed gle=%u\n",
            temp_path_.c_str(), gle);
        Fail("the state file cannot be created", gle);
        return;
    }
    ok_ = true;
}

void StateWriter::Fail(const char* what, uint32_t gle) {
    ok_ = false;
    if (!error_.empty()) return;
    error_ = gle ? std::string(what) + ": " + host_platform::ErrorText(gle) : std::string(what);
}

StateWriter::~StateWriter() {
    file_.Close();
    uint32_t ignored = 0;
    if (!committed_)
        host_platform::DeleteHostFile(temp_path_, ignored);
}

void StateWriter::WriteRaw(const void* src, size_t n) {
    if (!ok_) return;
    const auto* p = static_cast<const uint8_t*>(src);
    while (n > 0) {
        const uint32_t chunk = n > kIoChunk ? kIoChunk : static_cast<uint32_t>(n);
        uint32_t wrote = 0;
        if (!file_.Write(p, chunk, wrote) || wrote != chunk) {
            const uint32_t gle = host_platform::LastErrorCode();
            LOG(Caution, "StateWriter: WriteFile failed gle=%u (%u/%u)\n",
                gle, wrote, chunk);
            Fail("a write to the state file failed", gle);
            return;
        }
        p              += chunk;
        n              -= chunk;
        bytes_written_ += chunk;
    }
}

void StateWriter::WriteTag(uint8_t kind, const char* name, uint64_t size) {
    const StateFieldTag tag{kind, StateFieldNameHash(name), size};
    WriteRaw(&tag, sizeof(tag));
}

void StateWriter::WriteBlock(const char* name, const void* src, size_t n) {
    WriteTag(kStateFieldBytes, name, n);
    if (n) WriteRaw(src, n);
}

void StateWriter::BeginFrame(uint32_t id) {
    frames_.push_back(bytes_written_);
    const StateFrameHeader header{id, 0};
    WriteRaw(&header, sizeof(header));
}

void StateWriter::EndFrame() {
    if (frames_.empty()) {
        LOG(Caution, "StateWriter: EndFrame with no open frame\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    const uint64_t header_off = frames_.back();
    frames_.pop_back();
    const uint64_t length = bytes_written_ - header_off - sizeof(StateFrameHeader);
    PatchAt(header_off + offsetof(StateFrameHeader, length), &length, sizeof(length));
}

void StateWriter::PatchAt(uint64_t offset, const void* src, size_t n) {
    if (!ok_) return;
    if (!file_.SeekTo(offset)) {
        const uint32_t gle = host_platform::LastErrorCode();
        LOG(Caution, "StateWriter: PatchAt seek failed gle=%u\n", gle);
        Fail("a write to the state file failed", gle);
        return;
    }
    uint32_t wrote = 0;
    if (!file_.Write(src, static_cast<uint32_t>(n), wrote) || wrote != n) {
        const uint32_t gle = host_platform::LastErrorCode();
        LOG(Caution, "StateWriter: PatchAt write failed gle=%u\n", gle);
        Fail("a write to the state file failed", gle);
        return;
    }
    if (!file_.SeekToEnd()) {
        const uint32_t gle = host_platform::LastErrorCode();
        LOG(Caution, "StateWriter: PatchAt seek-back failed gle=%u\n", gle);
        Fail("a write to the state file failed", gle);
    }
}

bool StateWriter::Commit() {
    if (!frames_.empty()) {
        LOG(Caution, "StateWriter: Commit with %zu open frames\n", frames_.size());
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    uint32_t ignored = 0;
    if (!ok_) {
        file_.Close();
        host_platform::DeleteHostFile(temp_path_, ignored);
        return false;
    }
    file_.Close();
    if (!host_platform::ReplaceHostFile(temp_path_, final_path_, true)) {
        const uint32_t gle = host_platform::LastErrorCode();
        LOG(Caution, "StateWriter: MoveFileEx('%s' -> '%s') failed gle=%u\n",
            temp_path_.c_str(), final_path_.c_str(), gle);
        host_platform::DeleteHostFile(temp_path_, ignored);
        Fail("the state file cannot be replaced", gle);
        return false;
    }
    committed_ = true;
    return true;
}

StateReader::StateReader(const std::string& path) {
    if (!file_.Open(path, host_platform::FileMode::kReadExisting)) {
        error_ = "the state file cannot be opened: " +
                 host_platform::ErrorText(host_platform::LastErrorCode());
        return;
    }
    if (!file_.Size(file_size_)) {
        error_ = "the state file cannot be read: " +
                 host_platform::ErrorText(host_platform::LastErrorCode());
        return;
    }
    ok_ = true;
}

void StateReader::Reject(const char* fmt, ...) {
    ok_ = false;
    char reason[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(reason, sizeof(reason), fmt, ap);
    va_end(ap);
    std::string where = "image";
    for (const Frame& f : frames_) {
        char id[16];
        snprintf(id, sizeof(id), "/0x%X", f.id);
        where += id;
    }
    char field[96];
    snprintf(field, sizeof(field), " field %u '%s'", fields_, field_name_.c_str());
    throw StateImageRejected(where + field + ": " + reason);
}

uint64_t StateReader::Remaining() const {
    const uint64_t limit = frames_.empty() ? file_size_ : frames_.back().end;
    return limit > pos_ ? limit - pos_ : 0;
}

void StateReader::ReadRaw(void* dst, size_t n) {
    if (!ok_) Reject("the image is not open");
    if (n > Remaining())
        Reject("the saved data ends %llu bytes short of what this build reads",
               static_cast<unsigned long long>(n - Remaining()));
    auto* p = static_cast<uint8_t*>(dst);
    while (n > 0) {
        const uint32_t chunk = n > kIoChunk ? kIoChunk : static_cast<uint32_t>(n);
        uint32_t got = 0;
        if (!file_.Read(p, chunk, got) || got != chunk)
            Reject("ReadFile failed gle=%u (%u/%u)", host_platform::LastErrorCode(), got, chunk);
        p    += chunk;
        n    -= chunk;
        pos_ += chunk;
    }
}

void StateReader::ExpectTag(uint8_t kind, const char* name, uint64_t size) {
    ++fields_;
    field_name_ = name;
    StateFieldTag tag{};
    ReadRaw(&tag, sizeof(tag));
    if (tag.name != StateFieldNameHash(name))
        Reject("the saved field at this position is a different field");
    if (tag.kind != kind || tag.size != size)
        Reject("saved %s of %llu bytes, this build reads %s of %llu bytes",
               FieldKindName(tag.kind), static_cast<unsigned long long>(tag.size),
               FieldKindName(kind), static_cast<unsigned long long>(size));
}

void StateReader::ReadBlock(const char* name, void* dst, size_t n) {
    ExpectTag(kStateFieldBytes, name, n);
    if (n) ReadRaw(dst, n);
}

uint32_t StateReader::EnterFrame() {
    StateFrameHeader header{};
    ReadRaw(&header, sizeof(header));
    if (header.length > Remaining())
        Reject("frame 0x%X claims %llu bytes past the end of its parent", header.id,
               static_cast<unsigned long long>(header.length - Remaining()));
    frames_.push_back(Frame{header.id, pos_ + header.length, fields_});
    fields_     = 0;
    field_name_ = "";
    return header.id;
}

void StateReader::LeaveFrame() {
    if (frames_.empty()) {
        LOG(Caution, "StateReader: LeaveFrame with no open frame\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    if (Remaining() != 0)
        Reject("this build leaves %llu saved bytes unread",
               static_cast<unsigned long long>(Remaining()));
    fields_     = frames_.back().parent_fields;
    field_name_ = "";
    frames_.pop_back();
}

void StateReader::SkipFrame() {
    if (frames_.empty()) {
        LOG(Caution, "StateReader: SkipFrame with no open frame\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    SeekTo(frames_.back().end);
    fields_     = frames_.back().parent_fields;
    field_name_ = "";
    frames_.pop_back();
}

void StateReader::SeekTo(uint64_t offset) {
    if (!file_.SeekTo(offset))
        Reject("seek to %llu failed gle=%u", static_cast<unsigned long long>(offset),
               host_platform::LastErrorCode());
    pos_ = offset;
}

}
