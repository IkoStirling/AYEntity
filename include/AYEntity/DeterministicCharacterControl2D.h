#pragma once
// design reference: AYEntity/design.md Stage55 finite registered character input ownership.
#include <AYEntity/DeterministicCharacterRespawn2D.h>
#include <optional>

namespace ayt::entity {
inline constexpr std::uint32_t kDetCharacterControl2DProfileVersion=1;
inline constexpr std::size_t kDetCharacterControl2DMaxOwners=8,kDetCharacterControl2DMaxPolicyBytes=8192;
enum class DetCharacterControlPhase2D : std::uint32_t {Ready=0,Active=1,Cooldown=2,Retired=3};
enum class DetCharacterControlOutcome2D : std::uint32_t {
    None=0,Normal=1,Active=2,Started=3,DeniedActive=4,DeniedCooldown=5,
    DeniedGround=6,DeniedCancel=7,DeniedRelocation=8,Canceled=9,RelocationCanceled=10,Retired=11
};
/// @brief One fixed enabled character and a bounded horizontal dash target policy.
/// @note durationTicks is1..65535 and cooldownTicks0..65535. dashSpeed is finite,
/// strictly positive and <=character.maxHorizontalSpeed. groundOnly samples the
/// previous committed grounded state, not the next character pre contact probe.
struct DetCharacterControlOwner2D {
    SimEntityId actor=0;
    math::DetFloat32 dashSpeed=math::DetFloat32::fromInt(8);
    std::uint32_t durationTicks=8,cooldownTicks=16;
    bool groundOnly=false,initialFacingRight=true;
};
/** @brief Optional registered input owner over a separately installed matching advanced controller.
 * @note Default callback order is optional disjoint-owner Brain3, external input4,
 * Attack5, Vitality6, Flow7, Control15, character pre20 at collision.priority.
 * It does not install or adopt Character/collision/Respawn and writes no pose,
 * body velocity or HP. Dash is a target-speed override; actual acceleration,
 * deceleration, gravity, carrier, jump and wall responses remain Character policy.
 * No exact-distance, instant-velocity, no-gravity or invulnerability promise.
 * Before actors/seal, install the matching Character or explicit Respawn facade,
 * then Control. character.advanced must be true. characterRespawn requires the
 * complete matching respawn configuration here; otherwise respawn must be empty.
 * Owners are sorted nonzero ordinary solid Kinematic enabled characters at seal,
 * with zero pending Character speed/held samples. They remain enabled while present;
 * retirement is allowed and clears emitted samples. Brain and Control owner sets
 * must be disjoint. Assembly also places all relevant paths before character pre.
 * Other callbacks must not replace Control-owned Character inputs. These are caller
 * authority constraints, not a generic discovery of arbitrary installed facades.
 * Full descriptor defaults/identities and optional Respawn policy are literal policy,
 * <=8KiB/9 chunks; actual joint64-schema capacity still applies.
 */
/// @brief Complete immutable control policy and nonconflicting registration IDs.
struct DetCharacterControl2DConfig {
    DetCharacterController2DConfig character;
    std::optional<DetCharacterRespawn2DConfig> respawn;
    std::uint32_t inputSchema=304,stateSchema=305,policySchemaBase=306,
        systemId=15,validatorId=79,logicProfileId=79;
    std::vector<DetCharacterControlOwner2D> owners;
};
/// @brief Persistent complete normal input plus held dash, facing and cancellation samples.
/// @note Missing acquisition holds every sample. Explicit false releases dash/cancel
/// and advanced jump/drop. A canceled, active, cooldown, airborne or fresh-relocation
/// dash press consumes its rising edge, with no buffer or automatic repeat.
struct DetCharacterControlInput2D {
    DetCharacterInput2D character;
    bool dashHeld=false,facingRight=true,cancel=false;
};
/// Input members occupy one sorted owner's block at1000+200*index.
enum DetCharacterControlInputField2D : std::uint32_t {
    DetControlRawSpeed=10,DetControlRawJump=20,DetControlRawCrouch=30,DetControlRawDrop=40,
    DetControlRawDashHeld=50,DetControlRawFacing=60,DetControlRawCancel=70,DetControlDashLatch=80,
    DetControlEmittedSpeed=90,DetControlEmittedJump=100,DetControlEmittedCrouch=110,
    DetControlEmittedDrop=120,DetControlObservedPresent=130,DetControlObservedAccepted=140,
    DetControlRisingEdge=150
};
/// Timeline members use the same sorted owner blocks; stored last stamps are tick+1.
enum DetCharacterControlField2D : std::uint32_t {
    DetControlPhase=10,DetControlLockedFacing=20,DetControlStartTick=30,DetControlEndTick=40,
    DetControlReadyTick=50,DetControlRequestCount=60,DetControlStartCount=70,
    DetControlDeniedCount=80,DetControlCancelCount=90,DetControlCanceled=100,
    DetControlLastOutcome=110,DetControlObservedGrounded=120,DetControlObservedRespawnTick=130,
    DetControlLastStartStamp=140,DetControlLastCancelStamp=150
};
enum DetCharacterControlGlobalField2D : std::uint32_t {DetControlProcessedTick=1};
/** @brief Copied control decision, registered samples and finite windows.
 * @note A start at T emits locked +/-dashSpeed on [T,T+durationTicks). readyTick
 * is T+durationTicks+cooldownTicks; cancellation retains this original deadline.
 * Explicit cancel or a previous-tick actual Accepted relocation cancels an active
 * dash before this tick's Character input and rejects a same-tick fresh dash edge.
 * Queued or denied relocations do not cancel. A same-tick Accepted post can reset
 * the actual Character input after this captured decision; the pure validator
 * accepts only that real current result, and the next Control observes/cancels it.
 * At checkpoint nextTick N>0, phase/outcome/emitted/observations describe N-1.
 * Active therefore may be the final emitted tick even when N==endTick. Observed
 * grounded and respawn stamps are pre-Control observations, not current support.
 * Counts are attempts/decisions, not successful distance or historical proof.
 * Retirement ends an active override once, counting a cancellation and retaining
 * readyTick; raw held samples stay registered and are consumed without actor access.
 * Zero-based lastStartTick/lastCancelTick are meaningful only with their counts.
 * Death is not implicitly bound: the application supplies explicit cancellation.
 */
/// @brief Copied registered input and last finite control decision.
struct DetCharacterControlState2D {
    DetCharacterControlInput2D input;
    DetCharacterInput2D emitted;
    DetCharacterControlPhase2D phase=DetCharacterControlPhase2D::Ready;
    DetCharacterControlOutcome2D outcome=DetCharacterControlOutcome2D::None;
    bool dashLatched=false,risingEdge=false,lockedFacingRight=true,canceled=false,
        ownerPresent=false,observedGrounded=false,observedAccepted=false;
    std::uint64_t startTick=0,endTick=0,readyTick=0,decisionCount=0,lastDecisionTick=0,
        requestCount=0,startCount=0,deniedCount=0,cancelCount=0,lastStartTick=0,lastCancelTick=0,
        observedRespawnTick=0;
};
/// @brief Register finite Control input/timeline globals, callback and pure dependency closure.
/// @note Failed assembly can be partial; abandon it. Install no dependency twice.
bool installDetCharacterControl2D(DeterministicSession& session,DetCharacterControl2DConfig config);
/// @brief Set complete registered input only from an actual earlier callback before Control.
/// @note Invalid policy/input or late use throws; arithmetic is planned for all owners
/// before Character writes. Caught typed-context faults still require Session restore.
void detCharacterControlInput2D(DetTickContext& context,const DetCharacterControl2DConfig& config,
    SimEntityId owner,DetCharacterControlInput2D input);
/// @brief Copy raw input, last output and decision; no pointer/cache or hidden authority.
DetCharacterControlState2D detCharacterControlState2D(DetTickContext& context,
    const DetCharacterControl2DConfig& config,SimEntityId owner);
DetCharacterControlState2D detCharacterControlState2D(const DetSessionCheckpoint& checkpoint,
    const DetCharacterControl2DConfig& config,SimEntityId owner);
/// @brief Actual mutable descriptors:120 input fields and121 timeline fields at8 owners.
std::vector<DetTypedStateSchema> detCharacterControlStateSchemas2D(const DetCharacterControl2DConfig& config);
/// @brief Actual selected literal policy descriptors, using full nested defaults and identity.
std::vector<DetTypedStateSchema> detCharacterControlPolicySchemas2D(const DetCharacterControl2DConfig& config);
/// @brief Complete immutable policy bytes; no hash-only authority or semantic migration.
std::vector<std::uint8_t> encodeDetCharacterControlConfig2D(const DetCharacterControl2DConfig& config);
/// @brief Resolve a fixed owner's input or timeline field through its sorted ID.
std::uint32_t detCharacterControlInputField2D(const DetCharacterControl2DConfig& config,
    SimEntityId owner,DetCharacterControlInputField2D field);
std::uint32_t detCharacterControlField2D(const DetCharacterControl2DConfig& config,
    SimEntityId owner,DetCharacterControlField2D field);
} // namespace ayt::entity
