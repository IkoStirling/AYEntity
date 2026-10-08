#pragma once
#include <AYEntity/components/DetSimTransformComponent.h>
#include <AYMath/Random.h>
#include <AYEntity/DeterministicState.h>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace ayt::entity {
class World;
class Entity;
class DetSessionStorage;
using SimEntityId = std::uint64_t;
using DetStateBlocks = std::map<std::uint32_t,std::vector<std::uint64_t>>;
struct DetStateSchema {
    std::uint32_t id=0, version=1;
    std::vector<std::uint32_t> fields;
    friend bool operator==(const DetStateSchema&,const DetStateSchema&)=default;
};
struct DetSessionConfig {
    std::uint32_t applicationVersion=1, inputVersion=1;
    std::uint32_t stepNumerator=1, stepDenominator=60;
    std::uint64_t contentHash=0;
};
struct DetTickCommand {
    std::uint32_t source=0, sequence=0, type=0;
    std::vector<std::uint8_t> payload;
    friend bool operator==(const DetTickCommand&,const DetTickCommand&)=default;
};
struct DetTickInput {
    std::uint64_t tick=0;
    std::uint32_t version=1;
    std::vector<DetTickCommand> commands;
    friend bool operator==(const DetTickInput&,const DetTickInput&)=default;
};
struct DetActorState {
    DetSimTransformComponent::Snapshot pose;
    DetStateBlocks blocks;
    friend bool operator==(const DetActorState&,const DetActorState&)=default;
};
struct DetSessionCheckpoint {
    static constexpr std::uint32_t kVersion=1;
    std::vector<std::uint8_t> manifest;
    std::uint64_t nextTick=0;
    std::map<SimEntityId,DetActorState> actors;
    DetStateBlocks globals;
    std::map<std::uint32_t,math::pcg32_state> randomStreams;
    std::vector<DetTickCommand> pendingEvents;
    std::vector<SimEntityId> retiredIds;
};
/// First differing canonical field; IDs are session identities, never addresses/RTTI.
struct DetStateDifference {
    std::uint64_t tick=0, entity=0, expected=0, actual=0;
    std::uint32_t component=0, field=0;
    std::string section;
    /// Zero-based vector/quaternion lane; scalar fields use zero.
    std::uint32_t lane=0;
};
/// Explicit little-endian profile, length bounds and checksum; no object dumps.
std::vector<std::uint8_t> encodeDetCheckpoint(const DetSessionCheckpoint& state);
bool decodeDetCheckpoint(std::span<const std::uint8_t> bytes,DetSessionCheckpoint& state,std::string& error);
std::optional<DetStateDifference> firstDetDifference(const DetSessionCheckpoint& expected,const DetSessionCheckpoint& actual);
std::uint64_t detCheckpointHash(const DetSessionCheckpoint& state);
bool canonicalizeDetInput(DetTickInput& input,std::string& error);
std::vector<std::uint8_t> encodeDetInput(const DetTickInput& input);
bool decodeDetInput(std::span<const std::uint8_t> bytes,DetTickInput& input,std::string& error);

class DeterministicSession;
/** @brief One sealed tick's authoritative access, valid only during its system callback.
 * @note Iterate entities() by stable SimEntityId. Hidden mutable callback state,
 * runtime Entity IDs, native floats and external effects are outside the contract.
 * All mutable gameplay data must use pose(), typed read/write, legacy word
 * blocks, or registered RNG. Typed access throws on ID/shape/type/value errors;
 * the tick faults even if a callback catches the exception.
 */
class DetTickContext {
public:
    std::uint64_t tick() const;
    math::DetFloat32 dt() const;
    const DetTickInput& input() const;
    std::span<const DetTickCommand> events() const;
    std::vector<SimEntityId> entities() const;
    DetSimTransformComponent& pose(SimEntityId id);
    std::span<std::uint64_t> words(SimEntityId id,std::uint32_t schema);
    std::span<std::uint64_t> globals(std::uint32_t schema);
    /// Copies only; typed schemas cannot be accessed through words()/globals().
    template<DetStateScalar T> T read(SimEntityId id,std::uint32_t schema,std::uint32_t field) {
        return std::get<T>(readValue(id,schema,field,detStateType(DetStateValue{T{}})));
    }
    /// Validate type/value before replacing all lanes; errors fault the tick.
    template<DetStateScalar T> void write(SimEntityId id,std::uint32_t schema,std::uint32_t field,T value) {
        writeValue(id,schema,field,DetStateValue{value});
    }
    /// Global state uses the reserved actor ID zero, never an Entity pointer.
    template<DetStateScalar T> T readGlobal(std::uint32_t schema,std::uint32_t field) {return read<T>(0,schema,field);}
    template<DetStateScalar T> void writeGlobal(std::uint32_t schema,std::uint32_t field,T value) {write<T>(0,schema,field,value);}
    math::pcg32_state& random(std::uint32_t stream);
    /// Queue structural changes for end-of-tick, in system (priority,id)/call order.
    /// IDs cannot be reused, even after despawn; invalid requests fault the tick.
    void spawn(SimEntityId id,DetActorState initial={});
    void despawn(SimEntityId id);
    /// Next-tick delivery, sorted by stable producer system ID and local sequence.
    void emit(std::uint32_t type,std::span<const std::uint8_t> payload={});
private:
    friend class DeterministicSession;
    DetTickContext(DeterministicSession& session,std::uint32_t system);
    DetStateValue readValue(SimEntityId id,std::uint32_t schema,std::uint32_t field,DetStateType type);
    void writeValue(SimEntityId id,std::uint32_t schema,std::uint32_t field,const DetStateValue& value);
    DeterministicSession& _session;
    std::uint32_t _system, _sequence=0;
};

/** @brief Explicit single-threaded tick owner for a World and its registered Sim state.
 * @note Link AYEntity::Determinism. Claim an initialized World with no legacy Sim
 * systems/authorities. Configure schemas, globals, RNG and callbacks before seal().
 * Systems run by (priority,stable ID), entities by Sim ID, input/events by
 * (source,sequence). This owner excludes direct World Sim ticks and structural
 * mutation. Presentation may run independently but cannot change Sim fields.
 * Schema IDs >=2 describe typed fixed fields or legacy uint64 words (pose ID 1).
 * Checkpoint validates everything before restoring; callback failure faults the
 * session until explicit restore. It cannot undo unregistered external effects.
 * World outlives the session; handles/pointers may change after restoration.
 */
class DeterministicSession {
public:
    using System=std::function<bool(DetTickContext&)>;
    using Validator=std::function<bool(const DetSessionCheckpoint&,std::string&)>;
    explicit DeterministicSession(World& world,DetSessionConfig config={});
    /// Owned registered-state kernel, without a World/Host or presentation entities.
    /// Same callbacks, manifests, checkpoint validation and collision as the World adapter.
    /// Link AYEntity::DeterminismKernel; this overload claims no global engine services.
    explicit DeterministicSession(DetSessionConfig config={});
    ~DeterministicSession();
    DeterministicSession(const DeterministicSession&)=delete;
    DeterministicSession& operator=(const DeterministicSession&)=delete;
    bool registerSchema(DetStateSchema schema,std::vector<std::uint64_t> defaults={});
    /// Register finite, bounded typed fields before actors/seal; manifest version 2.
    /// Defaults are automatically applied to actors and registered globals.
    bool registerTypedSchema(DetTypedStateSchema schema);
    bool registerSystem(std::uint32_t id,std::int32_t priority,System system);
    /// Version/hash of registered authoritative code enters manifest 4 (max 64).
    /// Configure before seal; matching peers/replays must have the same identities.
    bool registerLogicProfile(std::uint32_t id,std::uint32_t version,std::uint64_t hash);
    /// Register pure read-only state semantics before seal (max 64, sorted stable IDs).
    /// ID/version enter manifest 3. Called at seal, checkpoint, restore and before
    /// structural tick commit; false/throw rejects with ID and diagnostic. No
    /// mutable captures, external effects or session reentry; validator code changes
    /// require a new version. Invalid restore never changes World state.
    bool registerValidator(std::uint32_t id,std::uint32_t version,Validator validator);
    bool registerRandomStream(std::uint32_t id,std::uint64_t seed);
    bool registerGlobalState(std::uint32_t schema,std::vector<std::uint64_t> words={});
    bool addEntity(SimEntityId id,DetActorState initial={});
    /// Freeze canonical configuration and validate the initial registered state.
    bool seal();
    /// Validate sequential input and external mutation before any tick writes.
    /// Input validation failure leaves state unchanged; execution failure faults.
    bool advance(DetTickInput input);
    /// Capture all registered fields at a quiescent boundary; reject outside writes.
    std::optional<DetSessionCheckpoint> checkpoint() const;
    /// Same manifest required; invalid data rejected before changing any Sim state.
    bool restore(const DetSessionCheckpoint& state);
    std::uint64_t nextTick() const;
    /// Immutable input schema version used by tick owners and neutral bootstrap frames.
    std::uint32_t inputVersion() const;
    math::DetFloat32 fixedStep() const;
    bool sealed() const;
    bool faulted() const;
    const std::string& error() const;
    const std::vector<std::uint8_t>& manifest() const;
    /// Presentation lookup only; authoritative systems use stable session IDs.
    Entity* presentationEntity(SimEntityId id) const;
private:
    friend class DetTickContext;
    DeterministicSession(std::unique_ptr<DetSessionStorage>,DetSessionConfig);
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
} // namespace ayt::entity
