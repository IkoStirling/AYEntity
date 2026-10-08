#pragma once
#include <AYEntity/DeterministicRollback.h>

namespace ayt::entity {
/// Opt-in indexed archive. Limits include each segment's initial snapshot and
/// reserve its completion seal; oversized indivisible records fail explicitly.
struct DetRollbackReplayArchiveOptions {
    /// 1024..256 MiB, including initial checkpoint and reserved seal/end markers.
    std::uint64_t maxSegmentBytes=64*1024*1024;
    /// 2..99996 initial/input/recovery records per file; completion seal is excluded.
    std::uint32_t maxSegmentRecords=10000;
    /// 1..65536 physical files, also subject to the 16 MiB archive index budget.
    std::uint32_t maxSegments=65536;
};
struct DetReplayFileSegment {
    std::string path;
    std::uint64_t storedBytes=0,fileHash=0,firstTick=0,endTick=0,initialHash=0,finalHash=0,records=0;
    std::uint32_t firstEpoch=0,lastEpoch=0;
};
/** @brief Observe peer-verified real input without advancing or restoring Sim.
 * @note Link AYEntity::Determinism. begin before the first tick; sync after every
 * network mutation and before recovery. Session/network outlive the writer.
 * Schema 2 .rpl stores confirmed inputs, full witnesses and explicit epoch
 * recovery boundaries. Missing old input is a declared trusted snapshot gap,
 * never presented as simulated/verified input. Recovery events preserve IDs.
 * Fresh destinations only; single files are bounded at 256 MiB/100000 records.
 * Archive options rotate between records; .rpi is published only after finish.
 * I/O or indivisible record budget failure is terminal; no implicit prefix salvage.
 * finish seals the verified prefix, excluding speculative tail (head is recorded).
 */
class DetRollbackReplayWriter {
public:
    DetRollbackReplayWriter();
    ~DetRollbackReplayWriter();
    bool begin(const DeterministicRollbackNetwork&, std::string basePath, std::uint32_t checkpointInterval=300);
    /// Indexed .rpi + independently sealed schema2 .rpl segments. Rotation is
    /// between complete records; path() remains the stable index path. No overwrite.
    bool begin(const DeterministicRollbackNetwork&, std::string basePath,
        DetRollbackReplayArchiveOptions, std::uint32_t checkpointInterval=300);
    bool sync(const DeterministicRollbackNetwork&);
    bool finish(const DeterministicRollbackNetwork&);
    std::string path() const;
    std::uint64_t recordedNextTick() const;
    std::uint32_t fileSegmentCount() const;
    const std::string& error() const;
private:
    struct Impl; std::unique_ptr<Impl> _impl;
};
struct DetReplaySegment {
    std::uint32_t epoch=0;
    std::uint64_t firstTick=0, endTick=0, skippedTicks=0;
};
enum class DetReplayArchiveStop : std::uint32_t { MissingSegment=1, InvalidSegment=2, Discontinuity=3, Budget=4 };
/// Recovery never asserts that the original live session ended here.
struct DetReplayArchiveRecovery {
    DetReplayArchiveStop reason=DetReplayArchiveStop::MissingSegment;
    std::uint32_t stopOrdinal=0;
};
struct DetReplayFileIssue {
    std::string path;
    /// Physical container/index record offset, when available.
    std::optional<std::uint64_t> offset;
};
/** @brief Verified rollback recording playback, epoch seek and field diagnosis.
 * @note open validates .rpl completely or the .rpi seal/index and first file.
 * Further files are hash/shape/manifest/continuity checked before use (one retained,
 * two temporarily during replacement). advance executes input/recovery once.
 * seek(epoch,nextTick) locates the indexed file, rejects recovery gaps and silently
 * re-executes; it clears playback effects. Reset presentation on backward seek.
 * Snapshot gaps are trusted recovery, not proof of skipped simulation.
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
    /// Physical files from .rpi, distinct from logical recovery epoch segments.
    const std::vector<DetReplayFileSegment>& files() const;
    /// Present for profile2 recovered prefixes; head is only the retained seal's head.
    const std::optional<DetReplayArchiveRecovery>& recovery() const;
    const std::optional<DetReplayFileIssue>& issue() const;
    std::vector<DetConfirmedEvent> takeConfirmedEvents();
    const std::string& error() const;
    const std::optional<DetStateDifference>& difference() const;
private:
    friend class DetRollbackReplayArchive;
    struct Impl; std::unique_ptr<Impl> _impl;
};
struct DetReplayArchiveReport {
    bool valid=false;
    std::string path,error;
    std::optional<DetReplayFileIssue> issue;
    std::optional<DetReplayArchiveRecovery> recovery;
    std::uint64_t speculativeHead=0;
    std::vector<DetReplayFileSegment> files;
    std::vector<DetReplaySegment> segments;
};
/** @brief Offline archive inspection and explicit recovery/index reconstruction.
 * @note inspect checks every file, without executing Sim. scan derives ordinal paths
 * from .rpi/.rpi.partial and stops at the first missing/invalid/discontinuous segment.
 * rebuildIndex copies the usable prefix into a NEW directory and publishes a profile2
 * recovered index only after checking every copy. Source files are never modified.
 * Input must be quiescent; no unsealed record salvage, skipping gaps or authentication.
 * A recovered prefix is not proof that the original live session ended there.
 */
class DetRollbackReplayArchive {
public:
    static DetReplayArchiveReport inspect(const std::string& path);
    static DetReplayArchiveReport scan(const std::string& sourceIndex);
    static DetReplayArchiveReport rebuildIndex(const std::string& sourceIndex,const std::string& newDirectory);
};
} // namespace ayt::entity
