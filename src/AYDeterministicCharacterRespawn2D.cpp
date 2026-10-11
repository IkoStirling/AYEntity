// design reference: AYEntity/design.md Stage44 finite registered respawn closure.
#include <AYEntity/DeterministicCharacterRespawn2D.h>
#include <AYEntity/DeterministicCollisionQuery2D.h>
#include "detail/DetCharacterRespawn2D.h"
#include "detail/DetSessionWire.h"
#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace ayt::entity {
namespace {
using D=math::DetFloat32;using V=math::DetVec2;
constexpr std::uint64_t logicHash=0x4452533244503031ULL;
[[noreturn]] void invalid(){throw std::invalid_argument("deterministic respawn 2D: invalid policy, identity, result or pipeline order");}
bool zero(V value){return value.x.bits()==0 && value.y.bits()==0;}
std::uint64_t stamp(std::uint64_t tick){if(tick==UINT64_MAX)invalid();return tick+1;}
template<class Read>DetCharacterRespawnState2D result(Read read){return {
    static_cast<DetRespawnResult2D>(std::get<std::uint32_t>(read(DetRespawnLastResult))),
    std::get<std::uint32_t>(read(DetRespawnLastSpawn)),std::get<std::uint64_t>(read(DetRespawnResultTick))};}
void descriptor(const detwire::ManifestLayout& manifest,const DetTypedStateSchema& schema){
    const auto& fields=manifest.schemas.at(schema.id);if(manifest.schemaVersions.at(schema.id)!=schema.version || fields.size()!=schema.fields.size())invalid();
    for(std::size_t i=0;i<fields.size();++i)if(fields[i].id!=schema.fields[i].id || fields[i].type!=detStateType(schema.fields[i].initial)
        || fields[i].width!=encodeDetStateValue(schema.fields[i].initial).size())invalid();
}
void validate(const DetSessionCheckpoint& cp,const DetCharacterRespawn2DConfig& c){
    (void)DetCollisionQuery2D(cp,c.character.collision);
    const auto manifest=detwire::manifestLayout(cp.manifest);const auto state=detail::respawnStateSchema(c),policy=detail::respawnPolicySchema(c);
    descriptor(manifest,state);descriptor(manifest,policy);const auto& k=c.character;const auto& p=k.collision;
    const auto characterState=detail::respawnCharacterStateSchema(k),characterPolicy=detail::respawnCharacterPolicySchema(k);
    descriptor(manifest,characterState);descriptor(manifest,characterPolicy);
    if(!manifest.globals.contains(c.policySchema) || manifest.validatorVersions.at(c.validatorId)!=1
        || manifest.logicProfiles.at(c.logicProfileId)!=std::pair<std::uint32_t,std::uint64_t>{1,logicHash}
        || manifest.validatorVersions.at(p.systemId)!=9 || manifest.validatorVersions.at(k.validatorId)!=8
        || manifest.systems.at(k.preSystemId)!=p.priority || manifest.systems.at(p.systemId)!=p.priority
        || manifest.systems.at(k.postSystemId)!=p.priority || manifest.schemaVersions.at(k.stateSchema)!=8
        || manifest.schemaVersions.at(k.policySchema)!=8 || manifest.schemaVersions.at(p.bodySchema)!=9
        || manifest.schemaVersions.at(p.historySchema)!=9
        || manifest.logicProfiles.at(k.logicProfileId)!=std::pair<std::uint32_t,std::uint64_t>{8,0x4443433244503038ULL})invalid();
    const DetStateLayout states(state),policies(policy),characters(characterState),characterPolicies(characterPolicy),bodies(detCollisionBodySchema2D(p));
    if(cp.globals.at(c.policySchema)!=policies.defaults() || cp.globals.at(k.policySchema)!=characterPolicies.defaults())invalid();
    for(const auto& spawn:c.spawns){const auto found=cp.actors.find(spawn.actor);
        if(found==cp.actors.end()){if(!std::binary_search(cp.retiredIds.begin(),cp.retiredIds.end(),spawn.actor))invalid();}
        else {const auto& words=found->second.blocks.at(p.bodySchema);
            if(std::get<std::uint32_t>(bodies.read(words,DetBodyMode))!=static_cast<std::uint32_t>(DetBodyMode2D::Kinematic)
                || std::get<bool>(bodies.read(words,DetBodyTrigger)))invalid();}}
    // Its own validator owns complete support semantics. This descriptor and full
    // literal policy additionally bind the public getter's supplied configuration.
    const auto getCharacter=[&]<class T>(const DetActorState& actor,std::uint32_t field){
        return std::get<T>(characters.read(actor.blocks.at(k.stateSchema),field));
    };
    for(const auto& [id,actor]:cp.actors){
        if(actor.blocks.at(c.policySchema)!=policies.defaults() || actor.blocks.at(k.policySchema)!=characterPolicies.defaults())invalid();const auto& words=actor.blocks.at(c.stateSchema);
        const auto read=[&](std::uint32_t field){return states.read(words,field);};
        if(std::get<std::uint32_t>(read(DetRespawnPendingSpawn)) || std::get<std::uint64_t>(read(DetRespawnRequestTick))
            || std::get<bool>(read(DetRespawnApplied)) || std::get<bool>(read(DetRespawnPreCompleted)))invalid();
        const auto r=result(read);const auto code=static_cast<std::uint32_t>(r.result);
        if(!code){if(r.spawn || r.resultTick)invalid();continue;}
        const auto* spawn=detail::respawnSpawn(c,r.spawn);
        if(code>4 || !spawn || spawn->actor!=id || !r.resultTick || r.resultTick>cp.nextTick)invalid();
        if(r.result!=DetRespawnResult2D::Accepted || r.resultTick!=cp.nextTick)continue;
        const auto body=[&]<class T>(std::uint32_t f){return std::get<T>(bodies.read(actor.blocks.at(p.bodySchema),f));};
        if(!getCharacter.operator()<bool>(actor,DetCharacterEnabled) || !getCharacter.operator()<bool>(actor,DetCharacterStandingCaptured)
            || getCharacter.operator()<bool>(actor,DetCharacterGrounded) || getCharacter.operator()<bool>(actor,DetCharacterCrouched)
            || getCharacter.operator()<bool>(actor,DetCharacterCrouchHeld)
            || getCharacter.operator()<DetEntityRef>(actor,DetCharacterSupport).value
            || !zero(getCharacter.operator()<V>(actor,DetCharacterVelocity)) || !zero(getCharacter.operator()<V>(actor,DetCharacterSupportVelocity))
            || getCharacter.operator()<std::uint64_t>(actor,DetCharacterSupportTick)
            || getCharacter.operator()<D>(actor,DetCharacterTargetSpeed).bits()
            || getCharacter.operator()<std::uint32_t>(actor,DetCharacterCoyoteRemaining)
            || getCharacter.operator()<std::uint32_t>(actor,DetCharacterJumpBufferRemaining)
            || getCharacter.operator()<std::uint32_t>(actor,DetCharacterDropRemaining)
            || getCharacter.operator()<D>(actor,DetCharacterCoyoteSupportY).bits()
            || getCharacter.operator()<std::uint64_t>(actor,DetCharacterDropTick)
            || body.operator()<std::uint32_t>(DetBodyMode)!=static_cast<std::uint32_t>(DetBodyMode2D::Kinematic)
            || body.operator()<bool>(DetBodyTrigger) || !zero(body.operator()<V>(DetBodyVelocity))
            || body.operator()<DetEntityRef>(DetBodyIgnoredOneWay).value
            || body.operator()<V>(DetBodyHalf)!=getCharacter.operator()<V>(actor,DetCharacterStandingHalf)
            || body.operator()<V>(DetBodyOffset)!=getCharacter.operator()<V>(actor,DetCharacterStandingOffset)
            || actor.pose.position[0]!=spawn->position.x.bits() || actor.pose.position[1]!=spawn->position.y.bits()
            || actor.pose.previousPosition!=actor.pose.position || actor.pose.hasPreviousPosition)invalid();
        if((p.motionPolicy!=DetCollisionMotionPolicy2D::Fault || p.staticSlopes)
            && (body.operator()<std::uint32_t>(DetBodyMotionResult) || body.operator()<DetEntityRef>(DetBodyMotionOther).value
                || body.operator()<std::uint32_t>(DetBodyMotionAxis) || body.operator()<std::uint64_t>(DetBodyMotionResultTick)))invalid();
    }
}
}
namespace detail {
void validateRespawnCheckpoint(const DetSessionCheckpoint& checkpoint,const DetCharacterRespawn2DConfig& config){validate(checkpoint,config);}
bool validRespawnConfig(const DetCharacterRespawn2DConfig& c){
    const auto& k=c.character;const auto& p=k.collision;
    if(!k.advanced || !p.extended || !p.oneWayPlatforms || !p.characterRespawn || p.maxTriggerPairs>55
        || c.spawns.empty() || c.spawns.size()>kDetCharacterRespawn2DMaxSpawns || !c.validatorId || !c.logicProfileId
        || c.validatorId==k.validatorId || c.validatorId==p.systemId || c.logicProfileId==k.logicProfileId)return false;
    std::set<std::uint32_t> ids;for(auto id:{p.bodySchema,p.historySchema,k.stateSchema,k.policySchema,c.stateSchema,c.policySchema})
        if(id<2 || id==UINT32_MAX || !ids.insert(id).second)return false;
    std::uint32_t previous=0;for(const auto& spawn:c.spawns){if(!spawn.id || spawn.id<=previous || !spawn.actor || !spawn.position.isFinite())return false;previous=spawn.id;}return true;
}
DetTypedStateSchema respawnStateSchema(const DetCharacterRespawn2DConfig& c){return {c.stateSchema,1,{
    {DetRespawnPendingSpawn,std::uint32_t{}},{DetRespawnRequestTick,std::uint64_t{}},
    {DetRespawnLastResult,std::uint32_t{}},{DetRespawnLastSpawn,std::uint32_t{}},
    {DetRespawnResultTick,std::uint64_t{}},{DetRespawnApplied,false},{DetRespawnPreCompleted,false}}};}
DetTypedStateSchema respawnPolicySchema(const DetCharacterRespawn2DConfig& c){
    const auto& k=c.character;const auto& p=k.collision;
    DetTypedStateSchema schema{c.policySchema,1,{{1,std::uint32_t{1}},{2,c.stateSchema},{3,c.policySchema},
        {4,c.validatorId},{5,c.logicProfileId},{6,k.stateSchema},{7,k.policySchema},{8,p.bodySchema},{9,p.historySchema},
        {10,p.systemId},{11,k.postSystemId},{12,std::uint32_t{9}},{13,std::uint32_t{8}},
        {14,std::uint32_t{1}},{15,std::uint32_t{1}},{16,static_cast<std::uint32_t>(c.spawns.size())}}};
    for(std::size_t i=0;i<c.spawns.size();++i){const auto f=20+static_cast<std::uint32_t>(3*i);const auto& s=c.spawns[i];
        schema.fields.push_back({f,s.id});schema.fields.push_back({f+1,DetEntityRef{s.actor}});schema.fields.push_back({f+2,s.position});}
    for(const auto& field:respawnCharacterPolicySchema(k).fields)schema.fields.push_back({100+field.id,field.initial});return schema;
}
const DetRespawnSpawn2D* respawnSpawn(const DetCharacterRespawn2DConfig& c,std::uint32_t id){
    const auto it=std::lower_bound(c.spawns.begin(),c.spawns.end(),id,[](const auto& row,auto v){return row.id<v;});
    return it!=c.spawns.end() && it->id==id?&*it:nullptr;
}
void respawnPolicyMatches(DetTickContext& context,const DetCharacterRespawn2DConfig& c){
    if(!validRespawnConfig(c))invalid();const auto policy=respawnPolicySchema(c);
    for(const auto& f:policy.fields)std::visit([&](const auto& value){using T=std::decay_t<decltype(value)>;
        if(encodeDetStateValue(context.readGlobal<T>(c.policySchema,f.id))!=encodeDetStateValue(f.initial))invalid();},f.initial);
}
bool registerRespawnState(DeterministicSession& session,const DetCharacterRespawn2DConfig& c){
    if(!validRespawnConfig(c))return false;
    return session.registerTypedSchema(respawnStateSchema(c)) && session.registerTypedSchema(respawnPolicySchema(c))
        && session.registerGlobalState(c.policySchema) && session.registerLogicProfile(c.logicProfileId,1,logicHash)
        && session.registerValidator(c.validatorId,1,[c](const auto& cp,std::string& error){try{validate(cp,c);return true;}
            catch(const std::exception& e){error=e.what();return false;}});
}
void resolveRespawn(DetTickContext& context,SimEntityId actor,const DetCharacterRespawn2DConfig& c,std::uint32_t spawn,DetRespawnResult2D code){
    context.write(actor,c.stateSchema,DetRespawnPendingSpawn,std::uint32_t{});context.write(actor,c.stateSchema,DetRespawnRequestTick,std::uint64_t{});
    context.write(actor,c.stateSchema,DetRespawnLastResult,static_cast<std::uint32_t>(code));context.write(actor,c.stateSchema,DetRespawnLastSpawn,spawn);
    context.write(actor,c.stateSchema,DetRespawnResultTick,stamp(context.tick()));context.write(actor,c.stateSchema,DetRespawnApplied,code==DetRespawnResult2D::Accepted);
}
bool consumeRespawn(DetTickContext& context,SimEntityId actor,const DetCharacterRespawn2DConfig& c){
    if(!context.read<bool>(actor,c.stateSchema,DetRespawnPreCompleted))invalid();
    context.write(actor,c.stateSchema,DetRespawnPreCompleted,false);
    if(!context.read<bool>(actor,c.stateSchema,DetRespawnApplied))return false;
    const auto& k=c.character;const auto s=k.stateSchema;
    for(auto field:{DetCharacterGrounded,DetCharacterJumpRequested,DetCharacterJumped,DetCharacterSnapped,DetCharacterDropStarted,
        DetCharacterCrouched,DetCharacterCrouchHeld})context.write(actor,s,field,false);
    context.write(actor,s,DetCharacterSupport,DetEntityRef{});
    for(auto field:{DetCharacterVelocity,DetCharacterSupportVelocity,DetCharacterAppliedCarry,DetCharacterIntentVelocity})context.write(actor,s,field,V{});
    context.write(actor,s,DetCharacterTargetSpeed,D{});context.write(actor,s,DetCharacterSupportTick,std::uint64_t{});
    for(auto field:{DetCharacterCoyoteRemaining,DetCharacterJumpBufferRemaining,DetCharacterDropRemaining})context.write(actor,s,field,std::uint32_t{});
    context.write(actor,s,DetCharacterCoyoteSupportY,D{});context.write(actor,s,DetCharacterDropTick,std::uint64_t{});
    context.write(actor,s,DetCharacterJumpLatch,context.read<bool>(actor,s,DetCharacterJumpHeld));
    context.write(actor,s,DetCharacterDropLatch,context.read<bool>(actor,s,DetCharacterDropHeld));
    if(k.collision.stepUp){context.write(actor,s,DetCharacterStepped,false);
        context.write(actor,k.collision.bodySchema,DetBodyStepRequested,false);context.write(actor,k.collision.bodySchema,DetBodyStepped,false);}
    if(k.collision.staticSlopes)context.write(actor,s,DetCharacterGroundNormal,V{});
    context.write(actor,c.stateSchema,DetRespawnApplied,false);return true;
}
}
DetRespawnRequestCode2D detCharacterRespawnRequest2D(DetTickContext& context,SimEntityId actor,const DetCharacterRespawn2DConfig& c,std::uint32_t spawnId){
    detail::respawnPolicyMatches(context,c);const auto* spawn=detail::respawnSpawn(c,spawnId);
    if(!spawn || spawn->actor!=actor)return DetRespawnRequestCode2D::UnknownSpawn;
    const auto entities=context.entities();if(!std::binary_search(entities.begin(),entities.end(),actor))return DetRespawnRequestCode2D::Unavailable;
    if(context.read<bool>(actor,c.stateSchema,DetRespawnPreCompleted))invalid();
    if(context.read<std::uint32_t>(actor,c.stateSchema,DetRespawnPendingSpawn))return DetRespawnRequestCode2D::AlreadyRequested;
    if(!context.read<bool>(actor,c.character.stateSchema,DetCharacterEnabled))return DetRespawnRequestCode2D::Unavailable;
    context.write(actor,c.stateSchema,DetRespawnPendingSpawn,spawnId);context.write(actor,c.stateSchema,DetRespawnRequestTick,stamp(context.tick()));
    return DetRespawnRequestCode2D::Queued;
}
DetCharacterRespawnState2D detCharacterRespawnState2D(DetTickContext& context,SimEntityId actor,const DetCharacterRespawn2DConfig& c){
    detail::respawnPolicyMatches(context,c);return {static_cast<DetRespawnResult2D>(context.read<std::uint32_t>(actor,c.stateSchema,DetRespawnLastResult)),
        context.read<std::uint32_t>(actor,c.stateSchema,DetRespawnLastSpawn),context.read<std::uint64_t>(actor,c.stateSchema,DetRespawnResultTick)};
}
DetCharacterRespawnState2D detCharacterRespawnState2D(const DetSessionCheckpoint& cp,SimEntityId actor,const DetCharacterRespawn2DConfig& c){
    if(!detail::validRespawnConfig(c))invalid();validate(cp,c);const DetStateLayout state(detail::respawnStateSchema(c));
    return result([&](std::uint32_t field){return state.read(cp.actors.at(actor).blocks.at(c.stateSchema),field);});
}
} // namespace ayt::entity
