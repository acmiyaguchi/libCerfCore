#pragma once

#include "pxa2xx_dma.h"

#include <atomic>
#include <cstdint>
#include <thread>

class Pxa2xxDmaMoves : public Pxa2xxDma {
public:
    using Pxa2xxDma::Pxa2xxDma;

    void OnPortChange() override;

protected:
    void RunUnboundLocked(uint32_t ch) override;
    void StopUnboundLocked(uint32_t ch) override;
    bool UnboundBusyLocked(uint32_t ch) const override { return phase_[ch] != Phase::Idle; }
    bool UnboundWaitingLocked(uint32_t ch) const override { return phase_[ch] == Phase::Wait; }
    void ResetUnboundLocked() override;
    void SaveUnbound(StateWriter& w) override;
    void RestoreUnbound(StateReader& r) override;
    void BeginMoveLocked(uint32_t ch, uint64_t at) override;
    void StopMoveLocked(uint32_t ch) override;
    void RequestChangedLocked(uint32_t ch, uint64_t now) override;
    uint64_t WaitingMoveRiseLocked(uint64_t now) override;
    void WaitUnboundLocked(uint32_t ch) override;
    void ReleaseUnboundWaitLocked(uint32_t ch) override;
    bool ReadDescriptorWordsLocked(uint32_t pa, uint32_t (&w)[4]) override;

    virtual bool MoveBusErrorLocked(uint32_t pa, bool write) = 0;
    virtual bool DescriptorInMemoryOnly() const = 0;

    virtual uint32_t MoveAddressLocked(uint32_t ch, uint32_t pa) { (void)ch; return pa; }
    virtual bool     DiscardsEmptyDescriptor() const = 0;
    virtual bool     CompareEnabled(uint32_t dcmd) const { (void)dcmd; return false; }
    virtual bool     CompareImmediate(uint32_t dcmd) const { (void)dcmd; return false; }
    virtual void     SetCompareStatusLocked(uint32_t ch, bool equal) { (void)ch; (void)equal; }

private:
    enum class Phase : uint8_t { Idle, Fetch, Read, Write, Compare, Wait, Done, Discard };
    static constexpr uint32_t kStageBytes = 32u;

    static bool     Moving(Phase phase) { return phase != Phase::Idle && phase != Phase::Wait; }
    static uint32_t UnitWidth(uint32_t dcmd);
    static uint32_t SizeBytes(uint32_t dcmd);

    bool BusAccess(uint32_t pa, uint32_t width, bool write, uint32_t& value);
    bool ReadUnit(uint32_t pa, uint32_t width, uint32_t& value);
    bool WriteUnit(uint32_t pa, uint32_t width, uint32_t value);

    void DriveLocked(uint32_t ch);
    void StepLocked(uint32_t ch);
    void BeginFetchLocked(uint32_t ch);
    void LoadFetchedLocked(uint32_t ch);
    void DescriptorLoadedLocked(uint32_t ch);
    bool AwaitRequestLocked(uint32_t ch);
    void StartTransferLocked(uint32_t ch);
    void BeginChunkLocked(uint32_t ch);
    void ReadChunkLocked(uint32_t ch);
    void WriteChunkLocked(uint32_t ch);
    void CompareLocked(uint32_t ch);
    void DescriptorDoneLocked(uint32_t ch);
    void DiscardLocked(uint32_t ch);
    void BusErrorLocked(uint32_t ch);
    void FinishLocked(uint32_t ch);
    void EndLocked(uint32_t ch, MoveEnd end);

    Phase    phase_[kMaxChannels]     = {};
    bool     driving_[kMaxChannels]   = {};
    uint32_t chunk_[kMaxChannels]     = {};
    uint8_t  staged_[kMaxChannels][kStageBytes] = {};
    std::atomic<std::thread::id> access_thread_{};
};
