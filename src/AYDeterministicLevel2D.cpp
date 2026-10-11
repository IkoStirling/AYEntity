// design reference: AYEntity/design.md Stage32/36/42/46 bounded complete level content.
#include <AYEntity/DeterministicLevel2D.h>
#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>

namespace ayt::entity {
namespace {
using D=math::DetFloat32;using V=math::DetVec2;using Bytes=std::vector<std::uint8_t>;
[[noreturn]] void invalid(const char* reason){throw std::invalid_argument(reason);}
std::uint32_t indexed(std::uint32_t base,std::size_t offset){
    if(!base || offset>std::numeric_limits<std::uint32_t>::max()-base)invalid("Level registration ID overflow");
    const auto id=base+static_cast<std::uint32_t>(offset);if(id==UINT32_MAX)invalid("Reserved level registration ID");return id;
}
struct Writer {Bytes bytes;
    void u(std::uint64_t value,unsigned width=4){for(unsigned n=0;n<width;++n)bytes.push_back(static_cast<std::uint8_t>(value>>(8*n)));}
    void scalar(D value){if(!value.isFinite())invalid("Nonfinite level Float32");u(value.bits());}
    void vector(V value){scalar(value.x);scalar(value.y);}
};
template<class Codec> void collision(Codec& io,DetCollision2DConfig& c){
    io.word(c.bodySchema);io.word(c.historySchema);io.word(c.systemId);io.signedWord(c.priority);
    io.word(c.eventType);io.word(c.maxBodies);io.word(c.maxTriggerPairs);io.word(c.maxContactIterations);
    io.boolean(c.extended);io.boolean(c.oneWayPlatforms);io.boolean(c.stepUp);io.scalar(c.maxStepHeight);
    io.enumeration(c.motionPolicy);io.boolean(c.staticSlopes);io.boolean(c.movingSlopes);io.boolean(c.continuousTerrain);
}
template<class Codec> void character(Codec& io,DetCharacterController2DConfig& c){
    io.word(c.stateSchema);io.word(c.policySchema);io.word(c.preSystemId);io.word(c.postSystemId);
    io.word(c.validatorId);io.word(c.logicProfileId);
    io.scalar(c.maxHorizontalSpeed);io.scalar(c.acceleration);io.scalar(c.deceleration);io.scalar(c.gravity);
    io.scalar(c.jumpSpeed);io.scalar(c.maxFallSpeed);io.scalar(c.maxPlatformSpeed);io.scalar(c.groundProbeDistance);
    io.scalar(c.groundContactTolerance);io.scalar(c.snapDistance);io.enumeration(c.leaveVelocity);io.boolean(c.advanced);
    io.word(c.coyoteTicks);io.word(c.jumpBufferTicks);io.word(c.dropThroughTicks);io.boolean(c.crouchEnabled);
    io.scalar(c.crouchHeightScale);io.scalar(c.maxSlopeRisePerRun);
}
struct BinaryCodec {Writer w;
    void word(std::uint32_t value){w.u(value);}void signedWord(std::int32_t value){w.u(static_cast<std::uint32_t>(value));}
    void boolean(bool value){w.u(value?1:0);}void scalar(D value){w.scalar(value);}
    template<class E>void enumeration(E value){w.u(static_cast<std::uint32_t>(value));}
};
Bytes canonical(const DetLevel2D& level){
    const bool respawnContent=!level.respawnSpawns.empty() || !level.respawnBindings.empty() || !level.respawnRegions.empty();
    if(level.formatVersion<4 && (level.character.collision.characterRespawn || respawnContent))invalid("Older level formats cannot author character respawn");
    if(level.respawnSpawns.size()>kDetCharacterRespawn2DMaxSpawns || level.respawnBindings.size()>kDetCharacterRespawnFlow2DMaxBindings
        || level.respawnRegions.size()>kDetCharacterRespawnFlow2DMaxRegions
        || (level.character.collision.characterRespawn?level.respawnSpawns.empty() || level.respawnBindings.empty():respawnContent))invalid("Level respawn table budget or opt-in mismatch");
    if(level.paths.size()>8 || level.actors.empty() || level.actors.size()>64)invalid("Level body/path count limit");
    if(level.formatVersion<1 || level.formatVersion>4)invalid("Unknown level format version");
    if(level.formatVersion==1 && (!level.actionTargets.empty() || !level.rules.empty() || level.maxActionsPerTick!=64))invalid("Format1 cannot author actions/rules");
    if(level.actionTargets.size()>8 || level.rules.size()>kDetInteraction2DMaxRules
        || !level.maxActionsPerTick || level.maxActionsPerTick>1024 || (level.actionTargets.empty() && !level.rules.empty()))invalid("Level action/rule budget");
    const bool progressContent=!level.counters.empty() || !level.objectives.empty() || !level.claims.empty()
        || !level.flowBindings.empty() || !level.objectiveActions.empty();
    if(level.formatVersion<3 && progressContent)invalid("Older formats cannot author progress/flow");
    if((level.counters.empty() && progressContent) || level.counters.size()>kDetProgress2DMaxCounters
        || level.objectives.size()>kDetProgress2DMaxObjectives || level.claims.size()>kDetProgress2DMaxClaims
        || level.flowBindings.size()>kDetProgressFlow2DMaxClaims || level.objectiveActions.size()>kDetProgressFlow2DMaxRules)invalid("Level progress/flow budget");
    for(const auto& objective:level.objectives)if(objective.conditions.empty() || objective.conditions.size()>kDetProgress2DMaxConditions)invalid("Level objective condition budget");
    BinaryCodec io;const auto magic=level.formatVersion==1?std::string_view("AYDLV001"):
        level.formatVersion==2?std::string_view("AYDLV002"):level.formatVersion==3?std::string_view("AYDLV003"):std::string_view("AYDLV004");
    for(auto ch:magic)io.w.u(static_cast<unsigned char>(ch),1);
    io.word(level.formatVersion);io.word(level.applicationVersion);io.word(level.inputVersion);
    io.word(level.stepNumerator);io.word(level.stepDenominator);
    auto policy=level.character;collision(io,policy.collision);if(level.formatVersion>=4)io.boolean(policy.collision.characterRespawn);character(io,policy);
    io.word(static_cast<std::uint32_t>(level.paths.size()));
    for(const auto& path:level.paths){if(path.waypoints.size()<2 || path.waypoints.size()>24)invalid("Level waypoint count limit");
        io.word(path.id);io.enumeration(path.playback);io.word(static_cast<std::uint32_t>(path.waypoints.size()));
        for(const auto& point:path.waypoints){io.w.vector(point.offset);io.word(point.travelTicks);io.word(point.dwellTicks);}}
    io.word(static_cast<std::uint32_t>(level.actors.size()));
    for(const auto& actor:level.actors){io.w.u(actor.id,8);io.enumeration(actor.body.mode);io.enumeration(actor.body.shape);
        io.w.vector(actor.position);io.w.vector(actor.body.half);io.w.vector(actor.body.offset);io.w.vector(actor.body.velocity);
        io.word(actor.body.layer);io.word(actor.body.mask);io.boolean(actor.body.trigger);io.boolean(actor.body.oneWay);
        io.boolean(actor.character);io.word(actor.path);
        if(actor.body.ignoredOneWay.value)invalid("Level cannot author ignored platform IDs");}
    if(level.formatVersion>=2){io.word(level.maxActionsPerTick);io.word(static_cast<std::uint32_t>(level.actionTargets.size()));
        for(const auto& target:level.actionTargets){io.w.u(target.actor,8);io.word(target.collisionMask);io.boolean(target.collisionControl);io.word(target.path);}
        io.word(static_cast<std::uint32_t>(level.rules.size()));for(const auto& rule:level.rules){io.word(rule.id);io.w.u(rule.trigger,8);io.w.u(rule.actor,8);
            io.enumeration(rule.phase);io.w.u(rule.action.target,8);io.enumeration(rule.action.kind);io.boolean(rule.once);io.word(rule.cooldownTicks);io.word(rule.delayTicks);}}
    if(level.formatVersion>=3){io.word(static_cast<std::uint32_t>(level.counters.size()));
        io.word(static_cast<std::uint32_t>(level.objectives.size()));io.word(static_cast<std::uint32_t>(level.claims.size()));
        for(const auto& counter:level.counters){io.word(counter.id);io.w.u(counter.minimum,8);io.w.u(counter.maximum,8);io.w.u(counter.initial,8);}
        for(const auto& objective:level.objectives){io.word(objective.id);io.word(static_cast<std::uint32_t>(objective.conditions.size()));
            for(const auto& condition:objective.conditions){io.word(condition.counter);io.w.u(condition.atLeast,8);}}
        for(const auto& claim:level.claims){io.word(claim.id);io.w.u(claim.trigger,8);io.w.u(claim.actor,8);io.word(claim.counter);io.w.u(claim.amount,8);}
        io.word(static_cast<std::uint32_t>(level.flowBindings.size()));io.word(static_cast<std::uint32_t>(level.objectiveActions.size()));
        for(const auto& binding:level.flowBindings){io.word(binding.claim);io.word(binding.requiredObjective);}
        for(const auto& rule:level.objectiveActions){io.word(rule.id);io.word(rule.objective);io.w.u(rule.action.target,8);io.enumeration(rule.action.kind);io.boolean(rule.once);}}
    if(level.formatVersion>=4){io.word(static_cast<std::uint32_t>(level.respawnSpawns.size()));
        io.word(static_cast<std::uint32_t>(level.respawnBindings.size()));io.word(static_cast<std::uint32_t>(level.respawnRegions.size()));
        for(const auto& spawn:level.respawnSpawns){io.word(spawn.id);io.w.u(spawn.actor,8);io.w.vector(spawn.position);}
        for(const auto& binding:level.respawnBindings){io.w.u(binding.actor,8);io.word(binding.spawn);}
        for(const auto& region:level.respawnRegions){io.word(region.id);io.w.u(region.trigger,8);io.w.u(region.actor,8);io.enumeration(region.kind);io.word(region.spawn);}}
    if(io.w.bytes.size()>kDetLevel2DMaxCanonicalBytes)invalid("Level canonical content limit");return std::move(io.w.bytes);
}
std::uint64_t hash(const Bytes& bytes){std::uint64_t h=14695981039346656037ull;for(auto byte:bytes){h^=byte;h*=1099511628211ull;}return h;}
struct Chunk {DetTypedStateSchema schema;std::vector<std::uint64_t> words;};
struct Prepared {Bytes bytes;std::vector<DetPathMotion2DConfig> paths;std::vector<Chunk> chunks;
    std::vector<std::pair<SimEntityId,DetActorState>> actors;
    std::optional<DetActions2DConfig> actions;std::optional<DetInteraction2DConfig> interaction;
    std::optional<DetProgress2DConfig> progress;std::optional<DetProgressFlow2DConfig> flow;
    std::optional<DetCharacterRespawn2DConfig> respawn;std::optional<DetCharacterRespawnFlow2DConfig> respawnFlow;};
void unique(std::set<std::uint32_t>& ids,std::uint32_t value,bool schema=false){
    if(!value || value==UINT32_MAX || (schema && value<2) || !ids.insert(value).second)invalid("Level registration namespace conflict");
}
Prepared prepare(const DetLevel2D& level,const DetLevel2DOptions& options){
    Prepared result;result.bytes=canonical(level);const auto& cc=level.character;const auto& bc=cc.collision;
    if(!level.applicationVersion || !level.inputVersion || !level.stepNumerator || !level.stepDenominator)invalid("Zero level session version/step");
    if(level.actors.size()>bc.maxBodies)invalid("Level exceeds configured body budget");
    std::set<std::uint32_t> schemas,systems,validators,logic;
    for(auto id:{bc.bodySchema,bc.historySchema,cc.stateSchema,cc.policySchema})unique(schemas,id,true);
    for(auto id:{bc.systemId,cc.preSystemId,cc.postSystemId})unique(systems,id);
    unique(validators,bc.systemId);unique(validators,cc.validatorId);unique(logic,cc.logicProfileId);
    unique(validators,options.identityValidatorId);unique(logic,options.identityLogicId);
    std::uint32_t previousPath=0;
    for(std::size_t n=0;n<level.paths.size();++n){const auto& source=level.paths[n];if(source.id<=previousPath)invalid("Path IDs must increase");previousPath=source.id;
        DetPathMotion2DConfig p;p.collision=bc;p.playback=source.playback;p.waypoints=source.waypoints;
        p.tickPeriod=D::fromUInt(level.stepNumerator)/D::fromUInt(level.stepDenominator);
        p.stateSchema=indexed(options.pathSchemaBase,2*n);p.policySchema=indexed(options.pathSchemaBase,2*n+1);
        p.preSystemId=indexed(options.pathPreSystemBase,n);p.postSystemId=indexed(options.pathPostSystemBase,n);
        p.validatorId=indexed(options.pathValidatorBase,n);p.logicProfileId=indexed(options.pathLogicBase,n);
        if(!(p.preSystemId<cc.preSystemId && p.postSystemId>cc.postSystemId))invalid("Level path/character ordering conflict");
        unique(schemas,p.stateSchema,true);unique(schemas,p.policySchema,true);unique(systems,p.preSystemId);unique(systems,p.postSystemId);
        unique(validators,p.validatorId);unique(logic,p.logicProfileId);result.paths.push_back(std::move(p));}
    const auto actorAt=[&](SimEntityId id)->const DetLevelActor2D&{const auto it=std::find_if(level.actors.begin(),level.actors.end(),[&](const auto& a){return a.id==id;});
        if(it==level.actors.end())invalid("Level authority actor is missing");return *it;};
    if(!level.actionTargets.empty() || !level.counters.empty()){
        DetActions2DConfig actions;actions.collision=bc;actions.stateSchema=options.actionsStateSchema;
        actions.policySchemaBase=options.actionsPolicySchemaBase;actions.validatorId=options.actionsValidatorId;
        actions.logicProfileId=options.actionsLogicProfileId;actions.maxActionsPerTick=level.maxActionsPerTick;
        SimEntityId previous=0;
        for(const auto& source:level.actionTargets){if(!source.actor || source.actor<=previous || !source.collisionMask)invalid("Level targets must increase with nonzero mask");previous=source.actor;
            const auto& actor=actorAt(source.actor);const auto& body=actor.body;
            if(body.mask!=0 && body.mask!=source.collisionMask)invalid("Level action target initial mask is unauthorized");
            if(source.collisionControl && (body.shape!=DetCollisionShape2D::Aabb || body.oneWay || body.mode==DetBodyMode2D::Disabled
                || (!body.trigger && body.mode!=DetBodyMode2D::Static && body.mode!=DetBodyMode2D::MovingObstacle)))invalid("Unsupported level collision target");
            DetActionTarget2D target{source.actor,source.collisionMask,source.collisionControl,{}};
            if(source.path){if(actor.path!=source.path)invalid("Level action path differs from actor binding");
                const auto it=std::find_if(level.paths.begin(),level.paths.end(),[&](const auto& path){return path.id==source.path;});
                if(it==level.paths.end())invalid("Level action path is missing");target.path=result.paths[static_cast<std::size_t>(it-level.paths.begin())];}
            if(!source.collisionControl && !target.path)invalid("Level action target grants no control");actions.targets.push_back(std::move(target));}
        (void)encodeDetActionsConfig2D(actions);unique(schemas,actions.stateSchema,true);
        for(const auto& schema:detActionsPolicySchemas2D(actions))unique(schemas,schema.id,true);
        unique(validators,actions.validatorId);unique(logic,actions.logicProfileId);result.actions=std::move(actions);
        if(!level.rules.empty()){
            DetInteraction2DConfig rules;rules.actions=*result.actions;rules.stateSchema=options.interactionStateSchema;
            rules.policySchemaBase=options.interactionPolicySchemaBase;rules.systemId=options.interactionSystemId;
            rules.validatorId=options.interactionValidatorId;rules.logicProfileId=options.interactionLogicProfileId;rules.rules=level.rules;
            if(rules.systemId>=cc.preSystemId || rules.systemId>=bc.systemId)invalid("Level interaction ordering conflict");
            for(const auto& path:result.paths)if(rules.systemId>=path.preSystemId)invalid("Level interaction must precede every path pre");
            for(const auto& rule:rules.rules){const auto& trigger=actorAt(rule.trigger);const auto& actor=actorAt(rule.actor);
                if(!trigger.body.trigger || trigger.body.mode==DetBodyMode2D::Disabled || actor.body.trigger
                    || actor.body.mode!=DetBodyMode2D::Kinematic)invalid("Level rule trigger/actor role mismatch");
                const auto target=std::find_if(rules.actions.targets.begin(),rules.actions.targets.end(),[&](const auto& t){return t.actor==rule.action.target;});
                if(target==rules.actions.targets.end())invalid("Level rule target has no authority");
                const auto kind=static_cast<std::uint32_t>(rule.action.kind);
                if(kind<=2?!target->collisionControl:!target->path)invalid("Level rule action unsupported by target");}
            const auto bytes=encodeDetInteractionConfig2D(rules);unique(schemas,rules.stateSchema,true);
            // Interaction profile1 packs at most1000 bytes per policy schema.
            // Actual fresh installation/seal below also validates this binding.
            const auto chunks=(bytes.size()+999)/1000;
            for(std::size_t n=0;n<chunks;++n)unique(schemas,indexed(rules.policySchemaBase,n),true);
            unique(systems,rules.systemId);unique(validators,rules.validatorId);unique(logic,rules.logicProfileId);result.interaction=std::move(rules);
        }
    }
    if(!level.counters.empty()){
        DetProgress2DConfig progress;progress.actions=*result.actions;progress.stateSchema=options.progressStateSchema;
        progress.policySchemaBase=options.progressPolicySchemaBase;progress.validatorId=options.progressValidatorId;
        progress.logicProfileId=options.progressLogicProfileId;progress.counters=level.counters;
        progress.objectives=level.objectives;progress.claims=level.claims;
        for(const auto& claim:progress.claims){const auto& trigger=actorAt(claim.trigger).body;const auto& actor=actorAt(claim.actor).body;
            if(trigger.shape!=DetCollisionShape2D::Aabb || !trigger.trigger || trigger.oneWay || trigger.mode==DetBodyMode2D::Disabled
                || actor.trigger || actor.mode!=DetBodyMode2D::Kinematic)invalid("Level collectible trigger/actor role mismatch");}
        const auto bytes=encodeDetProgressConfig2D(progress);unique(schemas,progress.stateSchema,true);
        for(std::size_t n=0;n<(bytes.size()+999)/1000;++n)unique(schemas,indexed(progress.policySchemaBase,n),true);
        unique(validators,progress.validatorId);unique(logic,progress.logicProfileId);result.progress=std::move(progress);
        if(!level.flowBindings.empty() || !level.objectiveActions.empty()){
            DetProgressFlow2DConfig flow;flow.progress=*result.progress;flow.collectStateSchema=options.flowCollectStateSchema;
            flow.objectiveStateSchema=options.flowObjectiveStateSchema;flow.policySchemaBase=options.flowPolicySchemaBase;
            flow.systemId=options.flowSystemId;flow.validatorId=options.flowValidatorId;flow.logicProfileId=options.flowLogicProfileId;
            flow.claims=level.flowBindings;flow.rules=level.objectiveActions;
            if(flow.systemId>=cc.preSystemId || flow.systemId>=bc.systemId
                || (result.interaction && flow.systemId>=result.interaction->systemId))invalid("Level flow ordering conflict");
            for(const auto& path:result.paths)if(flow.systemId>=path.preSystemId)invalid("Level flow must precede every path pre");
            for(const auto& rule:flow.rules){const auto target=std::find_if(flow.progress.actions.targets.begin(),flow.progress.actions.targets.end(),[&](const auto& t){return t.actor==rule.action.target;});
                if(target==flow.progress.actions.targets.end())invalid("Level objective action target has no authority");
                const auto kind=static_cast<std::uint32_t>(rule.action.kind);
                if(kind<=2?!target->collisionControl:!target->path)invalid("Level objective action unsupported by target");}
            const auto flowBytes=encodeDetProgressFlowConfig2D(flow);
            unique(schemas,flow.collectStateSchema,true);unique(schemas,flow.objectiveStateSchema,true);
            for(std::size_t n=0;n<(flowBytes.size()+999)/1000;++n)unique(schemas,indexed(flow.policySchemaBase,n),true);
            unique(systems,flow.systemId);unique(validators,flow.validatorId);unique(logic,flow.logicProfileId);result.flow=std::move(flow);
        }
    }
    if(bc.characterRespawn){
        DetCharacterRespawn2DConfig respawn;respawn.character=cc;respawn.stateSchema=options.respawnStateSchema;
        respawn.policySchema=options.respawnPolicySchema;respawn.validatorId=options.respawnValidatorId;
        respawn.logicProfileId=options.respawnLogicProfileId;respawn.spawns=level.respawnSpawns;
        DetCharacterRespawnFlow2DConfig flow;flow.respawn=respawn;flow.stateSchema=options.respawnFlowStateSchema;
        flow.policySchemaBase=options.respawnFlowPolicySchemaBase;flow.systemId=options.respawnFlowSystemId;
        flow.validatorId=options.respawnFlowValidatorId;flow.logicProfileId=options.respawnFlowLogicProfileId;
        flow.bindings=level.respawnBindings;flow.regions=level.respawnRegions;
        for(const auto& spawn:respawn.spawns){const auto& actor=actorAt(spawn.actor);
            if(!actor.character || actor.body.trigger || actor.body.mode!=DetBodyMode2D::Kinematic)invalid("Level respawn actor must be an initial character Kinematic");}
        for(const auto& region:flow.regions){const auto& trigger=actorAt(region.trigger).body;
            const auto& actor=actorAt(region.actor);if(!trigger.trigger || trigger.mode==DetBodyMode2D::Disabled
                || trigger.oneWay || trigger.shape!=DetCollisionShape2D::Aabb || !actor.character
                || actor.body.trigger || actor.body.mode!=DetBodyMode2D::Kinematic)invalid("Level respawn region role mismatch");}
        if(flow.systemId>=cc.preSystemId || flow.systemId>=bc.systemId
            || (result.flow && flow.systemId>=result.flow->systemId)
            || (result.interaction && flow.systemId>=result.interaction->systemId))invalid("Level respawn flow ordering conflict");
        for(const auto& path:result.paths)if(flow.systemId>=path.preSystemId)invalid("Level respawn flow must precede every path pre");
        (void)encodeDetCharacterRespawnFlowConfig2D(flow);
        unique(schemas,respawn.stateSchema,true);unique(schemas,respawn.policySchema,true);
        unique(validators,respawn.validatorId);unique(logic,respawn.logicProfileId);
        unique(schemas,detCharacterRespawnFlowStateSchema2D(flow).id,true);
        for(const auto& schema:detCharacterRespawnFlowPolicySchemas2D(flow))unique(schemas,schema.id,true);
        unique(systems,flow.systemId);unique(validators,flow.validatorId);unique(logic,flow.logicProfileId);
        result.respawn=std::move(respawn);result.respawnFlow=std::move(flow);
    }
    std::vector<std::uint64_t> words(1+(result.bytes.size()+7)/8);words[0]=result.bytes.size();
    for(std::size_t n=0;n<result.bytes.size();++n)words[1+n/8]|=std::uint64_t{result.bytes[n]}<<(8*(n%8));
    for(std::size_t offset=0;offset<words.size();offset+=128){const auto id=indexed(options.identitySchemaBase,result.chunks.size());
        if(result.chunks.size()>=16)invalid("Level identity schema limit");unique(schemas,id,true);
        Chunk chunk;chunk.schema.id=id;chunk.schema.version=level.formatVersion;
        const auto size=std::min<std::size_t>(128,words.size()-offset);
        for(std::size_t n=0;n<size;++n)chunk.schema.fields.push_back({static_cast<std::uint32_t>(n+1),words[offset+n]});
        chunk.words=DetStateLayout(chunk.schema).defaults();result.chunks.push_back(std::move(chunk));}
    std::set<std::uint32_t> usedPaths;SimEntityId previousActor=0;
    for(const auto& actor:level.actors){if(!actor.id || actor.id<=previousActor)invalid("Actor IDs must increase");previousActor=actor.id;
        if(actor.body.mode==DetBodyMode2D::Disabled)invalid("Level actors must be active");
        if(actor.character && (actor.body.mode!=DetBodyMode2D::Kinematic || actor.body.trigger || actor.body.velocity!=V{}))invalid("Invalid initial character role/velocity");
        DetActorState state;DetSimTransformComponent pose;
        if(!pose.setPosition({actor.position.x,actor.position.y,{}}))invalid("Invalid level pose");state.pose=pose.snapshot();
        state.blocks[bc.bodySchema]=detCollisionBodyState2D(bc,actor.body);
        if(actor.character){DetCharacterState2D initial;initial.enabled=true;state.blocks[cc.stateSchema]=detCharacterState2D(cc,initial);}
        if(actor.path){auto path=std::find_if(level.paths.begin(),level.paths.end(),[&](const auto& p){return p.id==actor.path;});
            if(path==level.paths.end() || actor.character || actor.body.mode!=DetBodyMode2D::MovingObstacle || actor.body.trigger || actor.body.velocity!=V{})invalid("Invalid level path binding");
            const auto index=static_cast<std::size_t>(path-level.paths.begin());DetPathMotionState2D initial;initial.bound=true;initial.origin=actor.position;
            state.blocks[result.paths[index].stateSchema]=detPathMotionState2D(result.paths[index],initial);usedPaths.insert(actor.path);}
        result.actors.emplace_back(actor.id,std::move(state));}
    if(usedPaths.size()!=level.paths.size())invalid("Unused level path policy");
    if(schemas.size()>64 || systems.size()>64 || validators.size()>64 || logic.size()>64)invalid("Level composite registration budget");
    std::vector<DetCollisionProxy2D> proxies;
    for(const auto& actor:level.actors){const auto center=actor.position+actor.body.offset;
        proxies.push_back({actor.id,{center-actor.body.half,center+actor.body.half},actor.body.layer,actor.body.mask,actor.body.trigger,actor.body.shape});}
    // The real pure narrowphase excludes the empty triangular bounding corner.
    for(const auto& pair:detCollisionPairs2D(proxies)){
        const auto first=std::find_if(level.actors.begin(),level.actors.end(),[&](const auto& a){return a.id==pair.first;});
        const auto second=std::find_if(level.actors.begin(),level.actors.end(),[&](const auto& a){return a.id==pair.second;});
        if(first->body.trigger || second->body.trigger || first->body.oneWay || second->body.oneWay
            || (first->body.mode==DetBodyMode2D::Static && second->body.mode==DetBodyMode2D::Static))continue;
        if(!bc.extended && !(first->body.mode==DetBodyMode2D::Kinematic && second->body.mode==DetBodyMode2D::Static)
            && !(second->body.mode==DetBodyMode2D::Kinematic && first->body.mode==DetBodyMode2D::Static))continue;
        invalid("Level initial ordinary solid penetration");}
    return result;
}
bool install(DeterministicSession& session,const DetLevel2D& level,const DetLevel2DOptions& options,const Prepared& prepared,
    const std::function<bool(DeterministicSession&)>& beforeActors={}){
    if(prepared.actions && !installDetActions2D(session,*prepared.actions))return false;
    if(prepared.interaction && !installDetInteraction2D(session,*prepared.interaction))return false;
    if(prepared.respawn){if(!installDetCharacterRespawn2D(session,*prepared.respawn))return false;}
    else if(!installDetCharacterController2D(session,level.character))return false;
    for(const auto& p:prepared.paths)if(!installDetPathMotion2D(session,p))return false;
    if(prepared.progress && !installDetProgress2D(session,*prepared.progress))return false;
    if(prepared.flow && !installDetProgressFlow2D(session,*prepared.flow))return false;
    if(prepared.respawnFlow && !installDetCharacterRespawnFlow2D(session,*prepared.respawnFlow))return false;
    if(beforeActors && !beforeActors(session))return false;
    if(session.sealed() || session.faulted() || session.nextTick()!=0)return false;
    for(const auto& chunk:prepared.chunks)if(!(session.registerTypedSchema(chunk.schema) && session.registerGlobalState(chunk.schema.id)))return false;
    const auto identityHash=level.formatVersion==1?0x41444c5630303031ull:level.formatVersion==2?0x41444c5630303032ull:level.formatVersion==3?0x41444c5630303033ull:0x41444c5630303034ull;
    if(!session.registerLogicProfile(options.identityLogicId,level.formatVersion,identityHash))return false;
    if(!session.registerValidator(options.identityValidatorId,level.formatVersion,[chunks=prepared.chunks](const DetSessionCheckpoint& cp,std::string& error){
        for(const auto& chunk:chunks){auto g=cp.globals.find(chunk.schema.id);if(g==cp.globals.end() || g->second!=chunk.words){error="Immutable level content global changed";return false;}
            for(const auto& [id,actor]:cp.actors){(void)id;auto block=actor.blocks.find(chunk.schema.id);
                if(block==actor.blocks.end() || block->second!=chunk.words){error="Immutable level content actor copy changed";return false;}}}return true;}))return false;
    for(const auto& [id,state]:prepared.actors)if(!session.addEntity(id,state))return false;return true;
}
Prepared checked(const DetLevel2D& level,const DetLevel2DOptions& options){auto prepared=prepare(level,options);
    DeterministicSession scratch(detLevelSessionConfig(level));if(!install(scratch,level,options,prepared) || !scratch.seal())throw std::invalid_argument("Level preflight: "+scratch.error());return prepared;}
struct TokenReader {
    std::string_view text;std::size_t offset=0;std::size_t& diagnosticOffset;
    TokenReader(std::string_view source,std::size_t& location):text(source),diagnosticOffset(location){if(source.empty() || source.size()>kDetLevel2DMaxTextBytes)invalid("Level text size limit");
        for(std::size_t n=0;n<source.size();++n){const auto c=static_cast<unsigned char>(source[n]);
            if(c>126 || (c<32 && c!='\t' && c!='\r' && c!='\n')){diagnosticOffset=n;invalid("Level text must be ASCII");}}}
    std::string_view next(){while(offset<text.size() && (text[offset]==' ' || text[offset]=='\t' || text[offset]=='\r' || text[offset]=='\n'))++offset;
        const auto begin=offset;diagnosticOffset=begin;while(offset<text.size() && text[offset]!=' ' && text[offset]!='\t' && text[offset]!='\r' && text[offset]!='\n')++offset;
        if(begin==offset)invalid("Truncated level token");return text.substr(begin,offset-begin);}
    void expect(std::string_view value){if(next()!=value)invalid("Unexpected level token");}
    template<class T>T integer(){const auto token=next();T result{};const auto [end,error]=std::from_chars(token.data(),token.data()+token.size(),result);
        if(error!=std::errc{} || end!=token.data()+token.size())invalid("Invalid level integer");return result;}
    void word(std::uint32_t& value){value=integer<std::uint32_t>();}void signedWord(std::int32_t& value){value=integer<std::int32_t>();}
    void boolean(bool& value){auto x=integer<std::uint32_t>();if(x>1)invalid("Invalid level Boolean");value=x!=0;}
    void scalar(D& value){const auto token=next();std::uint32_t bits=0;const auto [end,error]=std::from_chars(token.data(),token.data()+token.size(),bits,16);
        if(token.size()!=8 || error!=std::errc{} || end!=token.data()+token.size())invalid("Float32 needs eight hex digits");value=D::fromBits(bits);if(!value.isFinite())invalid("Nonfinite level Float32");}
    void vector(V& value){scalar(value.x);scalar(value.y);}template<class E>void enumeration(E& value){value=static_cast<E>(integer<std::uint32_t>());}
    void end(){while(offset<text.size() && (text[offset]==' ' || text[offset]=='\t' || text[offset]=='\r' || text[offset]=='\n'))++offset;
        diagnosticOffset=offset;if(offset!=text.size())invalid("Trailing level tokens");}
};
struct TextWriter {std::ostringstream stream;
    TextWriter(){stream.imbue(std::locale::classic());}
    void word(std::uint32_t value){stream<<' '<<value;}void signedWord(std::int32_t value){stream<<' '<<value;}
    void boolean(bool value){word(value?1:0);}template<class E>void enumeration(E value){word(static_cast<std::uint32_t>(value));}
    void scalar(D value){constexpr char digits[]="0123456789abcdef";stream<<' ';for(int n=7;n>=0;--n)stream<<digits[(value.bits()>>(4*n))&15];}
    void vector(V value){scalar(value.x);scalar(value.y);}
};
} // namespace
DetSessionConfig detLevelSessionConfig(const DetLevel2D& level){const auto bytes=canonical(level);return {level.applicationVersion,level.inputVersion,level.stepNumerator,level.stepDenominator,hash(bytes)};}
DetPathMotion2DConfig detLevelPathConfig2D(const DetLevel2D& level,std::uint32_t path,const DetLevel2DOptions& options){const auto p=prepare(level,options);
    for(std::size_t n=0;n<level.paths.size();++n)if(level.paths[n].id==path)return p.paths[n];invalid("Unknown level path ID");}
DetActions2DConfig detLevelActionsConfig2D(const DetLevel2D& level,const DetLevel2DOptions& options){auto p=prepare(level,options);
    if(!p.actions)invalid("Level has no Actions content");return std::move(*p.actions);}
DetInteraction2DConfig detLevelInteractionConfig2D(const DetLevel2D& level,const DetLevel2DOptions& options){auto p=prepare(level,options);
    if(!p.interaction)invalid("Level has no Interaction rules");return std::move(*p.interaction);}
DetProgress2DConfig detLevelProgressConfig2D(const DetLevel2D& level,const DetLevel2DOptions& options){auto p=prepare(level,options);
    if(!p.progress)invalid("Level has no Progress content");return std::move(*p.progress);}
DetProgressFlow2DConfig detLevelProgressFlowConfig2D(const DetLevel2D& level,const DetLevel2DOptions& options){auto p=prepare(level,options);
    if(!p.flow)invalid("Level has no Progress Flow content");return std::move(*p.flow);}
DetCharacterRespawn2DConfig detLevelRespawnConfig2D(const DetLevel2D& level,const DetLevel2DOptions& options){auto p=prepare(level,options);
    if(!p.respawn)invalid("Level has no Respawn content");return std::move(*p.respawn);}
DetCharacterRespawnFlow2DConfig detLevelRespawnFlowConfig2D(const DetLevel2D& level,const DetLevel2DOptions& options){auto p=prepare(level,options);
    if(!p.respawnFlow)invalid("Level has no Respawn Flow content");return std::move(*p.respawnFlow);}
bool validateDetLevel2D(const DetLevel2D& level,std::string& error,const DetLevel2DOptions& options){try{(void)checked(level,options);error.clear();return true;}catch(const std::exception& e){error=e.what();return false;}}
bool decodeDetLevel2D(std::string_view text,DetLevel2D& output,std::string& error,const DetLevel2DOptions& options){std::size_t location=0;bool semantic=false;try{
    TokenReader r(text,location);DetLevel2D value;r.expect("AYDLEVEL");value.formatVersion=r.integer<std::uint32_t>();
    if(value.formatVersion<1 || value.formatVersion>4)invalid("Unknown level format version");
    r.expect("session");r.word(value.applicationVersion);r.word(value.inputVersion);r.word(value.stepNumerator);r.word(value.stepDenominator);
    r.expect("collision");collision(r,value.character.collision);if(value.formatVersion>=4)r.boolean(value.character.collision.characterRespawn);r.expect("character");character(r,value.character);
    r.expect("paths");const auto paths=r.integer<std::uint32_t>();if(paths>8)invalid("Level path count limit");
    for(std::uint32_t n=0;n<paths;++n){r.expect("path");DetLevelPath2D p;r.word(p.id);r.enumeration(p.playback);const auto count=r.integer<std::uint32_t>();
        if(count<2 || count>24)invalid("Level waypoint count limit");for(std::uint32_t k=0;k<count;++k){r.expect("point");DetPathWaypoint2D point;r.vector(point.offset);r.word(point.travelTicks);r.word(point.dwellTicks);p.waypoints.push_back(point);}value.paths.push_back(std::move(p));}
    r.expect("actors");const auto actors=r.integer<std::uint32_t>();if(!actors || actors>64)invalid("Level actor count limit");
    for(std::uint32_t n=0;n<actors;++n){r.expect("actor");DetLevelActor2D actor;actor.id=r.integer<std::uint64_t>();r.enumeration(actor.body.mode);r.enumeration(actor.body.shape);
        r.vector(actor.position);r.vector(actor.body.half);r.vector(actor.body.offset);r.vector(actor.body.velocity);r.word(actor.body.layer);r.word(actor.body.mask);
        r.boolean(actor.body.trigger);r.boolean(actor.body.oneWay);r.boolean(actor.character);r.word(actor.path);value.actors.push_back(actor);}
    if(value.formatVersion>=2){r.expect("actions");r.word(value.maxActionsPerTick);const auto targets=r.integer<std::uint32_t>();
        if(targets>8)invalid("Level target count limit");for(std::uint32_t n=0;n<targets;++n){r.expect("target");DetLevelActionTarget2D target;
            target.actor=r.integer<std::uint64_t>();r.word(target.collisionMask);r.boolean(target.collisionControl);r.word(target.path);value.actionTargets.push_back(target);}
        r.expect("rules");const auto count=r.integer<std::uint32_t>();if(count>kDetInteraction2DMaxRules)invalid("Level rule count limit");
        for(std::uint32_t n=0;n<count;++n){r.expect("rule");DetInteractionRule2D rule;r.word(rule.id);rule.trigger=r.integer<std::uint64_t>();
            rule.actor=r.integer<std::uint64_t>();r.enumeration(rule.phase);rule.action.target=r.integer<std::uint64_t>();r.enumeration(rule.action.kind);
            r.boolean(rule.once);r.word(rule.cooldownTicks);r.word(rule.delayTicks);value.rules.push_back(rule);}}
    if(value.formatVersion>=3){r.expect("progress");const auto counters=r.integer<std::uint32_t>(),objectives=r.integer<std::uint32_t>(),claims=r.integer<std::uint32_t>();
        if(counters>kDetProgress2DMaxCounters || objectives>kDetProgress2DMaxObjectives || claims>kDetProgress2DMaxClaims)invalid("Level progress count limit");
        for(std::uint32_t n=0;n<counters;++n){r.expect("counter");DetProgressCounter2D counter;r.word(counter.id);
            counter.minimum=r.integer<std::uint64_t>();counter.maximum=r.integer<std::uint64_t>();counter.initial=r.integer<std::uint64_t>();value.counters.push_back(counter);}
        for(std::uint32_t n=0;n<objectives;++n){r.expect("objective");DetProgressObjective2D objective;r.word(objective.id);const auto count=r.integer<std::uint32_t>();
            if(!count || count>kDetProgress2DMaxConditions)invalid("Level objective condition limit");
            for(std::uint32_t k=0;k<count;++k){r.expect("condition");DetProgressCondition2D condition;r.word(condition.counter);condition.atLeast=r.integer<std::uint64_t>();objective.conditions.push_back(condition);}value.objectives.push_back(std::move(objective));}
        for(std::uint32_t n=0;n<claims;++n){r.expect("claim");DetCollectibleClaim2D claim;r.word(claim.id);
            claim.trigger=r.integer<std::uint64_t>();claim.actor=r.integer<std::uint64_t>();r.word(claim.counter);claim.amount=r.integer<std::uint64_t>();value.claims.push_back(claim);}
        r.expect("flow");const auto bindings=r.integer<std::uint32_t>(),rules=r.integer<std::uint32_t>();
        if(bindings>kDetProgressFlow2DMaxClaims || rules>kDetProgressFlow2DMaxRules)invalid("Level flow count limit");
        for(std::uint32_t n=0;n<bindings;++n){r.expect("binding");DetProgressFlowClaimBinding2D binding;r.word(binding.claim);r.word(binding.requiredObjective);value.flowBindings.push_back(binding);}
        for(std::uint32_t n=0;n<rules;++n){r.expect("objectiveaction");DetProgressFlowRule2D rule;r.word(rule.id);r.word(rule.objective);
            rule.action.target=r.integer<std::uint64_t>();r.enumeration(rule.action.kind);r.boolean(rule.once);value.objectiveActions.push_back(rule);}}
    if(value.formatVersion>=4){r.expect("respawn");const auto spawns=r.integer<std::uint32_t>(),bindings=r.integer<std::uint32_t>(),regions=r.integer<std::uint32_t>();
        if(spawns>kDetCharacterRespawn2DMaxSpawns || bindings>kDetCharacterRespawnFlow2DMaxBindings || regions>kDetCharacterRespawnFlow2DMaxRegions)invalid("Level respawn count limit");
        for(std::uint32_t n=0;n<spawns;++n){r.expect("spawn");DetRespawnSpawn2D spawn;r.word(spawn.id);spawn.actor=r.integer<std::uint64_t>();r.vector(spawn.position);value.respawnSpawns.push_back(spawn);}
        for(std::uint32_t n=0;n<bindings;++n){r.expect("defaultspawn");DetRespawnFlowBinding2D binding;binding.actor=r.integer<std::uint64_t>();r.word(binding.spawn);value.respawnBindings.push_back(binding);}
        for(std::uint32_t n=0;n<regions;++n){r.expect("region");DetRespawnFlowRegion2D region;r.word(region.id);region.trigger=r.integer<std::uint64_t>();
            region.actor=r.integer<std::uint64_t>();r.enumeration(region.kind);r.word(region.spawn);value.respawnRegions.push_back(region);}}
    r.expect("end");r.end();semantic=true;(void)checked(value,options);output=std::move(value);error.clear();return true;
}catch(const std::exception& e){error=e.what();if(semantic)error+=" [semantic preflight]";
    else {std::size_t line=1,column=1;for(std::size_t n=0;n<std::min(location,text.size());++n){if(text[n]=='\n'){++line;column=1;}else ++column;}
        error+=" [line "+std::to_string(line)+", column "+std::to_string(column)+", byte "+std::to_string(location)+"]";}return false;}}
bool loadDetLevel2D(const std::filesystem::path& path,DetLevel2D& output,std::string& error,const DetLevel2DOptions& options){try{
    std::ifstream input(path,std::ios::binary);if(!input){error="Cannot open level resource";return false;}
    std::string text(kDetLevel2DMaxTextBytes+1,'\0');input.read(text.data(),static_cast<std::streamsize>(text.size()));const auto count=input.gcount();
    if(input.bad() || count<0 || static_cast<std::size_t>(count)>kDetLevel2DMaxTextBytes){error="Level file read/size limit";return false;}text.resize(static_cast<std::size_t>(count));return decodeDetLevel2D(text,output,error,options);
}catch(const std::exception& e){error=e.what();return false;}}
std::string encodeDetLevel2D(const DetLevel2D& level,const DetLevel2DOptions& options){(void)checked(level,options);TextWriter w;w.stream<<"AYDLEVEL "<<level.formatVersion<<"\nsession";
    w.word(level.applicationVersion);w.word(level.inputVersion);w.word(level.stepNumerator);w.word(level.stepDenominator);
    auto policy=level.character;w.stream<<"\ncollision";collision(w,policy.collision);if(level.formatVersion>=4)w.boolean(policy.collision.characterRespawn);w.stream<<"\ncharacter";character(w,policy);
    w.stream<<"\npaths";w.word(static_cast<std::uint32_t>(level.paths.size()));for(const auto& path:level.paths){w.stream<<"\npath";w.word(path.id);w.enumeration(path.playback);w.word(static_cast<std::uint32_t>(path.waypoints.size()));
        for(const auto& point:path.waypoints){w.stream<<"\npoint";w.vector(point.offset);w.word(point.travelTicks);w.word(point.dwellTicks);}}
    w.stream<<"\nactors";w.word(static_cast<std::uint32_t>(level.actors.size()));for(const auto& actor:level.actors){w.stream<<"\nactor "<<actor.id;w.enumeration(actor.body.mode);w.enumeration(actor.body.shape);
        w.vector(actor.position);w.vector(actor.body.half);w.vector(actor.body.offset);w.vector(actor.body.velocity);w.word(actor.body.layer);w.word(actor.body.mask);
        w.boolean(actor.body.trigger);w.boolean(actor.body.oneWay);w.boolean(actor.character);w.word(actor.path);}
    if(level.formatVersion>=2){w.stream<<"\nactions";w.word(level.maxActionsPerTick);w.word(static_cast<std::uint32_t>(level.actionTargets.size()));
        for(const auto& target:level.actionTargets){w.stream<<"\ntarget "<<target.actor;w.word(target.collisionMask);w.boolean(target.collisionControl);w.word(target.path);}
        w.stream<<"\nrules";w.word(static_cast<std::uint32_t>(level.rules.size()));for(const auto& rule:level.rules){w.stream<<"\nrule";w.word(rule.id);
            w.stream<<' '<<rule.trigger<<' '<<rule.actor;w.enumeration(rule.phase);w.stream<<' '<<rule.action.target;w.enumeration(rule.action.kind);
            w.boolean(rule.once);w.word(rule.cooldownTicks);w.word(rule.delayTicks);}}
    if(level.formatVersion>=3){w.stream<<"\nprogress";w.word(static_cast<std::uint32_t>(level.counters.size()));
        w.word(static_cast<std::uint32_t>(level.objectives.size()));w.word(static_cast<std::uint32_t>(level.claims.size()));
        for(const auto& counter:level.counters)w.stream<<"\ncounter "<<counter.id<<' '<<counter.minimum<<' '<<counter.maximum<<' '<<counter.initial;
        for(const auto& objective:level.objectives){w.stream<<"\nobjective";w.word(objective.id);w.word(static_cast<std::uint32_t>(objective.conditions.size()));
            for(const auto& condition:objective.conditions)w.stream<<"\ncondition "<<condition.counter<<' '<<condition.atLeast;}
        for(const auto& claim:level.claims)w.stream<<"\nclaim "<<claim.id<<' '<<claim.trigger<<' '<<claim.actor<<' '<<claim.counter<<' '<<claim.amount;
        w.stream<<"\nflow";w.word(static_cast<std::uint32_t>(level.flowBindings.size()));w.word(static_cast<std::uint32_t>(level.objectiveActions.size()));
        for(const auto& binding:level.flowBindings)w.stream<<"\nbinding "<<binding.claim<<' '<<binding.requiredObjective;
        for(const auto& rule:level.objectiveActions){w.stream<<"\nobjectiveaction "<<rule.id<<' '<<rule.objective<<' '<<rule.action.target;
            w.enumeration(rule.action.kind);w.boolean(rule.once);}}
    if(level.formatVersion>=4){w.stream<<"\nrespawn";w.word(static_cast<std::uint32_t>(level.respawnSpawns.size()));
        w.word(static_cast<std::uint32_t>(level.respawnBindings.size()));w.word(static_cast<std::uint32_t>(level.respawnRegions.size()));
        for(const auto& spawn:level.respawnSpawns){w.stream<<"\nspawn "<<spawn.id<<' '<<spawn.actor;w.vector(spawn.position);}
        for(const auto& binding:level.respawnBindings)w.stream<<"\ndefaultspawn "<<binding.actor<<' '<<binding.spawn;
        for(const auto& region:level.respawnRegions){w.stream<<"\nregion "<<region.id<<' '<<region.trigger<<' '<<region.actor;w.enumeration(region.kind);w.word(region.spawn);}}
    w.stream<<"\nend\n";
    auto result=w.stream.str();if(result.size()>kDetLevel2DMaxTextBytes)invalid("Encoded level text size limit");return result;}
bool configureDetLevel2D(DeterministicSession& session,const DetLevel2D& level,std::string& error,const DetLevel2DOptions& options,
    const std::function<bool(DeterministicSession&)>& beforeActors){try{
    if(session.sealed() || session.faulted() || session.nextTick()!=0 || session.inputVersion()!=level.inputVersion
        || session.fixedStep().bits()!=(D::fromUInt(level.stepNumerator)/D::fromUInt(level.stepDenominator)).bits())invalid("Level needs a fresh matching Session");
    const auto prepared=checked(level,options);if(!install(session,level,options,prepared,beforeActors)){
        error=session.error().empty()?"Level destination registration conflict":session.error();return false;}error.clear();return true;
}catch(const std::exception& e){error=e.what();return false;}}
} // namespace ayt::entity
