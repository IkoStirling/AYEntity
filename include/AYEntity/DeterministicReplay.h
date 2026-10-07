#pragma once
#include <AYEntity/DeterministicSession.h>

namespace ayt::entity {
/** @brief Single-segment .rpl adapter using AYEntity's assigned event namespace.
 * @note Link AYEntity::Determinism. Seal the same session configuration first.
 * Records canonical tick inputs, post-tick field witnesses, periodic restart
 * checkpoints and an explicit completion seal. The file is capped at 256 MiB;
 * rotation/segment chaining is not part of this adapter. I/O failure stops
 * recording and cannot undo a tick already executed. Check error() immediately.
 */
class DetReplayWriter {
public:
    DetReplayWriter();
    ~DetReplayWriter();
    /// Start at nextTick=0; extensionless base paths receive .rpl before _000 rotation naming.
    bool begin(DeterministicSession& session,std::string basePath,std::uint32_t checkpointInterval=300);
    bool advance(DetTickInput input);
    bool finish();
    std::string path() const;
    const std::string& error() const;
private:
    struct Impl;std::unique_ptr<Impl> _impl;
};
/** @brief Strict session replay, checkpoint seek and first differing field diagnostics.
 * @note Missing completion seal, mismatched versions/ticks, corrupt payloads and
 * ambiguous legacy raw checkpoints are rejected. No skipping unknown records.
 * Replaying/seek uses the same session.advance() path as live input. Restore
 * may invalidate presentation pointers; obtain them again by SimEntityId.
 */
class DetReplayReader {
public:
    DetReplayReader();
    ~DetReplayReader();
    /// Validate all bounded adapter records and the completion seal before playback.
    bool open(std::string path);
    bool restoreInitial(DeterministicSession& session);
    bool advance(DeterministicSession& session);
    /// Latest checkpoint <= nextTick, followed by verified ticks up to nextTick.
    bool seek(DeterministicSession& session,std::uint64_t nextTick);
    bool atEnd() const;
    std::uint64_t tickCount() const;
    const std::string& error() const;
    const std::optional<DetStateDifference>& difference() const;
private:
    struct Impl;std::unique_ptr<Impl> _impl;
};
} // namespace ayt::entity
