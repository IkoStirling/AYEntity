#pragma once
#include <AYEntity/DeterministicSession.h>

namespace ayt::entity {
enum class DetPredictionMode : std::uint32_t { Omit, Hold, Zero };
/// Frames contain complete held state, including explicit release/zero values.
/// Unknown types and one-shot commands are omitted. Zero is valid only for an
/// application payload whose all-zero bytes represent a neutral per-tick delta.
struct DetPredictionRule {
    std::uint32_t type=0;
    DetPredictionMode mode=DetPredictionMode::Omit;
    friend bool operator==(const DetPredictionRule&,const DetPredictionRule&)=default;
};
/// Immutable fixed-roster prediction policy and canonical history byte bounds.
struct DetRollbackConfig {
    std::vector<std::uint32_t> members{1};
    std::uint32_t historyTicks=64, maxPredictionTicks=8, futureTicks=16;
    std::uint64_t maxBufferedBytes=64*1024*1024;
    std::vector<DetPredictionRule> prediction;
};
/// Identity of a committed presentation event: (epoch,tick,producer,sequence).
/// The tick is the emission tick; command.source is a stable system ID.
struct DetConfirmedEvent {
    std::uint32_t epoch=0;
    std::uint64_t tick=0;
    DetTickCommand command;
    friend bool operator==(const DetConfirmedEvent&,const DetConfirmedEvent&)=default;
};
/// Owner-thread counters only; tick lags convert to time outside authoritative Sim.
struct DetRollbackDiagnostics {
    std::uint32_t epoch=0;
    std::uint64_t head=0, confirmed=0, verified=0, oldest=0;
    std::uint64_t rollbacks=0, replayedTicks=0, lastDepth=0, maxDepth=0;
    std::uint64_t predictedTicks=0, bufferedBytes=0;
};
/** @brief Bounded input history, software-state rollback and confirmed effects.
 * @note Sole Session tick owner, on one thread at quiescent boundaries. Submit
 * complete per-member frames. Missing frames use the most recent EARLIER actual
 * frame, never a future frame or a previous guess. Late corrections replay from
 * the earliest changed tick, rebuilding subsequent guesses. All registered RNG,
 * structural changes, collision and script state are restored by Session.
 * Callback external effects are forbidden; drain takeConfirmedEvents outside Sim.
 * Execution/budget failure restores the pre-operation registered state and faults
 * this owner; external effects cannot be undone. Session outlives the owner.
 * History/prediction/configuration changes require a fresh agreed network epoch.
 */
class DeterministicRollback {
public:
    DeterministicRollback(DeterministicSession&, DetRollbackConfig={}, std::uint32_t epoch=1);
    ~DeterministicRollback();
    DeterministicRollback(const DeterministicRollback&)=delete;
    DeterministicRollback& operator=(const DeterministicRollback&)=delete;
    bool submit(std::uint32_t member, DetTickInput);
    bool ready() const;
    /// False with no fault means waiting; never advances a second clock on replay.
    bool advance();
    /// Explicit replay of retained history to the current head, without new effects.
    bool replayFrom(std::uint64_t nextTick);
    std::uint64_t confirmedNextTick() const;
    std::uint64_t oldestTick() const;
    std::uint64_t rollbackCount() const;
    std::uint64_t replayedTicks() const;
    bool hasInput(std::uint32_t member, std::uint64_t tick) const;
    std::vector<DetTickInput> actualInputs(std::uint32_t member) const;
    std::optional<DetSessionCheckpoint> checkpointAt(std::uint64_t nextTick) const;
    /// Retained complete real merged frame only; predictions are never returned.
    std::optional<DetTickInput> confirmedInputAt(std::uint64_t tick) const;
    DetRollbackDiagnostics diagnostics() const;
    /// Drain once, in tick/(producer,sequence) order. Network callers additionally
    /// bound throughNextTick by the peer-verified hash frontier. Application owns
    /// reliable delivery after draining; Session.emit remains next-tick Sim input.
    std::vector<DetConfirmedEvent> takeConfirmedEvents(std::uint64_t throughNextTick=UINT64_MAX);
    bool faulted() const;
    const std::string& error() const;
    const DetRollbackConfig& config() const;
private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
/// Agreed network identity, authority, delay and bounded transfer profile.
struct DetRollbackNetworkConfig {
    std::uint64_t sessionId=0;
    std::uint32_t epoch=1, localMember=0, recoveryMember=0, inputDelay=0;
    DetRollbackConfig rollback;
    std::uint32_t maxTransferBytes=16*1024*1024;
};
/** @brief Fixed-roster predictive networking with confirmed hashes and recovery.
 * @note Authenticated member mapping and transport/resend scheduling belong to the
 * application. No socket/wall clock enters Sim. Packet profile is distinct from
 * strict lockstep. Bootstrap inputDelay ticks with explicit neutral empty frames;
 * sample subsequent input at localInputTick(), once per forward tick, never replay.
 * Only actual-input-confirmed hashes are published; effects wait for all peer
 * hashes. Prediction stops at its horizon; peer verification also bounds progress.
 * recoveryMember is an explicitly trusted snapshot authority. It may initiate a
 * newer epoch from its latest input-confirmed checkpoint; bounded 16 KiB chunks
 * retransmit until matching new-epoch hellos. Peers validate the complete snapshot
 * before restore and wait for the whole fixed roster. No dynamic membership,
 * authentication, timeout policy or automatic choice of a recovery authority.
 * @note Recovery carries a bounded old-epoch event journal, repairing undelivered
 * effects while skipping the previously released presentation frontier.
 */
class DeterministicRollbackNetwork {
public:
    DeterministicRollbackNetwork(DeterministicSession&, DetRollbackNetworkConfig);
    ~DeterministicRollbackNetwork();
    DeterministicRollbackNetwork(const DeterministicRollbackNetwork&)=delete;
    DeterministicRollbackNetwork& operator=(const DeterministicRollbackNetwork&)=delete;
    std::uint64_t localInputTick() const;
    bool submitLocal(DetTickInput);
    bool receive(std::uint32_t authenticatedMember, std::span<const std::uint8_t> packet);
    bool ready() const;
    bool advance();
    bool synchronized() const;
    std::uint64_t confirmedNextTick() const;
    std::uint64_t verifiedNextTick() const;
    std::vector<std::uint32_t> missing() const;
    std::vector<std::vector<std::uint8_t>> packets() const;
    std::vector<DetConfirmedEvent> takeConfirmedEvents();
    /// Authority-only, latest input-confirmed checkpoint; strictly newer epoch.
    /// Explicit initiation also recovers a faulted coordinator. Pending effects
    /// are repaired; already released effects and their tick frontier never rewind.
    bool beginRecovery(std::uint32_t newEpoch);
    void disconnect(std::uint32_t member);
    bool faulted() const;
    const std::string& error() const;
    const DetRollbackNetworkConfig& config() const;
    const DeterministicRollback& history() const;
    DetRollbackDiagnostics diagnostics() const;
    /// Read-only recovery evidence for recording; never drains presentation events.
    std::uint64_t epochInitialTick() const;
    std::uint64_t recoveryJournalStart() const;
    std::vector<DetConfirmedEvent> recoveryJournal() const;
private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
} // namespace ayt::entity
