#include <AYEntity/DeterministicCharacterController2D.h>
#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>

namespace ayt::entity {
namespace {
using D=math::DetFloat32;
using V=math::DetVec2;
constexpr std::uint64_t logicHash=0x4443433244503031ull;
[[noreturn]] void invalid(){throw std::invalid_argument("deterministic character 2D: invalid policy, character or support state");}
D abs(D value){return value<D{}?-value:value;}
bool zero(V value){return value.x.bits()==0 && value.y.bits()==0;}
bool equal(V a,V b){return a.x.bits()==b.x.bits() && a.y.bits()==b.y.bits();}
bool bounded(D value,D upper){return value.isFinite() && value>=D{} && value<=upper;}
V boundOwn(V velocity,const DetCharacterController2DConfig& c) {
    if(!velocity.isFinite())invalid();
    const auto horizontal=c.maxHorizontalSpeed+c.maxPlatformSpeed;
    const auto vertical=c.jumpSpeed+c.maxFallSpeed+c.maxPlatformSpeed;
    return {std::clamp(velocity.x,-horizontal,horizontal),std::clamp(velocity.y,-vertical,vertical)};
}
bool validConfig(const DetCharacterController2DConfig& c) {
    const auto& p=c.collision;
    const std::array schemas{p.bodySchema,p.historySchema,c.stateSchema,c.policySchema};
    std::set<std::uint32_t> unique;
    for(auto id:schemas)if(id<2 || id==UINT32_MAX || !unique.insert(id).second)return false;
    if(!p.extended || !c.preSystemId || !p.systemId || !c.postSystemId
        || !(c.preSystemId<p.systemId && p.systemId<c.postSystemId)
        || !c.validatorId || c.validatorId==p.systemId || !c.logicProfileId || !p.eventType
        || p.maxBodies<1 || p.maxBodies>64 || p.maxTriggerPairs<1 || p.maxTriggerPairs>60
        || p.maxContactIterations<1 || p.maxContactIterations>256
        || static_cast<std::uint32_t>(c.leaveVelocity)>1)return false;
    const auto maximum=D::fromInt(1000000),length=D::fromInt(1000);
    if(!bounded(c.maxHorizontalSpeed,maximum) || !bounded(c.acceleration,maximum)
        || !bounded(c.deceleration,maximum) || !bounded(c.gravity,maximum)
        || !bounded(c.jumpSpeed,maximum) || !bounded(c.maxFallSpeed,maximum)
        || !bounded(c.maxPlatformSpeed,maximum) || !(c.maxPlatformSpeed>D{})
        || !bounded(c.groundContactTolerance,length) || !bounded(c.groundProbeDistance,length)
        || !bounded(c.snapDistance,length) || c.groundContactTolerance>c.groundProbeDistance
        || c.groundProbeDistance>c.snapDistance)return false;
    return true;
}
DetTypedStateSchema stateSchema(const DetCharacterController2DConfig& c) {
    return {c.stateSchema,kDetCharacterController2DProfileVersion,{
        {DetCharacterEnabled,false},{DetCharacterGrounded,false},{DetCharacterSupport,DetEntityRef{}},
        {DetCharacterVelocity,V{}},{DetCharacterSupportVelocity,V{}},{DetCharacterTargetSpeed,D{}},
        {DetCharacterJumpRequested,false},{DetCharacterAppliedCarry,V{}},{DetCharacterIntentVelocity,V{}},
        {DetCharacterJumped,false},{DetCharacterSnapped,false},{DetCharacterSupportTick,std::uint64_t{0}}}};
}
DetTypedStateSchema policySchema(const DetCharacterController2DConfig& c) {
    const auto& p=c.collision;
    return {c.policySchema,kDetCharacterController2DProfileVersion,{
        {1,kDetCharacterController2DProfileVersion},{2,p.bodySchema},{3,p.historySchema},{4,p.systemId},
        {5,c.preSystemId},{6,c.postSystemId},{7,c.validatorId},{8,p.maxBodies},{9,p.maxContactIterations},
        {10,p.eventType},{11,p.maxTriggerPairs},{12,c.gravity},{13,c.maxHorizontalSpeed},
        {14,c.acceleration},{15,c.deceleration},{16,c.jumpSpeed},{17,c.maxFallSpeed},
        {18,c.groundProbeDistance},{19,c.groundContactTolerance},{20,c.snapDistance},
        {21,c.maxPlatformSpeed},{22,static_cast<std::uint32_t>(c.leaveVelocity)},
        {23,p.priority},{24,c.logicProfileId}}};
}
DetTypedStateSchema bodySchema(const DetCharacterController2DConfig& c) {
    return {c.collision.bodySchema,kDetCollision2DExtendedProfileVersion,{
        {DetBodyMode,std::uint32_t{0}},{DetBodyHalf,V{}},{DetBodyOffset,V{}},{DetBodyVelocity,V{}},
        {DetBodyLayer,std::uint32_t{1}},{DetBodyMask,UINT32_MAX},{DetBodyTrigger,false}}};
}
void validateState(const DetCharacterState2D& s,const DetCharacterController2DConfig& c) {
    if(!s.velocity.isFinite() || !s.supportVelocity.isFinite()
        || abs(s.velocity.x)>c.maxHorizontalSpeed+c.maxPlatformSpeed
        || abs(s.velocity.y)>c.jumpSpeed+c.maxFallSpeed+c.maxPlatformSpeed
        || abs(s.supportVelocity.x)>c.maxPlatformSpeed || abs(s.supportVelocity.y)>c.maxPlatformSpeed)invalid();
    if(!s.enabled){if(s.grounded || s.support.value || !zero(s.velocity) || !zero(s.supportVelocity))invalid();}
    else if(s.grounded){if(!s.support.value || !s.velocity.y.isZero())invalid();}
    else if(s.support.value || !zero(s.supportVelocity))invalid();
}
void policyMatches(DetTickContext& context,const DetCharacterController2DConfig& c) {
    for(const auto& field:policySchema(c).fields)std::visit([&]<class T>(const T& expected){
        const auto actual=context.readGlobal<T>(c.policySchema,field.id);
        if(encodeDetStateValue(actual)!=encodeDetStateValue(expected))invalid();
    },field.initial);
}
struct SceneBody {SimEntityId id;DetCollisionBody2D body;math::DetAabb2 box;};
bool masks(const SceneBody& a,const SceneBody& b){return (a.body.layer&b.body.mask)!=0 && (b.body.layer&a.body.mask)!=0;}
bool supporting(const SceneBody& a,const SceneBody& b) {
    return a.id!=b.id && !a.body.trigger && !b.body.trigger && masks(a,b)
        && (b.body.mode==DetBodyMode2D::Static || b.body.mode==DetBodyMode2D::MovingObstacle);
}
D gap(const SceneBody& actor,const SceneBody& support) {
    const auto result=actor.box.min.y-support.box.max.y;if(!result.isFinite())invalid();return result;
}
bool overTop(const SceneBody& actor,const SceneBody& support,D distance) {
    if(!supporting(actor,support) || !(actor.box.min.x<support.box.max.x && support.box.min.x<actor.box.max.x))return false;
    const auto d=gap(actor,support);return d>=D{} && d<=distance;
}
void validatePlatform(const SceneBody& support,const DetCharacterController2DConfig& c) {
    if(!support.body.velocity.isFinite() || abs(support.body.velocity.x)>c.maxPlatformSpeed
        || abs(support.body.velocity.y)>c.maxPlatformSpeed)invalid();
}
std::vector<SceneBody> bodies(DetTickContext& context,const DetCharacterController2DConfig& c) {
    std::vector<SceneBody> result;
    for(auto id:context.entities()) {
        const auto schema=c.collision.bodySchema;
        DetCollisionBody2D body{static_cast<DetBodyMode2D>(context.read<std::uint32_t>(id,schema,DetBodyMode)),
            context.read<V>(id,schema,DetBodyHalf),context.read<V>(id,schema,DetBodyOffset),
            context.read<V>(id,schema,DetBodyVelocity),context.read<std::uint32_t>(id,schema,DetBodyLayer),
            context.read<std::uint32_t>(id,schema,DetBodyMask),context.read<bool>(id,schema,DetBodyTrigger)};
        if(body.mode==DetBodyMode2D::Disabled)continue;
        const auto p=context.pose(id).position;
        auto box=math::DetAabb2::fromCenterHalf(V{p.x,p.y}+body.offset,body.half);
        if(!box.hasArea() || !body.velocity.isFinite() || static_cast<std::uint32_t>(body.mode)>3)invalid();
        result.push_back({id,body,box});
    }
    if(result.size()>c.collision.maxBodies)invalid();
    return result;
}
const SceneBody* locate(const std::vector<SceneBody>& scene,SimEntityId id) {
    const auto it=std::lower_bound(scene.begin(),scene.end(),id,[](const auto& body,auto key){return body.id<key;});
    return it!=scene.end() && it->id==id?&*it:nullptr;
}
const SceneBody* support(const SceneBody& actor,const std::vector<SceneBody>& scene,
    const DetCharacterController2DConfig& c,V velocity,bool worldVelocity) {
    const SceneBody* result=nullptr;
    for(const auto& other:scene) {
        if(!overTop(actor,other,c.groundProbeDistance))continue;
        validatePlatform(other,c);
        if(gap(actor,other)>c.groundContactTolerance
            || (worldVelocity?velocity.y-other.body.velocity.y:velocity.y)>D{})continue;
        if(!result || gap(actor,other)<gap(actor,*result)
            || (gap(actor,other)==gap(actor,*result) && other.id<result->id))result=&other;
    }
    return result;
}
void clearInternal(DetTickContext& context,SimEntityId id,const DetCharacterController2DConfig& c) {
    context.write(id,c.stateSchema,DetCharacterAppliedCarry,V{});
    context.write(id,c.stateSchema,DetCharacterIntentVelocity,V{});
    context.write(id,c.stateSchema,DetCharacterJumped,false);
    context.write(id,c.stateSchema,DetCharacterSnapped,false);
}
void clearDisabled(DetTickContext& context,SimEntityId id,const DetCharacterController2DConfig& c) {
    for(const auto& field:stateSchema(c).fields)std::visit([&]<class T>(const T& value){context.write(id,c.stateSchema,field.id,value);},field.initial);
}
D approach(D value,D target,D acceleration,D deceleration,D dt) {
    const bool same=value.isZero() || target.isZero() || (value<D{})==(target<D{});
    const auto rate=same && abs(target)>abs(value)?acceleration:deceleration;
    const auto amount=rate*dt;if(!amount.isFinite())invalid();
    if(value<target){const auto next=value+amount;if(!next.isFinite())invalid();return std::min(next,target);}
    if(value>target){const auto next=value-amount;if(!next.isFinite())invalid();return std::max(next,target);}
    return value;
}
void pre(DetTickContext& context,const DetCharacterController2DConfig& c) {
    policyMatches(context,c);const auto scene=bodies(context,c);const auto dt=context.dt();
    for(auto id:context.entities()) {
        auto state=detCharacterState2D(context,id,c);
        if(!state.enabled){clearDisabled(context,id,c);continue;}
        const auto* actor=locate(scene,id);if(!actor || actor->body.mode!=DetBodyMode2D::Kinematic || actor->body.trigger)invalid();
        auto own=state.velocity;
        // Supported own Y is already relative to its carrier. Airborne own Y is
        // world velocity: compare it with each candidate's current vertical speed,
        // including a small jump inherited from a rapidly descending platform.
        const auto* contact=support(*actor,scene,c,own,!state.grounded);
        const SceneBody* carrier=contact;
        bool snapping=false;
        if(!carrier && state.grounded && own.y<=D{} && c.snapDistance>D{}) {
            const auto* previous=locate(scene,state.support.value);
            if(previous && overTop(*actor,*previous,c.snapDistance)) {
                validatePlatform(*previous,c);carrier=previous;snapping=true;
            }
        }
        if(state.grounded && !carrier && c.leaveVelocity==DetCharacterLeaveVelocity2D::Inherit)own=own+state.supportVelocity;
        const auto target=context.read<D>(id,c.stateSchema,DetCharacterTargetSpeed);
        if(!target.isFinite() || abs(target)>c.maxHorizontalSpeed)invalid();
        own.x=approach(own.x,target,c.acceleration,c.deceleration,dt);
        own.y=std::max(own.y-c.gravity*dt,-c.maxFallSpeed);
        const auto requested=context.read<bool>(id,c.stateSchema,DetCharacterJumpRequested);
        const bool jumped=requested && contact && c.jumpSpeed>D{};
        if(jumped) {
            own.y=c.jumpSpeed;
            if(c.leaveVelocity==DetCharacterLeaveVelocity2D::Inherit)own=own+contact->body.velocity;
            carrier=nullptr;contact=nullptr;snapping=false;
        }
        // Repeated leave/landing cycles retain a finite, explicit policy cap;
        // inherited momentum cannot grow without bound across carriers.
        own=boundOwn(own,c);
        const V carry=carrier?carrier->body.velocity:V{};
        V intended=own;
        if(snapping){const auto downward=-gap(*actor,*carrier)/dt;if(!downward.isFinite())invalid();intended.y=std::min(intended.y,downward);}
        const auto world=intended+carry;if(!world.isFinite())invalid();
        context.write(id,c.stateSchema,DetCharacterVelocity,own);
        context.write(id,c.stateSchema,DetCharacterGrounded,contact!=nullptr);
        context.write(id,c.stateSchema,DetCharacterSupport,DetEntityRef{carrier?carrier->id:0});
        context.write(id,c.stateSchema,DetCharacterSupportVelocity,carry);
        context.write(id,c.stateSchema,DetCharacterAppliedCarry,carry);
        context.write(id,c.stateSchema,DetCharacterIntentVelocity,own);
        context.write(id,c.stateSchema,DetCharacterJumped,jumped);
        context.write(id,c.stateSchema,DetCharacterSnapped,snapping);
        context.write(id,c.stateSchema,DetCharacterJumpRequested,false);
        context.write(id,c.collision.bodySchema,DetBodyVelocity,world);
    }
}
void post(DetTickContext& context,const DetCharacterController2DConfig& c) {
    const auto scene=bodies(context,c);
    for(auto id:context.entities()) {
        if(!context.read<bool>(id,c.stateSchema,DetCharacterEnabled)){clearDisabled(context,id,c);continue;}
        const auto* actor=locate(scene,id);if(!actor || actor->body.mode!=DetBodyMode2D::Kinematic || actor->body.trigger)invalid();
        const auto intent=context.read<V>(id,c.stateSchema,DetCharacterIntentVelocity);
        const auto carry=context.read<V>(id,c.stateSchema,DetCharacterAppliedCarry);
        const auto carrierId=context.read<DetEntityRef>(id,c.stateSchema,DetCharacterSupport).value;
        const auto jumped=context.read<bool>(id,c.stateSchema,DetCharacterJumped);
        const auto snapped=context.read<bool>(id,c.stateSchema,DetCharacterSnapped);
        const auto* grounded=jumped?nullptr:support(*actor,scene,c,actor->body.velocity,true);
        V own=actor->body.velocity-carry;if(!own.isFinite())invalid();
        // Blocked carrier motion is not reverse character acceleration.
        own.x=std::clamp(own.x,std::min(D{},intent.x),std::max(D{},intent.x));
        if(grounded)own.y=D{};
        else {
            if(snapped && own.y<intent.y)own.y=intent.y;
            if(carrierId && c.leaveVelocity==DetCharacterLeaveVelocity2D::Inherit) {
                own=actor->body.velocity;
                if(snapped && own.y<intent.y+carry.y)own.y=intent.y+carry.y;
            }
        }
        own=boundOwn(own,c);
        context.write(id,c.stateSchema,DetCharacterVelocity,own);
        context.write(id,c.stateSchema,DetCharacterGrounded,grounded!=nullptr);
        context.write(id,c.stateSchema,DetCharacterSupport,DetEntityRef{grounded?grounded->id:0});
        context.write(id,c.stateSchema,DetCharacterSupportVelocity,grounded?grounded->body.velocity:V{});
        context.write(id,c.stateSchema,DetCharacterSupportTick,grounded?context.tick()+1:std::uint64_t{0});
        clearInternal(context,id,c);
    }
}
void validateCheckpoint(const DetSessionCheckpoint& checkpoint,const DetCharacterController2DConfig& c,
    const DetStateLayout& states,const DetStateLayout& policies,const DetStateLayout& bodyLayout) {
    if(checkpoint.globals.at(c.policySchema)!=policies.defaults())invalid();
    std::vector<SceneBody> scene;
    for(const auto& [id,actor]:checkpoint.actors) {
        if(actor.blocks.at(c.policySchema)!=policies.defaults())invalid();
        const auto& words=actor.blocks.at(c.collision.bodySchema);
        const auto read=[&]<class T>(std::uint32_t field){return std::get<T>(bodyLayout.read(words,field));};
        DetCollisionBody2D body{static_cast<DetBodyMode2D>(read.operator()<std::uint32_t>(DetBodyMode)),
            read.operator()<V>(DetBodyHalf),read.operator()<V>(DetBodyOffset),read.operator()<V>(DetBodyVelocity),
            read.operator()<std::uint32_t>(DetBodyLayer),read.operator()<std::uint32_t>(DetBodyMask),read.operator()<bool>(DetBodyTrigger)};
        if(body.mode==DetBodyMode2D::Disabled)continue;
        const V position{D::fromBits(actor.pose.position[0]),D::fromBits(actor.pose.position[1])};
        auto box=math::DetAabb2::fromCenterHalf(position+body.offset,body.half);
        if(!box.hasArea())invalid();scene.push_back({id,body,box});
    }
    for(const auto& [id,actor]:checkpoint.actors) {
        const auto& words=actor.blocks.at(c.stateSchema);
        const auto read=[&]<class T>(std::uint32_t field){return std::get<T>(states.read(words,field));};
        DetCharacterState2D value{read.operator()<bool>(DetCharacterEnabled),read.operator()<bool>(DetCharacterGrounded),
            read.operator()<DetEntityRef>(DetCharacterSupport),read.operator()<V>(DetCharacterVelocity),read.operator()<V>(DetCharacterSupportVelocity)};
        validateState(value,c);
        if(!value.enabled){if(words!=states.defaults())invalid();continue;}
        const auto* self=locate(scene,id);if(!self || self->body.mode!=DetBodyMode2D::Kinematic || self->body.trigger)invalid();
        const auto target=read.operator()<D>(DetCharacterTargetSpeed);
        if(abs(target)>c.maxHorizontalSpeed || read.operator()<bool>(DetCharacterJumpRequested)
            || !zero(read.operator()<V>(DetCharacterAppliedCarry)) || !zero(read.operator()<V>(DetCharacterIntentVelocity))
            || read.operator()<bool>(DetCharacterJumped) || read.operator()<bool>(DetCharacterSnapped))invalid();
        const auto stamp=read.operator()<std::uint64_t>(DetCharacterSupportTick);
        if(value.grounded?stamp!=checkpoint.nextTick:stamp!=0)invalid();
        if(!value.grounded)continue;
        if(value.support.value==id)invalid();
        const auto* floor=locate(scene,value.support.value);
        if(!floor) {
            // Deferred despawn happens after post. Keep one explicit freshly
            // stamped retired-support boundary; next pre releases it.
            if(checkpoint.nextTick==0 || stamp!=checkpoint.nextTick
                || !std::binary_search(checkpoint.retiredIds.begin(),checkpoint.retiredIds.end(),value.support.value))invalid();
            continue;
        }
        validatePlatform(*floor,c);
        if(!overTop(*self,*floor,c.groundContactTolerance) || !equal(value.supportVelocity,floor->body.velocity)
            || (checkpoint.nextTick && self->body.velocity.y-floor->body.velocity.y>D{}))invalid();
    }
}
}
std::vector<std::uint64_t> detCharacterState2D(const DetCharacterController2DConfig& config,const DetCharacterState2D& state) {
    if(!validConfig(config))invalid();validateState(state,config);const auto schema=stateSchema(config);
    auto words=detStateDefaults(schema);
    writeDetState(schema,words,DetCharacterEnabled,state.enabled);
    writeDetState(schema,words,DetCharacterGrounded,state.grounded);
    writeDetState(schema,words,DetCharacterSupport,state.support);
    writeDetState(schema,words,DetCharacterVelocity,state.velocity);
    writeDetState(schema,words,DetCharacterSupportVelocity,state.supportVelocity);return words;
}
DetCharacterState2D detCharacterState2D(DetTickContext& context,SimEntityId actor,const DetCharacterController2DConfig& config) {
    if(!validConfig(config))invalid();const auto s=config.stateSchema;
    return {context.read<bool>(actor,s,DetCharacterEnabled),context.read<bool>(actor,s,DetCharacterGrounded),
        context.read<DetEntityRef>(actor,s,DetCharacterSupport),context.read<V>(actor,s,DetCharacterVelocity),
        context.read<V>(actor,s,DetCharacterSupportVelocity)};
}
void detCharacterInput2D(DetTickContext& context,SimEntityId actor,const DetCharacterController2DConfig& config,DetCharacterInput2D input) {
    if(!validConfig(config) || !input.targetSpeed.isFinite() || abs(input.targetSpeed)>config.maxHorizontalSpeed)invalid();
    policyMatches(context,config);
    if(!context.read<bool>(actor,config.stateSchema,DetCharacterEnabled))invalid();
    context.write(actor,config.stateSchema,DetCharacterTargetSpeed,input.targetSpeed);
    context.write(actor,config.stateSchema,DetCharacterJumpRequested,input.jumpPressed);
}
bool installDetCharacterController2D(DeterministicSession& session,DetCharacterController2DConfig config) {
    if(!validConfig(config))return false;
    const std::array schemas{config.collision.bodySchema,config.collision.historySchema,config.stateSchema,config.policySchema};
    const std::array systems{config.preSystemId,config.collision.systemId,config.postSystemId};
    const std::array validators{config.collision.systemId,config.validatorId};
    const std::array logic{config.logicProfileId};
    if(!session.characterRegistrationAvailable(schemas,systems,validators,logic))return false;
    const auto state=stateSchema(config),policy=policySchema(config);
    const DetStateLayout stateLayout(state),policyLayout(policy),bodyLayout(bodySchema(config));
    if(!(session.registerTypedSchema(state) && session.registerTypedSchema(policy)
        && session.registerGlobalState(config.policySchema)
        && installDetCollision2D(session,config.collision)
        && session.registerLogicProfile(config.logicProfileId,kDetCharacterController2DProfileVersion,logicHash)
        && session.registerValidator(config.validatorId,kDetCharacterController2DProfileVersion,
            [config,stateLayout,policyLayout,bodyLayout](const auto& checkpoint,std::string& error){
                try {validateCheckpoint(checkpoint,config,stateLayout,policyLayout,bodyLayout);return true;}
                catch(const std::exception& e){error=e.what();return false;}})
        && session.registerSystem(config.preSystemId,config.collision.priority,[config](DetTickContext& context){pre(context,config);return true;})
        && session.registerSystem(config.postSystemId,config.collision.priority,[config](DetTickContext& context){post(context,config);return true;})))return false;
    return true;
}
} // namespace ayt::entity
