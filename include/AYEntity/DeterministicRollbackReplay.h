#pragma once
#include <AYEntity/DeterministicRollback.h>

namespace ayt::entity {
/** @brief Observe peer-verified real input without advancing or restoring Sim.
 * @note Link AYEntity::Determinism. begin before the first tick; sync after every
 * network mutation and before recovery. Session/network outlive the writer.
 * Schema 2 .rpl stores confirmed inputs, full witnesses and explicit epoch
 * recovery boundaries. Missing old input is a declared trusted snapshot gap,
 * never presented as simulated/verified input. Recovery events preserve IDs.
 * Fresh destinations only, 256 MiB/100000 records; I/O failure is terminal.
 * finish seals the verified prefix, excluding speculative tail (head is recorded).
 */
class DetRollbackReplayWriter {
public:
    DetRollbackReplayWriter();
    ~DetRollbackReplayWriter();
    bool begin(const DeterministicRollbackNetwork&, std::string basePath, std::uint32_t checkpointInterval=300);
    bool sync(const DeterministicRollbackNetwork&);
    bool finish(const DeterministicRollbackNetwork&);
    std::string path() const;
    std::uint64_t recordedNextTick() const;
    const std::string& error() const;
private:
    struct Impl; std::unique_ptr<Impl> _impl;
};
struct DetReplaySegment {
    std::uint32_t epoch=0;
    std::uint64_t firstTick=0, endTick=0, skippedTicks=0;
};
/** @brief Verified rollback recording playback, epoch seek and field diagnosis.
 * @note open validates the whole bounded archive/seal before touching Session.
 * advance restores explicit recovery boundaries then executes one real input;
 * a terminal recovery boundary can complete without executing another tick.
 * seek(epoch,nextTick) rejects recovery gaps and clears pending playback effects;
 * seek re-execution is silent. Subsequent playback starts a new presentation
 * generation: the application must reset presentation when seeking backwards.
 * Snapshot gaps are trusted recorded recovery, not proof of skipped simulation.
 */
class DetRollbackReplayReader {
public:
    DetRollbackReplayReader();
    ~DetRollbackReplayReader();
    bool open(std::string path);
    bool restoreInitial(DeterministicSession&);
    bool advance(DeterministicSession&);
    bool seek(DeterministicSession&, std::uint32_t epoch, std::uint64_t nextTick);
    bool atEnd() const;
    std::uint64_t endTick() const;
    /// Live speculative head at seal; [endTick,head) was never recorded as real input.
    std::uint64_t speculativeHeadAtSeal() const;
    std::uint32_t epoch() const;
    const std::vector<DetReplaySegment>& segments() const;
    std::vector<DetConfirmedEvent> takeConfirmedEvents();
    const std::string& error() const;
    const std::optional<DetStateDifference>& difference() const;
private:
    struct Impl; std::unique_ptr<Impl> _impl;
};
} // namespace ayt::entity
