#pragma once
#include <AYEntity/DeterministicRollbackReplay.h>

namespace ayt::entity {
enum class DetReplayRegressionResult {
    Verified, VerifiedWithGaps, Equal, ManifestMismatch, InitialStateMismatch,
    InputMismatch, StateMismatch, EventMismatch, RecoveryMismatch, RangeMismatch,
    InvalidFile, ExecutionFailed, BudgetExceeded
};
struct DetReplayRegressionOptions {
    /// Logical records, excluding duplicate physical-file boundary checkpoints.
    std::uint64_t maxRecords=1000000;
    /// Conservative decoded artifact budget; 1024..256 MiB.
    std::uint64_t maxArtifactBytes=64*1024*1024;
};
struct DetReplayInputDifference {
    std::uint32_t source=0,sequence=0;
    std::string field;
    std::uint64_t expected=0,actual=0;
    std::optional<std::uint64_t> payloadOffset;
};
struct DetReplayRegressionReport {
    DetReplayRegressionResult result=DetReplayRegressionResult::InvalidFile;
    std::string left,right,error;
    std::uint32_t epoch=0;
    std::uint64_t tick=0,nextTick=0,record=0,recordsChecked=0,ticksExecuted=0,eventsChecked=0,skippedTicks=0;
    std::uint64_t leftRecordHash=0,rightRecordHash=0;
    std::uint64_t leftBeforeRecordHash=0,rightBeforeRecordHash=0;
    bool leftRecordPresent=false,rightRecordPresent=false;
    std::uint64_t manifestHash=0,rightManifestHash=0;
    std::uint64_t trustedRecoveryEvents=0;
    std::optional<DetStateDifference> difference;
    std::optional<DetReplayInputDifference> inputDifference;
    std::optional<DetReplayFileIssue> issue;
    std::optional<DetReplayArchiveRecovery> leftRecovery,rightRecovery;
    /// Canonical actual checkpoint on execution divergence, absent on callback fault.
    std::vector<std::uint8_t> actualCheckpoint;
    bool success() const;
};
using DetReplaySessionFactory=std::function<std::unique_ptr<DeterministicSession>()>;

/** @brief Execute replay regressions, compare recorded witnesses and export short repros.
 * @note Link AYEntity::Determinism. verify owns a fresh, configured/sealed Session
 * supplied by the application; captured World/context must outlive it. Recorded
 * recovery snapshots are trusted, skipped ticks are reported, never executed.
 * compare streams logical records in order, independent of physical segmentation;
 * equal means equal recorded input/state/events, not an executable proof.
 * writeArtifacts uses a NEW directory and sealed schema2 one-record repro .rpl files;
 * input failures can be rerun with the same application factory. It does not claim
 * hidden external state can be reproduced. Sources must remain immutable; existing
 * file bounds and explicit global record/artifact budgets apply. No authentication.
 */
class DetReplayRegression {
public:
    static DetReplayRegressionReport verify(const std::string&,const DetReplaySessionFactory&,DetReplayRegressionOptions={});
    static DetReplayRegressionReport compare(const std::string&,const std::string&,DetReplayRegressionOptions={});
    /// Includes report.json; failures additionally retain preceding checkpoint and
    /// failing input/recovery/expected state, optional peer case and actual checkpoint.
    static bool writeArtifacts(const DetReplayRegressionReport&,const std::string& newDirectory,std::string& error,DetReplayRegressionOptions={});
    static std::string toJson(const DetReplayRegressionReport&);
    /// Application executable protocol: --ayreplay-verify SOURCE NEW_DIRECTORY.
    /// JSON stdout; exit0 success,1 regression,2 arguments,3 artifact I/O failure.
    static int runnerMain(int argc,char** argv,const DetReplaySessionFactory&,DetReplayRegressionOptions={});
private:
    struct Cursor;
};
} // namespace ayt::entity
