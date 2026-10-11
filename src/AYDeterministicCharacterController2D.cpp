#include <AYEntity/DeterministicCharacterController2D.h>
#include <AYMath/DetSlopeGeometry2D.h>
#include "detail/DetCharacterCollision2D.h"
#include "detail/DetCharacterRespawn2D.h"
#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>

namespace ayt::entity {
namespace {
using D=math::DetFloat32;
using V=math::DetVec2;
constexpr std::uint64_t logicHash=0x4443433244503031ull,advancedLogicHash=0x4443433244503032ull,
    stepLogicHash=0x4443433244503033ull,motionLogicHash=0x4443433244503034ull,slopeLogicHash=0x4443433244503035ull,
    movingSlopeLogicHash=0x4443433244503036ull,terrainLogicHash=0x4443433244503037ull,respawnLogicHash=0x4443433244503038ull;
std::uint32_t profile(const DetCharacterController2DConfig& c){return c.collision.characterRespawn?kDetCharacterController2DRespawnProfileVersion:(c.collision.continuousTerrain?kDetCharacterController2DTerrainProfileVersion:(c.collision.movingSlopes?kDetCharacterController2DMovingSlopeProfileVersion:(c.collision.staticSlopes?kDetCharacterController2DSlopeProfileVersion:
    (c.collision.motionPolicy!=DetCollisionMotionPolicy2D::Fault?kDetCharacterController2DMotionProfileVersion:(c.collision.stepUp?kDetCharacterController2DStepProfileVersion:
    (c.advanced?kDetCharacterController2DAdvancedProfileVersion:kDetCharacterController2DProfileVersion))))));}
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
    const auto half=D::fromInt(1)/D::fromInt(2);
    if(!c.maxSlopeRisePerRun.isFinite() || !(c.maxSlopeRisePerRun>D{}) || c.maxSlopeRisePerRun>D::fromInt(16)
        || (!p.staticSlopes && c.maxSlopeRisePerRun.bits()!=D::fromInt(1).bits())
        || static_cast<std::uint32_t>(p.motionPolicy)>1
        || (p.motionPolicy!=DetCollisionMotionPolicy2D::Fault && p.maxTriggerPairs>59)
        || (p.staticSlopes && p.maxTriggerPairs>58) || (p.movingSlopes && (!p.staticSlopes || p.maxTriggerPairs>57))
        || (p.continuousTerrain && (!p.movingSlopes || p.maxTriggerPairs>56))
        || (p.characterRespawn && (!c.advanced || !p.oneWayPlatforms || p.maxTriggerPairs>55)))return false;
    if(c.advanced!=p.oneWayPlatforms || c.coyoteTicks>65535 || c.jumpBufferTicks>65535
        || c.dropThroughTicks>65535 || !c.crouchHeightScale.isFinite()
        || !(c.crouchHeightScale>D{} && c.crouchHeightScale<D::fromInt(1))
        || (!c.advanced && (c.coyoteTicks || c.jumpBufferTicks || c.dropThroughTicks
            || c.crouchEnabled || c.crouchHeightScale.bits()!=half.bits())))return false;
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
    if(p.stepUp?(!c.advanced || p.maxTriggerPairs>59 || !bounded(p.maxStepHeight,length) || !(p.maxStepHeight>D{})):
        p.maxStepHeight.bits()!=0)return false;
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
    DetTypedStateSchema result{c.stateSchema,profile(c),{
        {DetCharacterEnabled,false},{DetCharacterGrounded,false},{DetCharacterSupport,DetEntityRef{}},
        {DetCharacterVelocity,V{}},{DetCharacterSupportVelocity,V{}},{DetCharacterTargetSpeed,D{}},
        {DetCharacterJumpRequested,false},{DetCharacterAppliedCarry,V{}},{DetCharacterIntentVelocity,V{}},
        {DetCharacterJumped,false},{DetCharacterSnapped,false},{DetCharacterSupportTick,std::uint64_t{0}}}};
    if(c.advanced) {
        const std::array<DetTypedStateField,15> fields{{
            {DetCharacterCoyoteRemaining,std::uint32_t{0}},{DetCharacterJumpBufferRemaining,std::uint32_t{0}},
            {DetCharacterJumpHeld,false},{DetCharacterDropHeld,false},{DetCharacterCrouchHeld,false},
            {DetCharacterDropStarted,false},{DetCharacterCrouched,false},{DetCharacterStandingCaptured,false},
            {DetCharacterStandingHalf,V{}},{DetCharacterStandingOffset,V{}},{DetCharacterDropRemaining,std::uint32_t{0}},
            {DetCharacterCoyoteSupportY,D{}},{DetCharacterJumpLatch,false},{DetCharacterDropLatch,false},
            {DetCharacterDropTick,std::uint64_t{0}}}};
        result.fields.insert(result.fields.end(),fields.begin(),fields.end());
    }
    if(c.collision.stepUp)result.fields.push_back({DetCharacterStepped,false});
    if(c.collision.staticSlopes)result.fields.push_back({DetCharacterGroundNormal,V{}});
    return result;
}
DetTypedStateSchema policySchema(const DetCharacterController2DConfig& c) {
    const auto& p=c.collision;
    DetTypedStateSchema result{c.policySchema,profile(c),{
        {1,profile(c)},{2,p.bodySchema},{3,p.historySchema},{4,p.systemId},
        {5,c.preSystemId},{6,c.postSystemId},{7,c.validatorId},{8,p.maxBodies},{9,p.maxContactIterations},
        {10,p.eventType},{11,p.maxTriggerPairs},{12,c.gravity},{13,c.maxHorizontalSpeed},
        {14,c.acceleration},{15,c.deceleration},{16,c.jumpSpeed},{17,c.maxFallSpeed},
        {18,c.groundProbeDistance},{19,c.groundContactTolerance},{20,c.snapDistance},
        {21,c.maxPlatformSpeed},{22,static_cast<std::uint32_t>(c.leaveVelocity)},
        {23,p.priority},{24,c.logicProfileId}}};
    if(c.advanced) {
        const std::array<DetTypedStateField,7> fields{{{25,true},{26,c.coyoteTicks},{27,c.jumpBufferTicks},
            {28,c.dropThroughTicks},{29,c.crouchEnabled},{30,c.crouchHeightScale},{31,true}}};
        result.fields.insert(result.fields.end(),fields.begin(),fields.end());
    }
    if(p.stepUp){result.fields.push_back({32,true});result.fields.push_back({33,p.maxStepHeight});}
    if(p.motionPolicy!=DetCollisionMotionPolicy2D::Fault || p.staticSlopes)result.fields.push_back({34,static_cast<std::uint32_t>(p.motionPolicy)});
    if(p.staticSlopes){result.fields.push_back({35,true});result.fields.push_back({36,c.maxSlopeRisePerRun});}
    if(p.movingSlopes)result.fields.push_back({37,true});
    if(p.continuousTerrain)result.fields.push_back({38,true});
    if(p.characterRespawn)result.fields.push_back({39,true});
    return result;
}
DetTypedStateSchema bodySchema(const DetCharacterController2DConfig& c) {
    return detCollisionBodySchema2D(c.collision);
}
void validateState(const DetCharacterState2D& s,const DetCharacterController2DConfig& c) {
    if(!s.groundNormal.isFinite() || (!c.collision.staticSlopes && !zero(s.groundNormal))
        || (c.collision.staticSlopes && (s.grounded?!(s.groundNormal.y>D{}):!zero(s.groundNormal))))invalid();
    // A static step never carries momentum, including the fresh retired-support
    // boundary. Preserve the existing allowance for represented signed zero.
    if(s.stepped && (!c.collision.stepUp || !s.enabled || !s.grounded
        || !s.supportVelocity.x.isZero() || !s.supportVelocity.y.isZero()))invalid();
    if(!s.velocity.isFinite() || !s.supportVelocity.isFinite()
        || abs(s.velocity.x)>c.maxHorizontalSpeed+c.maxPlatformSpeed
        || abs(s.velocity.y)>c.jumpSpeed+c.maxFallSpeed+c.maxPlatformSpeed
        || abs(s.supportVelocity.x)>c.maxPlatformSpeed || abs(s.supportVelocity.y)>c.maxPlatformSpeed)invalid();
    if(!s.enabled){if(s.grounded || s.support.value || !zero(s.velocity) || !zero(s.supportVelocity))invalid();}
    else if(s.grounded){if(!s.support.value || !s.velocity.y.isZero())invalid();}
    else if(s.support.value || !zero(s.supportVelocity))invalid();
    const bool extras=s.crouched || s.standingCaptured || !zero(s.standingHalf) || !zero(s.standingOffset)
        || s.coyoteRemaining || s.jumpBufferRemaining || s.dropRemaining || s.jumpHeld || s.dropHeld
        || s.crouchHeld || s.jumpLatch || s.dropLatch || s.coyoteSupportY.bits()!=0 || s.dropTick;
    if(!c.advanced || !s.enabled){if(extras)invalid();return;}
    if(s.coyoteRemaining>c.coyoteTicks || s.jumpBufferRemaining>c.jumpBufferTicks
        || s.dropRemaining>=std::max(1u,c.dropThroughTicks)
        || !s.coyoteSupportY.isFinite() || abs(s.coyoteSupportY)>c.maxPlatformSpeed
        || (!s.coyoteRemaining && s.coyoteSupportY.bits()!=0)
        || s.jumpHeld!=s.jumpLatch || s.dropHeld!=s.dropLatch
        || (s.crouched && !c.crouchEnabled))invalid();
    if(!s.standingCaptured) {
        if(extras)invalid();
    }else if(!s.standingHalf.isFinite() || !s.standingOffset.isFinite()
        || !(s.standingHalf.x>D{} && s.standingHalf.y>D{}))invalid();
}
void policyMatches(DetTickContext& context,const DetCharacterController2DConfig& c) {
    for(const auto& field:policySchema(c).fields)std::visit([&]<class T>(const T& expected){
        const auto actual=context.readGlobal<T>(c.policySchema,field.id);
        if(encodeDetStateValue(actual)!=encodeDetStateValue(expected))invalid();
    },field.initial);
}
struct SceneBody {SimEntityId id;DetCollisionBody2D body;math::DetAabb2 box;};
bool slope(const SceneBody& body){return body.body.shape!=DetCollisionShape2D::Aabb;}
math::DetSlope2 slopeGeometry(const SceneBody& body){return {body.box,body.body.shape==DetCollisionShape2D::SlopeRisesRight};}
V groundNormal(const SceneBody& body){return slope(body)?slopeGeometry(body).surfaceNormal():V{D{},D::fromInt(1)};}
D tangentY(const SceneBody& body,D x){const auto n=groundNormal(body);const auto y=(-n.x/n.y)*x;if(!y.isFinite())invalid();return y;}
// The highest footprint corner caps the wedge's configuration-space support at
// its high endpoint. That local horizontal segment remains grounded even when
// a descending character has not yet crossed back onto the diagonal.
int slopeRegion(const SceneBody& actor,const SceneBody& floor){
    const bool right=floor.body.shape==DetCollisionShape2D::SlopeRisesRight;
    const auto corner=right?actor.box.max.x:actor.box.min.x;
    const auto high=right?floor.box.max.x:floor.box.min.x;
    if(corner==high)return 0;
    return (right?corner>high:corner<high)?1:-1;
}
D supportTangentY(const SceneBody& actor,const SceneBody& floor,D x){
    const auto region=slopeRegion(actor,floor);if(region>0)return {};
    const auto y=tangentY(floor,x);return region==0?std::max(D{},y):y;
}
D intentTangentY(const SceneBody& actor,const SceneBody& floor,D x){
    const auto region=slopeRegion(actor,floor);if(region>0)return {};
    const auto y=tangentY(floor,x);return region==0 && !(y<D{})?D{}:y;
}
bool walkable(const SceneBody& body,const DetCharacterController2DConfig& c){
    const auto n=groundNormal(body);return abs(n.x)/n.y<=c.maxSlopeRisePerRun;
}
bool overlaps(math::DetAabb2 box,const SceneBody& other){return slope(other)?slopeGeometry(other).interiorOverlapAabb(box):box.interiorOverlap(other.box);}
bool masks(const SceneBody& a,const SceneBody& b){return (a.body.layer&b.body.mask)!=0 && (b.body.layer&a.body.mask)!=0;}
bool supporting(const SceneBody& a,const SceneBody& b) {
    return a.id!=b.id && a.body.ignoredOneWay.value!=b.id && !a.body.trigger && !b.body.trigger && masks(a,b)
        && (b.body.mode==DetBodyMode2D::Static || b.body.mode==DetBodyMode2D::MovingObstacle);
}
D gap(const SceneBody& actor,const SceneBody& support) {
    const auto height=slope(support)?slopeGeometry(support).surfaceHeightAtFootprint(actor.box):std::optional<D>{support.box.max.y};
    if(!height)invalid();const auto result=actor.box.min.y-*height;if(!result.isFinite())invalid();return result;
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
        if(c.collision.oneWayPlatforms) {
            body.oneWay=context.read<bool>(id,schema,DetBodyOneWay);
            body.ignoredOneWay=context.read<DetEntityRef>(id,schema,DetBodyIgnoredOneWay);
        }
        if(c.collision.staticSlopes)body.shape=static_cast<DetCollisionShape2D>(context.read<std::uint32_t>(id,schema,DetBodyShape));
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
        if(!overTop(actor,other,c.groundProbeDistance) || !walkable(other,c))continue;
        validatePlatform(other,c);
        const auto relative=worldVelocity?velocity-other.body.velocity:velocity;
        if(gap(actor,other)>c.groundContactTolerance
            || (worldVelocity && slope(other)?relative.y>supportTangentY(actor,other,relative.x):relative.y>D{}))continue;
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
    if(c.advanced)context.write(id,c.stateSchema,DetCharacterDropStarted,false);
    if(c.collision.stepUp){
        context.write(id,c.collision.bodySchema,DetBodyStepRequested,false);
        context.write(id,c.collision.bodySchema,DetBodyStepped,false);
    }
}
void clearDisabled(DetTickContext& context,SimEntityId id,const DetCharacterController2DConfig& c) {
    for(const auto& field:stateSchema(c).fields)std::visit([&]<class T>(const T& value){context.write(id,c.stateSchema,field.id,value);},field.initial);
    if(c.collision.stepUp){
        context.write(id,c.collision.bodySchema,DetBodyStepRequested,false);
        context.write(id,c.collision.bodySchema,DetBodyStepped,false);
    }
}
D approach(D value,D target,D acceleration,D deceleration,D dt) {
    const bool same=value.isZero() || target.isZero() || (value<D{})==(target<D{});
    const auto rate=same && abs(target)>abs(value)?acceleration:deceleration;
    const auto amount=rate*dt;if(!amount.isFinite())invalid();
    if(value<target){const auto next=value+amount;if(!next.isFinite())invalid();return std::min(next,target);}
    if(value>target){const auto next=value-amount;if(!next.isFinite())invalid();return std::max(next,target);}
    return value;
}
SceneBody* locate(std::vector<SceneBody>& scene,SimEntityId id) {
    return const_cast<SceneBody*>(locate(static_cast<const std::vector<SceneBody>&>(scene),id));
}
V crouchHalf(V standing,const DetCharacterController2DConfig& c) {
    const V result{standing.x,standing.y*c.crouchHeightScale};
    if(!result.isFinite() || !(result.x>D{} && result.y>D{}))invalid();return result;
}
V crouchOffset(V half,V offset,const DetCharacterController2DConfig& c) {
    const V result{offset.x,offset.y-half.y+crouchHalf(half,c).y};
    if(!result.isFinite())invalid();return result;
}
bool interior(math::DetAabb2 a,math::DetAabb2 b) {
    return a.min.x<b.max.x && b.min.x<a.max.x && a.min.y<b.max.y && b.min.y<a.max.y;
}
bool separated(math::DetAabb2 a,math::DetAabb2 b) {
    return a.max.x<=b.min.x || b.max.x<=a.min.x || a.max.y<=b.min.y || b.max.y<=a.min.y;
}
void shape(DetTickContext& context,SceneBody& actor,const std::vector<SceneBody>& scene,
    const DetCharacterController2DConfig& c,DetCharacterState2D& state) {
    const auto schema=c.stateSchema;
    if(!state.standingCaptured) {
        if(context.tick()!=0)invalid();
        state.standingCaptured=true;state.standingHalf=actor.body.half;state.standingOffset=actor.body.offset;
        context.write(actor.id,schema,DetCharacterStandingCaptured,true);
        context.write(actor.id,schema,DetCharacterStandingHalf,state.standingHalf);
        context.write(actor.id,schema,DetCharacterStandingOffset,state.standingOffset);
    }
    const bool requested=c.crouchEnabled && state.crouchHeld;
    bool crouched=state.crouched;
    auto half=actor.body.half,offset=actor.body.offset;auto box=actor.box;
    if(requested!=crouched) {
        const auto p=context.pose(actor.id).position;
        // Algebraically fixed local feet are not necessarily exactly represented
        // fixed world feet after binary32 rounding. A failed candidate is a blocked
        // posture request: keep the complete old shape and do not search/move pose.
        try {
            const auto desiredHalf=requested?crouchHalf(state.standingHalf,c):state.standingHalf;
            const auto desiredOffset=requested?crouchOffset(state.standingHalf,state.standingOffset,c):state.standingOffset;
            const auto desired=math::DetAabb2::fromCenterHalf(V{p.x,p.y}+desiredOffset,desiredHalf);
            bool blocked=!desired.hasArea() || desired.min.y.bits()!=actor.box.min.y.bits();
            if(!requested && !blocked)for(const auto& other:scene)
                if(other.id!=actor.id && !other.body.trigger && masks(actor,other)
                    && overlaps(desired,other)){blocked=true;break;}
            if(!blocked){crouched=requested;half=desiredHalf;offset=desiredOffset;box=desired;}
        }catch(const std::invalid_argument&) {
            // Only pure candidate geometry above can throw this argument error.
            // Context accesses remain outside the guard and retain sticky failure.
        }
    }
    // Registered shape/offset change, never pose. Scratch reflects earlier stable-ID
    // shape decisions, so multiple simultaneous stand requests have one fixed order.
    context.write(actor.id,c.collision.bodySchema,DetBodyHalf,half);
    context.write(actor.id,c.collision.bodySchema,DetBodyOffset,offset);
    context.write(actor.id,schema,DetCharacterCrouched,crouched);
    actor.body.half=half;actor.body.offset=offset;actor.box=box;state.crouched=crouched;
}
void preAdvanced(DetTickContext& context,const DetCharacterController2DConfig& c) {
    policyMatches(context,c);auto scene=bodies(context,c);const auto dt=context.dt();
    for(auto id:context.entities()) {
        auto state=detCharacterState2D(context,id,c);
        if(!state.enabled){clearDisabled(context,id,c);context.write(id,c.collision.bodySchema,DetBodyIgnoredOneWay,DetEntityRef{});continue;}
        if(c.collision.stepUp){
            context.write(id,c.stateSchema,DetCharacterStepped,false);
            context.write(id,c.collision.bodySchema,DetBodyStepRequested,false);
            context.write(id,c.collision.bodySchema,DetBodyStepped,false);
        }
        auto* actor=locate(scene,id);if(!actor || actor->body.mode!=DetBodyMode2D::Kinematic || actor->body.trigger)invalid();
        shape(context,*actor,scene,c,state);
        auto ignored=actor->body.ignoredOneWay;
        auto dropRemaining=state.dropRemaining;
        if(ignored.value) {
            const auto* target=locate(scene,ignored.value);
            if(!target || !target->body.oneWay || (!dropRemaining && separated(actor->box,target->box))) {
                ignored={};dropRemaining=0;
                context.write(id,c.stateSchema,DetCharacterDropTick,std::uint64_t{0});
            }else if(dropRemaining)--dropRemaining;
        }
        actor->body.ignoredOneWay=ignored;
        context.write(id,c.collision.bodySchema,DetBodyIgnoredOneWay,ignored);
        auto own=state.velocity;
        const auto* contact=support(*actor,scene,c,own,!state.grounded);
        const bool jumpEdge=state.jumpHeld && !state.jumpLatch;
        const bool dropEdge=state.dropHeld && !state.dropLatch;
        context.write(id,c.stateSchema,DetCharacterJumpLatch,state.jumpHeld);
        context.write(id,c.stateSchema,DetCharacterDropLatch,state.dropHeld);
        bool dropped=dropEdge && contact && contact->body.oneWay;
        auto coyote=state.coyoteRemaining;
        auto supportY=state.coyoteSupportY;
        auto buffer=state.jumpBufferRemaining;
        if(contact && c.coyoteTicks){coyote=c.coyoteTicks;supportY=contact->body.velocity.y;}
        if(jumpEdge)buffer=c.jumpBufferTicks;
        if(dropped) {
            ignored={contact->id};actor->body.ignoredOneWay=ignored;
            context.write(id,c.collision.bodySchema,DetBodyIgnoredOneWay,ignored);
            dropRemaining=std::max(1u,c.dropThroughTicks)-1;
            contact=nullptr;coyote=buffer=0;supportY={};
        }
        const SceneBody* carrier=contact;bool snapping=false;
        if(!carrier && state.grounded && !dropped && own.y<=D{} && c.snapDistance>D{}) {
            const auto* previous=locate(scene,state.support.value);
            if(previous && walkable(*previous,c) && overTop(*actor,*previous,c.snapDistance)) {
                validatePlatform(*previous,c);carrier=previous;snapping=true;
            }
        }
        if(state.grounded && !carrier && c.leaveVelocity==DetCharacterLeaveVelocity2D::Inherit)own=own+state.supportVelocity;
        const auto target=context.read<D>(id,c.stateSchema,DetCharacterTargetSpeed);
        if(!target.isFinite() || abs(target)>c.maxHorizontalSpeed)invalid();
        own.x=approach(own.x,target,c.acceleration,c.deceleration,dt);
        own.y=std::max(own.y-c.gravity*dt,-c.maxFallSpeed);
        const bool jumped=!dropped && (jumpEdge || state.jumpBufferRemaining) && (contact || coyote) && c.jumpSpeed>D{};
        if(jumped) {
            own.y=c.jumpSpeed;
            if(c.leaveVelocity==DetCharacterLeaveVelocity2D::Inherit) {
                if(contact)own=own+contact->body.velocity;
                else if(carrier)own=own+carrier->body.velocity; // Snap retained the carrier until this jump.
                else own.y=own.y+supportY; // X inheritance already occurred at release.
            }
            carrier=nullptr;contact=nullptr;snapping=false;coyote=buffer=0;supportY={};
        }else if(!dropped) {
            if(!contact && coyote)--coyote;
            if(!jumpEdge && buffer)--buffer;
            if(!coyote)supportY={};
        }
        own=boundOwn(own,c);const V carry=carrier?carrier->body.velocity:V{};
        V intended=own;
        if(carrier && slope(*carrier) && !jumped){intended.y=intentTangentY(*actor,*carrier,intended.x);}
        if(snapping){const auto down=-gap(*actor,*carrier)/dt;if(!down.isFinite())invalid();intended.y=std::min(intended.y,down);}
        const auto world=intended+carry;if(!world.isFinite())invalid();
        context.write(id,c.stateSchema,DetCharacterVelocity,own);
        context.write(id,c.stateSchema,DetCharacterGrounded,contact!=nullptr);
        context.write(id,c.stateSchema,DetCharacterSupport,DetEntityRef{carrier?carrier->id:0});
        context.write(id,c.stateSchema,DetCharacterSupportVelocity,carry);
        context.write(id,c.stateSchema,DetCharacterAppliedCarry,carry);
        context.write(id,c.stateSchema,DetCharacterIntentVelocity,own);
        context.write(id,c.stateSchema,DetCharacterJumped,jumped);
        context.write(id,c.stateSchema,DetCharacterSnapped,snapping);
        context.write(id,c.stateSchema,DetCharacterDropStarted,dropped);
        context.write(id,c.stateSchema,DetCharacterJumpRequested,false);
        context.write(id,c.stateSchema,DetCharacterCoyoteRemaining,coyote);
        context.write(id,c.stateSchema,DetCharacterCoyoteSupportY,supportY);
        context.write(id,c.stateSchema,DetCharacterJumpBufferRemaining,buffer);
        context.write(id,c.stateSchema,DetCharacterDropRemaining,dropRemaining);
        context.write(id,c.collision.bodySchema,DetBodyVelocity,world);
        if(c.collision.stepUp) {
            const bool eligible=contact && contact->body.mode==DetBodyMode2D::Static && !contact->body.oneWay && !slope(*contact)
                && !jumped && !dropped && !ignored.value && own.y<=D{} && !own.x.isZero();
            context.write(id,c.collision.bodySchema,DetBodyStepRequested,eligible);
        }
    }
}
void postAdvanced(DetTickContext& context,const DetCharacterController2DConfig& c,const DetCharacterRespawn2DConfig* respawn=nullptr) {
    const auto scene=bodies(context,c);
    for(auto id:context.entities()) {
        if(respawn && detail::consumeRespawn(context,id,*respawn))continue;
        if(!context.read<bool>(id,c.stateSchema,DetCharacterEnabled)){clearDisabled(context,id,c);context.write(id,c.collision.bodySchema,DetBodyIgnoredOneWay,DetEntityRef{});continue;}
        const auto* actor=locate(scene,id);if(!actor || actor->body.mode!=DetBodyMode2D::Kinematic || actor->body.trigger)invalid();
        const auto intent=context.read<V>(id,c.stateSchema,DetCharacterIntentVelocity);
        const auto carry=context.read<V>(id,c.stateSchema,DetCharacterAppliedCarry);
        const auto carrierId=context.read<DetEntityRef>(id,c.stateSchema,DetCharacterSupport).value;
        const auto jumped=context.read<bool>(id,c.stateSchema,DetCharacterJumped);
        const auto dropped=context.read<bool>(id,c.stateSchema,DetCharacterDropStarted);
        const auto snapped=context.read<bool>(id,c.stateSchema,DetCharacterSnapped);
        const auto* grounded=jumped || dropped?nullptr:support(*actor,scene,c,actor->body.velocity,true);
        if(c.collision.stepUp) {
            const bool stepped=context.read<bool>(id,c.collision.bodySchema,DetBodyStepped);
            if(stepped && (!grounded || grounded->body.mode!=DetBodyMode2D::Static || grounded->body.oneWay))invalid();
            context.write(id,c.stateSchema,DetCharacterStepped,stepped);
        }
        V own=actor->body.velocity-carry;if(!own.isFinite())invalid();
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
        if(c.collision.staticSlopes)context.write(id,c.stateSchema,DetCharacterGroundNormal,grounded?groundNormal(*grounded):V{});
        if(grounded) {
            context.write(id,c.stateSchema,DetCharacterCoyoteRemaining,c.coyoteTicks);
            context.write(id,c.stateSchema,DetCharacterCoyoteSupportY,c.coyoteTicks?grounded->body.velocity.y:D{});
        }else if(jumped || dropped) {
            context.write(id,c.stateSchema,DetCharacterCoyoteRemaining,std::uint32_t{0});
            context.write(id,c.stateSchema,DetCharacterCoyoteSupportY,D{});
        }
        context.write(id,c.stateSchema,DetCharacterDropTick,actor->body.ignoredOneWay.value?context.tick()+1:std::uint64_t{0});
        clearInternal(context,id,c);
    }
}
void pre(DetTickContext& context,const DetCharacterController2DConfig& c) {
    if(c.advanced){preAdvanced(context,c);return;}
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
            if(previous && walkable(*previous,c) && overTop(*actor,*previous,c.snapDistance)) {
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
        if(carrier && slope(*carrier) && !jumped){intended.y=intentTangentY(*actor,*carrier,intended.x);}
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
void post(DetTickContext& context,const DetCharacterController2DConfig& c,const DetCharacterRespawn2DConfig* respawn=nullptr) {
    if(c.advanced){postAdvanced(context,c,respawn);return;}
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
        if(c.collision.staticSlopes)context.write(id,c.stateSchema,DetCharacterGroundNormal,grounded?groundNormal(*grounded):V{});
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
        if(c.collision.oneWayPlatforms) {
            body.oneWay=read.operator()<bool>(DetBodyOneWay);
            body.ignoredOneWay=read.operator()<DetEntityRef>(DetBodyIgnoredOneWay);
        }
        if(c.collision.staticSlopes)body.shape=static_cast<DetCollisionShape2D>(read.operator()<std::uint32_t>(DetBodyShape));
        if(c.collision.stepUp && (read.operator()<bool>(DetBodyStepRequested) || read.operator()<bool>(DetBodyStepped)))invalid();
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
        if(c.advanced) {
            value.crouched=read.operator()<bool>(DetCharacterCrouched);
            value.standingCaptured=read.operator()<bool>(DetCharacterStandingCaptured);
            value.standingHalf=read.operator()<V>(DetCharacterStandingHalf);
            value.standingOffset=read.operator()<V>(DetCharacterStandingOffset);
            value.coyoteRemaining=read.operator()<std::uint32_t>(DetCharacterCoyoteRemaining);
            value.jumpBufferRemaining=read.operator()<std::uint32_t>(DetCharacterJumpBufferRemaining);
            value.dropRemaining=read.operator()<std::uint32_t>(DetCharacterDropRemaining);
            value.jumpHeld=read.operator()<bool>(DetCharacterJumpHeld);
            value.dropHeld=read.operator()<bool>(DetCharacterDropHeld);
            value.crouchHeld=read.operator()<bool>(DetCharacterCrouchHeld);
            value.jumpLatch=read.operator()<bool>(DetCharacterJumpLatch);
            value.dropLatch=read.operator()<bool>(DetCharacterDropLatch);
            value.coyoteSupportY=read.operator()<D>(DetCharacterCoyoteSupportY);
            value.dropTick=read.operator()<std::uint64_t>(DetCharacterDropTick);
        }
        if(c.collision.stepUp)value.stepped=read.operator()<bool>(DetCharacterStepped);
        if(c.collision.staticSlopes)value.groundNormal=read.operator()<V>(DetCharacterGroundNormal);
        validateState(value,c);
        if(value.stepped && checkpoint.nextTick==0)invalid();
        if(!value.enabled){
            if(words!=states.defaults() || (c.advanced && std::get<DetEntityRef>(bodyLayout.read(actor.blocks.at(c.collision.bodySchema),DetBodyIgnoredOneWay)).value))invalid();
            continue;
        }
        const auto* self=locate(scene,id);if(!self || self->body.mode!=DetBodyMode2D::Kinematic || self->body.trigger)invalid();
        if(value.stepped && !self->body.velocity.y.isZero())invalid();
        const auto target=read.operator()<D>(DetCharacterTargetSpeed);
        if(abs(target)>c.maxHorizontalSpeed || read.operator()<bool>(DetCharacterJumpRequested)
            || !zero(read.operator()<V>(DetCharacterAppliedCarry)) || !zero(read.operator()<V>(DetCharacterIntentVelocity))
            || read.operator()<bool>(DetCharacterJumped) || read.operator()<bool>(DetCharacterSnapped))invalid();
        const auto stamp=read.operator()<std::uint64_t>(DetCharacterSupportTick);
        if(value.grounded?stamp!=checkpoint.nextTick:stamp!=0)invalid();
        if(c.advanced) {
            if(value.jumpHeld!=value.jumpLatch || value.dropHeld!=value.dropLatch
                || read.operator()<bool>(DetCharacterDropStarted))invalid();
            if(!value.standingCaptured) {
                if(checkpoint.nextTick!=0)invalid();
            }else {
                const auto half=value.crouched?crouchHalf(value.standingHalf,c):value.standingHalf;
                const auto offset=value.crouched?crouchOffset(value.standingHalf,value.standingOffset,c):value.standingOffset;
                if(!equal(self->body.half,half) || !equal(self->body.offset,offset))invalid();
            }
            const auto ignored=self->body.ignoredOneWay.value;
            if(!ignored) {
                if(value.dropRemaining || value.dropTick)invalid();
            }else {
                if(!checkpoint.nextTick || value.dropTick!=checkpoint.nextTick || ignored==id
                    || (value.grounded && value.support.value==ignored))invalid();
                const auto* target=locate(scene,ignored);
                if(target) {
                    if(!target->body.oneWay || target->body.trigger
                        || (target->body.mode!=DetBodyMode2D::Static && target->body.mode!=DetBodyMode2D::MovingObstacle))invalid();
                }else if(!std::binary_search(checkpoint.retiredIds.begin(),checkpoint.retiredIds.end(),ignored))invalid();
            }
            if(value.grounded && checkpoint.nextTick) {
                if(value.coyoteRemaining!=c.coyoteTicks || value.coyoteSupportY.bits()!=(c.coyoteTicks?value.supportVelocity.y:D{}).bits())invalid();
            }
        }
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
        if(value.stepped && (floor->body.mode!=DetBodyMode2D::Static || floor->body.oneWay || slope(*floor)))invalid();
        if(c.collision.staticSlopes && (!walkable(*floor,c) || !equal(value.groundNormal,groundNormal(*floor))))invalid();
        if(!overTop(*self,*floor,c.groundContactTolerance) || !equal(value.supportVelocity,floor->body.velocity)
            || (checkpoint.nextTick && (slope(*floor)?self->body.velocity.y-floor->body.velocity.y>supportTangentY(*self,*floor,self->body.velocity.x-floor->body.velocity.x):self->body.velocity.y-floor->body.velocity.y>D{})))invalid();
    }
}
}
DetTypedStateSchema detail::respawnCharacterStateSchema(const DetCharacterController2DConfig& config) {
    if(!validConfig(config) || !config.collision.characterRespawn)invalid();return stateSchema(config);
}
DetTypedStateSchema detail::respawnCharacterPolicySchema(const DetCharacterController2DConfig& config) {
    if(!validConfig(config) || !config.collision.characterRespawn)invalid();return policySchema(config);
}
std::vector<std::uint64_t> detCharacterState2D(const DetCharacterController2DConfig& config,const DetCharacterState2D& state) {
    if(!validConfig(config) || state.stepped)invalid();validateState(state,config);const auto schema=stateSchema(config);
    auto words=detStateDefaults(schema);
    writeDetState(schema,words,DetCharacterEnabled,state.enabled);
    writeDetState(schema,words,DetCharacterGrounded,state.grounded);
    writeDetState(schema,words,DetCharacterSupport,state.support);
    writeDetState(schema,words,DetCharacterVelocity,state.velocity);
    writeDetState(schema,words,DetCharacterSupportVelocity,state.supportVelocity);
    if(config.collision.staticSlopes)writeDetState(schema,words,DetCharacterGroundNormal,state.groundNormal);
    if(config.advanced) {
        writeDetState(schema,words,DetCharacterCrouched,state.crouched);
        writeDetState(schema,words,DetCharacterStandingCaptured,state.standingCaptured);
        writeDetState(schema,words,DetCharacterStandingHalf,state.standingHalf);
        writeDetState(schema,words,DetCharacterStandingOffset,state.standingOffset);
        writeDetState(schema,words,DetCharacterCoyoteRemaining,state.coyoteRemaining);
        writeDetState(schema,words,DetCharacterJumpBufferRemaining,state.jumpBufferRemaining);
        writeDetState(schema,words,DetCharacterDropRemaining,state.dropRemaining);
        writeDetState(schema,words,DetCharacterJumpHeld,state.jumpHeld);
        writeDetState(schema,words,DetCharacterDropHeld,state.dropHeld);
        writeDetState(schema,words,DetCharacterCrouchHeld,state.crouchHeld);
        writeDetState(schema,words,DetCharacterJumpLatch,state.jumpLatch);
        writeDetState(schema,words,DetCharacterDropLatch,state.dropLatch);
        writeDetState(schema,words,DetCharacterCoyoteSupportY,state.coyoteSupportY);
        writeDetState(schema,words,DetCharacterDropTick,state.dropTick);
    }
    return words;
}
DetCharacterState2D detCharacterState2D(DetTickContext& context,SimEntityId actor,const DetCharacterController2DConfig& config) {
    if(!validConfig(config))invalid();const auto s=config.stateSchema;
    DetCharacterState2D result{context.read<bool>(actor,s,DetCharacterEnabled),context.read<bool>(actor,s,DetCharacterGrounded),
        context.read<DetEntityRef>(actor,s,DetCharacterSupport),context.read<V>(actor,s,DetCharacterVelocity),
        context.read<V>(actor,s,DetCharacterSupportVelocity)};
    if(config.advanced) {
        result.crouched=context.read<bool>(actor,s,DetCharacterCrouched);
        result.standingCaptured=context.read<bool>(actor,s,DetCharacterStandingCaptured);
        result.standingHalf=context.read<V>(actor,s,DetCharacterStandingHalf);
        result.standingOffset=context.read<V>(actor,s,DetCharacterStandingOffset);
        result.coyoteRemaining=context.read<std::uint32_t>(actor,s,DetCharacterCoyoteRemaining);
        result.jumpBufferRemaining=context.read<std::uint32_t>(actor,s,DetCharacterJumpBufferRemaining);
        result.dropRemaining=context.read<std::uint32_t>(actor,s,DetCharacterDropRemaining);
        result.jumpHeld=context.read<bool>(actor,s,DetCharacterJumpHeld);
        result.dropHeld=context.read<bool>(actor,s,DetCharacterDropHeld);
        result.crouchHeld=context.read<bool>(actor,s,DetCharacterCrouchHeld);
        result.jumpLatch=context.read<bool>(actor,s,DetCharacterJumpLatch);
        result.dropLatch=context.read<bool>(actor,s,DetCharacterDropLatch);
        result.coyoteSupportY=context.read<D>(actor,s,DetCharacterCoyoteSupportY);
        result.dropTick=context.read<std::uint64_t>(actor,s,DetCharacterDropTick);
    }
    if(config.collision.stepUp)result.stepped=context.read<bool>(actor,s,DetCharacterStepped);
    if(config.collision.staticSlopes)result.groundNormal=context.read<V>(actor,s,DetCharacterGroundNormal);
    return result;
}
void detCharacterInput2D(DetTickContext& context,SimEntityId actor,const DetCharacterController2DConfig& config,DetCharacterInput2D input) {
    if(!validConfig(config) || !input.targetSpeed.isFinite() || abs(input.targetSpeed)>config.maxHorizontalSpeed
        || (!config.advanced && (input.crouchHeld || input.dropPressed)))invalid();
    policyMatches(context,config);
    if(!context.read<bool>(actor,config.stateSchema,DetCharacterEnabled))invalid();
    context.write(actor,config.stateSchema,DetCharacterTargetSpeed,input.targetSpeed);
    if(config.advanced) {
        context.write(actor,config.stateSchema,DetCharacterJumpHeld,input.jumpPressed);
        context.write(actor,config.stateSchema,DetCharacterDropHeld,input.dropPressed);
        context.write(actor,config.stateSchema,DetCharacterCrouchHeld,input.crouchHeld);
    }else context.write(actor,config.stateSchema,DetCharacterJumpRequested,input.jumpPressed);
}
namespace {
bool installPipeline(DeterministicSession& session,const DetCharacterController2DConfig& config,const DetCharacterRespawn2DConfig* respawn) {
    const auto state=stateSchema(config),policy=policySchema(config);
    const DetStateLayout stateLayout(state),policyLayout(policy),bodyLayout(bodySchema(config));
    const auto binding=respawn?std::optional{*respawn}:std::nullopt;
    if(!(session.registerTypedSchema(state) && session.registerTypedSchema(policy)
        && session.registerGlobalState(config.policySchema)
        && detail::installDetCharacterCollision2D(session,config.collision,config.stateSchema,config.maxSlopeRisePerRun,respawn)
        && session.registerLogicProfile(config.logicProfileId,profile(config),config.collision.characterRespawn?respawnLogicHash:(config.collision.continuousTerrain?terrainLogicHash:(config.collision.movingSlopes?movingSlopeLogicHash:(config.collision.staticSlopes?slopeLogicHash:
            (config.collision.motionPolicy!=DetCollisionMotionPolicy2D::Fault?motionLogicHash:(config.collision.stepUp?stepLogicHash:(config.advanced?advancedLogicHash:logicHash)))))))
        && session.registerValidator(config.validatorId,profile(config),
            [config,stateLayout,policyLayout,bodyLayout](const auto& checkpoint,std::string& error){
                try {validateCheckpoint(checkpoint,config,stateLayout,policyLayout,bodyLayout);return true;}
                catch(const std::exception& e){error=e.what();return false;}})
        && session.registerSystem(config.preSystemId,config.collision.priority,[config,binding](DetTickContext& context){pre(context,config);
            if(binding)for(auto id:context.entities())context.write(id,binding->stateSchema,DetRespawnPreCompleted,true);return true;})))return false;
    if(!session.registerSystem(config.postSystemId,config.collision.priority,[config,binding](DetTickContext& context){post(context,config,binding?&*binding:nullptr);return true;}))return false;
    return true;
}
}
bool installDetCharacterController2D(DeterministicSession& session,DetCharacterController2DConfig config) {
    if(!validConfig(config) || config.collision.characterRespawn)return false;
    const std::array schemas{config.collision.bodySchema,config.collision.historySchema,config.stateSchema,config.policySchema};
    const std::array systems{config.preSystemId,config.collision.systemId,config.postSystemId};
    const std::array validators{config.collision.systemId,config.validatorId};const std::array logic{config.logicProfileId};
    if(!session.characterRegistrationAvailable(schemas,systems,validators,logic))return false;
    return installPipeline(session,config,nullptr);
}
bool installDetCharacterRespawn2D(DeterministicSession& session,DetCharacterRespawn2DConfig config) {
    if(!validConfig(config.character) || !detail::validRespawnConfig(config))return false;const auto& c=config.character;
    const std::array schemas{c.collision.bodySchema,c.collision.historySchema,c.stateSchema,c.policySchema,config.stateSchema,config.policySchema};
    const std::array systems{c.preSystemId,c.collision.systemId,c.postSystemId};
    const std::array validators{c.collision.systemId,c.validatorId,config.validatorId};const std::array logic{c.logicProfileId,config.logicProfileId};
    if(!session.characterRegistrationAvailable(schemas,systems,validators,logic))return false;
    return detail::registerRespawnState(session,config) && installPipeline(session,c,&config);
}
} // namespace ayt::entity
