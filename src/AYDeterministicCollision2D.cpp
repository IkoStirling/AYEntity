// design reference: AYEntity/design.md Stage26/28/29 scratch-before-commit motion and static slopes.
#include <AYEntity/DeterministicCollision2D.h>
#include <AYEntity/DeterministicCollisionQuery2D.h>
#include "detail/DetSessionWire.h"
#include "detail/DetCharacterCollision2D.h"
#include "detail/DetCharacterRespawn2D.h"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <set>

namespace ayt::entity {
namespace {
using D=math::DetFloat32;
using V=math::DetVec2;
[[noreturn]] void invalid(){throw std::invalid_argument("deterministic collision 2D: invalid policy, body, history or unrepresentable motion");}
bool stopping(const DetCollision2DConfig& c){return c.motionPolicy==DetCollisionMotionPolicy2D::StopPrescribedOnConflict;}
bool motionState(const DetCollision2DConfig& c){return stopping(c) || c.staticSlopes;}
bool validConfig(const DetCollision2DConfig& c) {
    return c.bodySchema>=2 && c.bodySchema!=UINT32_MAX && c.historySchema>=2 && c.historySchema!=UINT32_MAX
        && c.bodySchema!=c.historySchema && c.systemId && c.eventType && c.maxBodies>=1 && c.maxBodies<=64
        && c.maxTriggerPairs>=1 && c.maxTriggerPairs<=(c.characterRespawn?55u:(c.continuousTerrain?56u:(c.movingSlopes?57u:(c.staticSlopes?58u:((c.stepUp || stopping(c))?59u:(c.extended?60u:61u))))))
        && c.maxContactIterations>=1 && c.maxContactIterations<=256
        && (!c.oneWayPlatforms || c.extended)
        && static_cast<std::uint32_t>(c.motionPolicy)<=1 && (!stopping(c) || c.extended)
        && (!c.staticSlopes || c.extended)
        && (!c.movingSlopes || c.staticSlopes) && (!c.continuousTerrain || c.movingSlopes)
        && (!c.characterRespawn || (c.extended && c.oneWayPlatforms))
        && (c.stepUp?(c.extended && c.oneWayPlatforms && c.maxStepHeight.isFinite()
            && c.maxStepHeight>D{} && c.maxStepHeight<=D::fromInt(1000)):c.maxStepHeight.bits()==0);
}
std::uint32_t profile(const DetCollision2DConfig& c){
    if(c.characterRespawn)return kDetCollision2DRespawnProfileVersion;
    if(c.continuousTerrain)return kDetCollision2DTerrainProfileVersion;
    if(c.movingSlopes)return kDetCollision2DMovingSlopeProfileVersion;
    if(c.staticSlopes)return kDetCollision2DSlopeProfileVersion;
    if(stopping(c))return kDetCollision2DMotionProfileVersion;
    if(c.stepUp)return kDetCollision2DStepProfileVersion;
    if(c.oneWayPlatforms)return kDetCollision2DOneWayProfileVersion;
    return c.extended?kDetCollision2DExtendedProfileVersion:kDetCollision2DProfileVersion;
}
void validateBody(const DetCollisionBody2D& b,bool extended=false,bool oneWayPlatforms=false,bool staticSlopes=false,bool movingSlopes=false) {
    if(static_cast<std::uint32_t>(b.mode)>(extended?3u:2u) || !b.half.isFinite() || !b.offset.isFinite() || !b.velocity.isFinite()
        || b.half.x<D{} || b.half.y<D{}) invalid();
    if(b.mode!=DetBodyMode2D::Disabled && (!(b.half.x>D{}) || !(b.half.y>D{}) || !b.layer)) invalid();
    if(b.mode==DetBodyMode2D::Static && (!b.velocity.x.isZero() || !b.velocity.y.isZero())) invalid();
    if(!oneWayPlatforms && (b.oneWay || b.ignoredOneWay.value))invalid();
    if(b.oneWay && (b.trigger || (b.mode!=DetBodyMode2D::Static && b.mode!=DetBodyMode2D::MovingObstacle)))invalid();
    if(b.ignoredOneWay.value && (b.trigger || b.mode!=DetBodyMode2D::Kinematic))invalid();
    if(static_cast<std::uint32_t>(b.shape)>2 || (!staticSlopes && b.shape!=DetCollisionShape2D::Aabb)
        || (b.shape!=DetCollisionShape2D::Aabb && ((b.mode!=DetBodyMode2D::Static && !(movingSlopes && b.mode==DetBodyMode2D::MovingObstacle)) || b.oneWay || b.trigger)))invalid();
}
DetTypedStateSchema bodySchema(const DetCollision2DConfig& c) {
    DetTypedStateSchema result{c.bodySchema,profile(c),{{DetBodyMode,std::uint32_t{0}}, {DetBodyHalf,V{}},
        {DetBodyOffset,V{}},{DetBodyVelocity,V{}},{DetBodyLayer,std::uint32_t{1}},
        {DetBodyMask,UINT32_MAX},{DetBodyTrigger,false}}};
    if(c.oneWayPlatforms){result.fields.push_back({DetBodyOneWay,false});result.fields.push_back({DetBodyIgnoredOneWay,DetEntityRef{}});}
    if(c.stepUp){result.fields.push_back({DetBodyStepRequested,false});result.fields.push_back({DetBodyStepped,false});}
    if(motionState(c)){
        result.fields.push_back({DetBodyMotionResult,std::uint32_t{0}});
        result.fields.push_back({DetBodyMotionOther,DetEntityRef{}});
        result.fields.push_back({DetBodyMotionAxis,std::uint32_t{0}});
        result.fields.push_back({DetBodyMotionResultTick,std::uint64_t{0}});
    }
    if(c.staticSlopes)result.fields.push_back({DetBodyShape,std::uint32_t{0}});
    return result;
}
DetTypedStateSchema historySchema(const DetCollision2DConfig& c) {
    DetTypedStateSchema s{c.historySchema,profile(c),{{1,profile(c)},
        {2,c.maxBodies},{3,c.maxTriggerPairs},{4,c.eventType},{5,std::uint32_t{0}},{6,math::kDetGeometry2DProfileVersion}}};
    if(c.extended){s.fields.push_back({7,true});s.fields.push_back({8,c.maxContactIterations});}
    if(c.stepUp)s.fields.push_back({9,c.maxStepHeight});
    for(std::uint32_t i=0;i<c.maxTriggerPairs;++i){s.fields.push_back({10+2*i,DetEntityRef{}});s.fields.push_back({11+2*i,DetEntityRef{}});}
    if(motionState(c))s.fields.push_back({128,static_cast<std::uint32_t>(c.motionPolicy)});
    if(c.staticSlopes){s.fields.push_back({129,true});s.fields.push_back({130,math::kDetSlopeGeometry2DProfileVersion});}
    if(c.movingSlopes)s.fields.push_back({131,true});
    if(c.continuousTerrain)s.fields.push_back({132,true});
    if(c.characterRespawn)s.fields.push_back({133,true});
    return s;
}
bool masks(std::uint32_t al,std::uint32_t am,std::uint32_t bl,std::uint32_t bm){return (am&bl)!=0 && (bm&al)!=0;}
struct Body {SimEntityId id;DetCollisionBody2D data;V position;D z;math::DetAabb2 box;bool stepRequested=false,stepped=false;
    DetCollisionMotionResult2D motionResult=DetCollisionMotionResult2D::Clear;
    SimEntityId motionOther=0;std::uint32_t motionAxis=0;
    bool slopeLimited=false,slopeGrounded=false;SimEntityId slopeSupport=0;
    D maxSlopeRisePerRun=D::fromInt(1);bool terrain=false,movingSlopeProfile=false,slopeLanding=false;V slopeCarry{};D slopeOwnIntentX{};};
math::DetSlope2 slope(const math::DetAabb2& box,DetCollisionShape2D shape){return {box,shape==DetCollisionShape2D::SlopeRisesRight};}
math::DetAabb2 bounds(const Body& b){auto box=math::DetAabb2::fromCenterHalf(b.position+b.data.offset,b.data.half);
    if(!box.hasArea() || (b.data.shape!=DetCollisionShape2D::Aabb && !slope(box,b.data.shape).valid()))invalid();return box;}
bool interior(const math::DetAabb2& a,DetCollisionShape2D shapeA,const math::DetAabb2& b,DetCollisionShape2D shapeB){
    if(shapeA==DetCollisionShape2D::Aabb)return shapeB==DetCollisionShape2D::Aabb?a.interiorOverlap(b):slope(b,shapeB).interiorOverlapAabb(a);
    if(shapeB==DetCollisionShape2D::Aabb)return slope(a,shapeA).interiorOverlapAabb(b);
    if(!a.interiorOverlap(b))return false;
    // Two static wedges are not solver pairs, but public proxy pairing still
    // returns actual shape overlap. SAT uses both inclined feature axes.
    const auto vertices=[](const math::DetAabb2& box,DetCollisionShape2D shape){return std::array<V,3>{box.min,V{box.max.x,box.min.y},
        shape==DetCollisionShape2D::SlopeRisesRight?box.max:V{box.min.x,box.max.y}};};
    const auto av=vertices(a,shapeA),bv=vertices(b,shapeB);
    for(const auto n:{slope(a,shapeA).surfaceNormal(),slope(b,shapeB).surfaceNormal()}){
        const auto project=[&](V p){const auto relative=p-a.min;if(!relative.isFinite())invalid();
            const auto x=n.x*relative.x,y=n.y*relative.y,value=x+y;if(!x.isFinite() || !y.isFinite() || !value.isFinite())invalid();return value;};
        auto amin=project(av[0]),amax=amin,bmin=project(bv[0]),bmax=bmin;
        for(unsigned i=1;i<3;++i){const auto ap=project(av[i]),bp=project(bv[i]);amin=std::min(amin,ap);amax=std::max(amax,ap);bmin=std::min(bmin,bp);bmax=std::max(bmax,bp);}
        if(amax<=bmin || bmax<=amin)return false;
    }
    return true;
}
bool blocks(const Body& a,const Body& b){return b.data.mode==DetBodyMode2D::Static && !a.data.trigger && !b.data.trigger
    && masks(a.data.layer,a.data.mask,b.data.layer,b.data.mask);}
D& axis(V& v,std::uint32_t i){if(i>1)invalid();return i==0?v.x:v.y;}
D axis(const V& v,std::uint32_t i){if(i>1)invalid();return i==0?v.x:v.y;}
D outward(D v,bool positive) {
    const auto bits=v.bits();
    auto next=v.isZero()?D::fromBits(positive?1u:0x80000001u):D::fromBits(bits+((positive!=v.signBit())?1u:UINT32_MAX));
    if(!next.isFinite())invalid();return next;
}
void snap(Body& b,const Body& wall,const math::DetHit2D& hit) {
    if(hit.axis>1)invalid();
    const auto a=hit.axis;const bool positive=(a==0?hit.normal.x:hit.normal.y)>D{};
    const auto plane=a==0?(positive?wall.box.max.x:wall.box.min.x):(positive?wall.box.max.y:wall.box.min.y);
    auto& p=axis(b.position,a);const auto half=axis(b.data.half,a),offset=axis(b.data.offset,a);
    p=(positive?plane+half:plane-half)-offset;
    // Round each software operation separately. Move outward only when rebuilding
    // the represented edge rounds into the wall; reject after a bounded 16 ULPs.
    for(unsigned i=0;i<=16;++i){b.box=bounds(b);const auto edge=a==0?(positive?b.box.min.x:b.box.max.x):(positive?b.box.min.y:b.box.max.y);
        if(positive?edge>=plane:edge<=plane)return;
        if(i==16)invalid();p=outward(p,positive);
    }
}
void snapSlope(Body& actor,const Body& wall) {
    const auto wedge=slope(wall.box,wall.data.shape);
    const auto height=wedge.surfaceHeightAtFootprint(actor.box);
    if(!height){const bool lowTouch=wall.data.shape==DetCollisionShape2D::SlopeRisesRight?
            actor.box.max.x==wall.box.min.x:actor.box.min.x==wall.box.max.x;
        if(actor.terrain && lowTouch && actor.box.min.y==wall.box.min.y && !wedge.interiorOverlapAabb(actor.box))return;
        invalid();}
    actor.position.y=(*height+actor.data.half.y)-actor.data.offset.y;
    // The represented triangle predicate is the authority. Height arithmetic
    // alone is not proof that the rebuilt box lies outside the filled wedge.
    for(unsigned i=0;i<=16;++i){actor.box=bounds(actor);if(!wedge.interiorOverlapAabb(actor.box))return;
        if(i==16)invalid();actor.position.y=outward(actor.position.y,true);}
}
V slopeTangent(V velocity,V normal) {
    const auto xx=normal.x*normal.x,yy=normal.y*normal.y,norm=xx+yy;
    const auto x=normal.x*velocity.x,y=normal.y*velocity.y,dot=x+y;
    if(!xx.isFinite() || !yy.isFinite() || !norm.isFinite() || !(norm>D{}) || !x.isFinite() || !y.isFinite() || !dot.isFinite())invalid();
    if(dot>D{})return velocity; // A steep-face X limit may leave outward jump motion.
    const auto ratio=dot/norm;
    const auto correction=normal*ratio;
    if(!ratio.isFinite() || !correction.isFinite())invalid();
    velocity=velocity-correction;
    // Preserve the established separately-rounded slope tangent convention.
    velocity.y=(-normal.x/normal.y)*velocity.x;if(!velocity.isFinite())invalid();return velocity;
}
void move(Body& b,const std::vector<Body>& bodies,D dt) {
    V remaining=b.data.velocity*dt;if(!remaining.isFinite())invalid();
    for(const auto& wall:bodies)if(blocks(b,wall) && b.box.interiorOverlap(wall.box))invalid();
    for(unsigned pass=0;pass<2;++pass) {
        std::optional<math::DetHit2D> best;const Body* target=nullptr;
        for(const auto& wall:bodies)if(blocks(b,wall)) {
            auto hit=math::detSweepAabb2D(b.box,remaining,wall.box);
            if(hit && hit->initialOverlap)invalid();
            if(hit && (!best || hit->fraction<best->fraction || (hit->fraction==best->fraction
                && (hit->axis<best->axis || (hit->axis==best->axis && wall.id<target->id))))){best=hit;target=&wall;}
        }
        if(!best){b.position=b.position+remaining;b.box=bounds(b);break;}
        b.position=b.position+remaining*best->fraction;snap(b,*target,*best);
        remaining=remaining*(D::fromInt(1)-best->fraction);axis(remaining,best->axis)=D{};axis(b.data.velocity,best->axis)=D{};
    }
    for(const auto& wall:bodies)if(blocks(b,wall) && b.box.interiorOverlap(wall.box))invalid();
}
#include "detail/DetCollision2DExtended.h"
template<class Read> std::vector<DetCollisionPair2D> readHistory(Read read,const DetCollision2DConfig& policy) {
    const auto u32=[&](auto field){return std::get<std::uint32_t>(read(field));};
    if(u32(1)!=profile(policy) || u32(2)!=policy.maxBodies || u32(3)!=policy.maxTriggerPairs
        || u32(4)!=policy.eventType || u32(6)!=math::kDetGeometry2DProfileVersion)invalid();
    if(policy.extended && (!std::get<bool>(read(7)) || u32(8)!=policy.maxContactIterations))invalid();
    if(policy.stepUp && std::get<D>(read(9)).bits()!=policy.maxStepHeight.bits())invalid();
    if(motionState(policy) && u32(128)!=static_cast<std::uint32_t>(policy.motionPolicy))invalid();
    if(policy.staticSlopes && (!std::get<bool>(read(129)) || u32(130)!=math::kDetSlopeGeometry2DProfileVersion))invalid();
    if(policy.movingSlopes && !std::get<bool>(read(131)))invalid();
    if(policy.continuousTerrain && !std::get<bool>(read(132)))invalid();
    if(policy.characterRespawn && !std::get<bool>(read(133)))invalid();
    const auto count=u32(5);if(count>policy.maxTriggerPairs)invalid();
    std::vector<DetCollisionPair2D> result;
    for(std::uint32_t i=0;i<policy.maxTriggerPairs;++i){DetCollisionPair2D pair{std::get<DetEntityRef>(read(10+2*i)).value,std::get<DetEntityRef>(read(11+2*i)).value};
        if(i<count){if(!pair.first || pair.first>=pair.second || (!result.empty() && !(result.back()<pair)))invalid();result.push_back(pair);}
        else if(pair.first || pair.second)invalid();
    }return result;
}
std::vector<DetCollisionPair2D> history(DetTickContext& c,const DetCollision2DConfig& policy) {
    return readHistory([&](std::uint32_t field)->DetStateValue {
        if((policy.staticSlopes && field==129) || (policy.movingSlopes && field==131) || (policy.continuousTerrain && field==132)
            || (policy.characterRespawn && field==133))return c.readGlobal<bool>(policy.historySchema,field);
        if(field>=10 && !(field==128 && motionState(policy)) && !(policy.staticSlopes && field==130))return c.readGlobal<DetEntityRef>(policy.historySchema,field);
        if(field==7)return c.readGlobal<bool>(policy.historySchema,field);
        if(field==9 && policy.stepUp)return c.readGlobal<D>(policy.historySchema,field);
        return c.readGlobal<std::uint32_t>(policy.historySchema,field);
    },policy);
}
std::vector<Body> readBodies(DetTickContext& c,const DetCollision2DConfig& policy) {
    std::vector<Body> bodies;
    for(const auto id:c.entities()) {
        const auto s=policy.bodySchema;
        DetCollisionBody2D data{static_cast<DetBodyMode2D>(c.read<std::uint32_t>(id,s,DetBodyMode)),
            c.read<V>(id,s,DetBodyHalf),c.read<V>(id,s,DetBodyOffset),c.read<V>(id,s,DetBodyVelocity),
            c.read<std::uint32_t>(id,s,DetBodyLayer),c.read<std::uint32_t>(id,s,DetBodyMask),c.read<bool>(id,s,DetBodyTrigger)};
        if(policy.oneWayPlatforms){data.oneWay=c.read<bool>(id,s,DetBodyOneWay);data.ignoredOneWay=c.read<DetEntityRef>(id,s,DetBodyIgnoredOneWay);}
        if(policy.staticSlopes)data.shape=static_cast<DetCollisionShape2D>(c.read<std::uint32_t>(id,s,DetBodyShape));
        bool requested=false;
        if(policy.stepUp){requested=c.read<bool>(id,s,DetBodyStepRequested);(void)c.read<bool>(id,s,DetBodyStepped);
            if(requested && (data.mode!=DetBodyMode2D::Kinematic || data.trigger || data.ignoredOneWay.value))invalid();}
        validateBody(data,policy.extended,policy.oneWayPlatforms,policy.staticSlopes,policy.movingSlopes);if(data.mode==DetBodyMode2D::Disabled)continue;
        const auto p=c.pose(id).position;Body b{id,data,{p.x,p.y},p.z,{},requested};b.box=bounds(b);b.movingSlopeProfile=policy.movingSlopes;bodies.push_back(b);
    }
    if(bodies.size()>policy.maxBodies)invalid();
    std::sort(bodies.begin(),bodies.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    return bodies;
}
struct TriggerResult {std::vector<DetCollisionPair2D> current;std::vector<std::vector<std::uint8_t>> events;};
TriggerResult triggerResult(const std::vector<Body>& solved,const std::vector<TraceStep>& trace,
    const std::vector<DetCollisionPair2D>& previous,const DetCollision2DConfig& policy) {
    TriggerResult result;auto& current=result.current;auto& events=result.events;
    std::vector<DetCollisionProxy2D> proxies;for(const auto& b:solved)proxies.push_back({b.id,b.box,b.data.layer,b.data.mask,b.data.trigger,b.data.shape});
    current=detCollisionPairs2D(proxies);
    std::erase_if(current,[&](const auto& p){auto trigger=[&](SimEntityId id){const auto it=std::lower_bound(solved.begin(),solved.end(),id,[](const Body& b,auto key){return b.id<key;});return it->data.trigger;};return !trigger(p.first)&&!trigger(p.second);});
    if(current.size()>policy.maxTriggerPairs)invalid();
    std::size_t i=0,j=0;
    while(i<previous.size() || j<current.size()){
        if(j==current.size() || (i<previous.size() && previous[i]<current[j]))events.push_back(encodeDetTriggerEvent2D({DetTriggerPhase2D::Exit,previous[i++]}));
        else if(i==previous.size() || current[j]<previous[i])events.push_back(encodeDetTriggerEvent2D({DetTriggerPhase2D::Enter,current[j++]}));
        else{events.push_back(encodeDetTriggerEvent2D({DetTriggerPhase2D::Stay,current[j++]}));++i;}
    }
    if(policy.extended){
        const auto transient=transientPairs(trace,previous,current);
        if(transient.size()+current.size()>policy.maxTriggerPairs)invalid();
        // Sort all transitions by stable pair; Enter precedes Exit within a crossing.
        std::vector<DetTriggerEvent2D> ordered;
        for(const auto& bytes:events){DetTriggerEvent2D e;if(!decodeDetTriggerEvent2D(bytes,e))invalid();ordered.push_back(e);}
        for(const auto& pair:transient){ordered.push_back({DetTriggerPhase2D::Enter,pair});ordered.push_back({DetTriggerPhase2D::Exit,pair});}
        std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a.pair<b.pair || (a.pair==b.pair && a.phase<b.phase);});
        events.clear();for(const auto& e:ordered)events.push_back(encodeDetTriggerEvent2D(e));
    }
    return result;
}
void tick(DetTickContext& c,const DetCollision2DConfig& policy,std::uint32_t controllerStateSchema,D maxSlopeRisePerRun,
    const DetCharacterRespawn2DConfig* respawn) {
    const auto previous=history(c,policy);auto bodies=readBodies(c,policy);
    if(policy.staticSlopes && controllerStateSchema)for(auto& b:bodies){
        b.slopeLimited=c.read<bool>(b.id,controllerStateSchema,10);b.maxSlopeRisePerRun=maxSlopeRisePerRun;
        b.slopeGrounded=c.read<bool>(b.id,controllerStateSchema,20);
        b.slopeSupport=c.read<DetEntityRef>(b.id,controllerStateSchema,30).value;
        b.terrain=policy.continuousTerrain;
        if(b.terrain){b.slopeCarry=c.read<V>(b.id,controllerStateSchema,80);const auto intent=c.read<V>(b.id,controllerStateSchema,90);
            b.slopeOwnIntentX=intent.x;
            b.slopeLanding=b.slopeLimited && intent.y<=D{} && !c.read<bool>(b.id,controllerStateSchema,100)
                && (!policy.oneWayPlatforms || !c.read<bool>(b.id,controllerStateSchema,180));}
    }
    auto start=bodies,solved=bodies;std::vector<TraceStep> trace;std::vector<SolidContact> contacts;
    std::vector<SimEntityId> cancelledCarry;
    if(policy.extended){
        try {moveTogether(solved,c.dt(),policy.maxContactIterations,trace,policy.stepUp?&contacts:nullptr,0,stopping(policy),policy.staticSlopes);}
        catch(const PrescribedMotionConflict& conflict){
            // Only this dedicated, classified conflict receives a retry. Start
            // from the entire original world, never the partially solved path.
            if(!stopping(policy))throw;
            for(const auto& stop:conflict.movers){
                const auto found=std::lower_bound(start.begin(),start.end(),stop.id,[](const Body& b,auto id){return b.id<id;});
                if(found==start.end() || found->id!=stop.id || found->data.mode!=DetBodyMode2D::MovingObstacle)invalid();
                found->data.velocity={};markMotion(*found,stop.result,stop.other,stop.axis);
            }
            for(const auto& result:conflict.actors){
                const auto found=std::lower_bound(start.begin(),start.end(),result.id,[](const Body& b,auto id){return b.id<id;});
                if(found==start.end() || found->id!=result.id)invalid();
                markMotion(*found,result.result,result.other,result.axis);
            }
            if(controllerStateSchema)for(auto& actor:start){
                if(!c.read<bool>(actor.id,controllerStateSchema,10))continue;
                const auto support=c.read<DetEntityRef>(actor.id,controllerStateSchema,30).value;
                const auto carry=c.read<V>(actor.id,controllerStateSchema,80);
                const auto stopped=std::find_if(conflict.movers.begin(),conflict.movers.end(),[&](const auto& result){return result.id==support;});
                // A jump or released support has no applied carry. Inherited
                // own velocity remains physical intent and is never cancelled.
                if(stopped!=conflict.movers.end() && (!carry.x.isZero() || !carry.y.isZero())){
                    actor.data.velocity=actor.data.velocity-carry;
                    if(actor.terrain)actor.slopeCarry={};
                    if(!actor.data.velocity.isFinite())invalid();cancelledCarry.push_back(actor.id);
                }
            }
            solved=start;trace.clear();contacts.clear();
            // No nested recovery: another conflict, pass limit or invalid motion
            // in this retry propagates as the normal executing Session fault.
            moveTogether(solved,c.dt(),policy.maxContactIterations,trace,policy.stepUp?&contacts:nullptr,0,true,policy.staticSlopes);
        }
    }
    else for(auto& b:solved)if(b.data.mode==DetBodyMode2D::Kinematic)move(b,bodies,c.dt());
    // design reference: AYEntity/design.md Stage26 static step-up. Validate the
    // ordinary result first: a trial must not conceal invalid baseline motion.
    auto triggers=triggerResult(solved,trace,previous,policy);
    if(respawn && !c.canEmitEvents(triggers.events.size(),triggers.events.size()*24))invalid();
    if(policy.stepUp){
        const auto candidates=stepCandidates(start,contacts,policy.maxStepHeight);
        if(!candidates.empty()){
            const auto& candidate=candidates[static_cast<std::size_t>(c.tick()%candidates.size())];
            auto trial=start;std::vector<TraceStep> trialTrace;
            if(tryStep(trial,trialTrace,solved,candidate,c.dt(),policy.maxContactIterations,stopping(policy),policy.staticSlopes)){
                // A valid geometric trial can still exceed the trigger budget.
                // Those pure candidate checks leave the validated baseline intact.
                try {auto trialTriggers=triggerResult(trial,trialTrace,previous,policy);
                    solved=std::move(trial);trace=std::move(trialTrace);triggers=std::move(trialTriggers);
                }catch(const std::invalid_argument&){}
            }
        }
    }
    struct Relocation {SimEntityId actor;std::uint32_t spawn;DetRespawnResult2D result;};
    std::vector<Relocation> relocations;
    if(respawn){
        detail::respawnPolicyMatches(c,*respawn);
        // A valid baseline must also fit the exact current Session emission budget.
        // Endpoint merging may reduce transient events; it must not hide that fault.
        if(!c.canEmitEvents(triggers.events.size(),triggers.events.size()*24))invalid();
        // The baseline and optional whole-world step have already passed. Pure
        // candidates never conceal executing faults or leave partial Context writes.
        for(auto id:c.entities()){
            if(!c.read<bool>(id,respawn->stateSchema,DetRespawnPreCompleted))invalid();
            const auto requested=c.read<std::uint32_t>(id,respawn->stateSchema,DetRespawnPendingSpawn);
            if(!requested){if(c.read<std::uint64_t>(id,respawn->stateSchema,DetRespawnRequestTick)
                || c.read<bool>(id,respawn->stateSchema,DetRespawnApplied))invalid();continue;}
            if(c.tick()==UINT64_MAX || c.read<std::uint64_t>(id,respawn->stateSchema,DetRespawnRequestTick)!=c.tick()+1)invalid();
            const auto* destination=detail::respawnSpawn(*respawn,requested);
            if(!destination || destination->actor!=id)invalid();
            auto found=std::lower_bound(solved.begin(),solved.end(),id,[](const Body& b,auto key){return b.id<key;});
            auto result=DetRespawnResult2D::Unavailable;
            if(found!=solved.end() && found->id==id && found->data.mode==DetBodyMode2D::Kinematic && !found->data.trigger
                && c.read<bool>(id,controllerStateSchema,DetCharacterEnabled)
                && c.read<bool>(id,controllerStateSchema,DetCharacterStandingCaptured)){
                auto trial=solved;auto& actor=trial[static_cast<std::size_t>(found-solved.begin())];
                actor.position=destination->position;
                actor.data.half=c.read<V>(id,controllerStateSchema,DetCharacterStandingHalf);
                actor.data.offset=c.read<V>(id,controllerStateSchema,DetCharacterStandingOffset);
                actor.data.velocity={};actor.data.ignoredOneWay={};actor.stepRequested=false;actor.stepped=false;
                actor.motionResult=DetCollisionMotionResult2D::Clear;actor.motionOther=0;actor.motionAxis=0;actor.slopeCarry={};
                bool occupied=false,valid=true;
                try {actor.box=bounds(actor);
                    for(const auto& other:trial)if(other.id!=id && !other.data.trigger
                        && masks(actor.data.layer,actor.data.mask,other.data.layer,other.data.mask)
                        && interior(actor.box,actor.data.shape,other.box,other.data.shape)){occupied=true;break;}
                }catch(const std::invalid_argument&){valid=false;}
                if(valid){
                    if(occupied)result=DetRespawnResult2D::Occupied;
                    else try {
                        // Existing trace is actual normal movement only. Never append
                        // old->destination; endpoint membership supplies Enter/Exit.
                        auto candidateTriggers=triggerResult(trial,trace,previous,policy);
                        if(!c.canEmitEvents(candidateTriggers.events.size(),candidateTriggers.events.size()*24))result=DetRespawnResult2D::TriggerCapacity;
                        else {solved=std::move(trial);triggers=std::move(candidateTriggers);result=DetRespawnResult2D::Accepted;}
                    }catch(const std::invalid_argument&){result=DetRespawnResult2D::TriggerCapacity;}
                }
            }
            relocations.push_back({id,requested,result});
        }
    }
    const auto& current=triggers.current;const auto& events=triggers.events;
    // All adapter semantics, shape arithmetic and capacities have passed. The
    // session remains responsible for sticky faults and explicit restore on error.
    for(const auto& b:solved)if(b.data.mode==DetBodyMode2D::Kinematic || b.data.mode==DetBodyMode2D::MovingObstacle){
        const bool relocated=std::any_of(relocations.begin(),relocations.end(),[&](const auto& r){return r.actor==b.id && r.result==DetRespawnResult2D::Accepted;});
        const auto position=math::DetVec3{b.position.x,b.position.y,b.z};
        if(relocated?!c.pose(b.id).setPositionDiscontinuous(position):!c.pose(b.id).setPosition(position))invalid();
        c.write(b.id,policy.bodySchema,DetBodyVelocity,b.data.velocity);
        if(relocated){c.write(b.id,policy.bodySchema,DetBodyHalf,b.data.half);c.write(b.id,policy.bodySchema,DetBodyOffset,b.data.offset);
            c.write(b.id,policy.bodySchema,DetBodyIgnoredOneWay,DetEntityRef{});}
    }
    if(policy.stepUp)for(const auto& b:solved)c.write(b.id,policy.bodySchema,DetBodyStepped,b.stepped);
    if(motionState(policy)){
        // Disabled actors also lose their previous result at this boundary.
        for(auto id:c.entities()){
            const auto found=std::lower_bound(solved.begin(),solved.end(),id,[](const Body& b,auto key){return b.id<key;});
            const Body* b=found!=solved.end() && found->id==id?&*found:nullptr;
            const auto result=b?b->motionResult:DetCollisionMotionResult2D::Clear;
            c.write(id,policy.bodySchema,DetBodyMotionResult,static_cast<std::uint32_t>(result));
            c.write(id,policy.bodySchema,DetBodyMotionOther,DetEntityRef{b?b->motionOther:0});
            c.write(id,policy.bodySchema,DetBodyMotionAxis,b?b->motionAxis:0u);
            c.write(id,policy.bodySchema,DetBodyMotionResultTick,result==DetCollisionMotionResult2D::Clear?std::uint64_t{0}:c.tick()+1);
        }
        for(auto id:cancelledCarry)c.write(id,controllerStateSchema,80,V{});
    }
    if(policy.continuousTerrain && controllerStateSchema)for(const auto& b:solved)if(b.slopeLimited)
        c.write(b.id,controllerStateSchema,80,b.slopeCarry);
    if(respawn)for(const auto& relocation:relocations)
        detail::resolveRespawn(c,relocation.actor,*respawn,relocation.spawn,relocation.result);
    c.writeGlobal(policy.historySchema,5,static_cast<std::uint32_t>(current.size()));
    for(std::uint32_t slot=0;slot<policy.maxTriggerPairs;++slot){const auto p=slot<current.size()?current[slot]:DetCollisionPair2D{};
        c.writeGlobal(policy.historySchema,10+2*slot,DetEntityRef{p.first});c.writeGlobal(policy.historySchema,11+2*slot,DetEntityRef{p.second});}
    for(const auto& event:events)c.emit(policy.eventType,event);
}
}
DetTypedStateSchema detCollisionBodySchema2D(const DetCollision2DConfig& c) {
    if(!validConfig(c))invalid();return bodySchema(c);
}
std::vector<DetCollisionPair2D> detCollisionPairs2D(std::span<const DetCollisionProxy2D> proxies) {
    if(proxies.size()>64)invalid();auto sorted=std::vector(proxies.begin(),proxies.end());
    std::vector<SimEntityId> ids;for(const auto& p:sorted){if(!p.id || !p.box.hasArea() || !p.layer || static_cast<std::uint32_t>(p.shape)>2
        || (p.shape!=DetCollisionShape2D::Aabb && !slope(p.box,p.shape).valid()))invalid();ids.push_back(p.id);}
    std::sort(ids.begin(),ids.end());if(std::adjacent_find(ids.begin(),ids.end())!=ids.end())invalid();
    std::sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.box.min.x<b.box.min.x || (a.box.min.x==b.box.min.x && a.id<b.id);});
    std::vector<DetCollisionPair2D> result;
    for(std::size_t i=0;i<sorted.size();++i)for(std::size_t j=i+1;j<sorted.size();++j){const auto& a=sorted[i];const auto& b=sorted[j];
        if(b.box.min.x>=a.box.max.x)break;
        if(masks(a.layer,a.mask,b.layer,b.mask) && interior(a.box,a.shape,b.box,b.shape))result.push_back({std::min(a.id,b.id),std::max(a.id,b.id)});
    }
    std::sort(result.begin(),result.end());return result;
}
std::vector<std::uint64_t> detCollisionBodyState2D(const DetCollision2DConfig& c,const DetCollisionBody2D& b) {
    if(!validConfig(c))invalid();validateBody(b,c.extended,c.oneWayPlatforms,c.staticSlopes,c.movingSlopes);const auto s=bodySchema(c);auto words=detStateDefaults(s);
    writeDetState(s,words,DetBodyMode,static_cast<std::uint32_t>(b.mode));writeDetState(s,words,DetBodyHalf,b.half);
    writeDetState(s,words,DetBodyOffset,b.offset);writeDetState(s,words,DetBodyVelocity,b.velocity);
    writeDetState(s,words,DetBodyLayer,b.layer);writeDetState(s,words,DetBodyMask,b.mask);writeDetState(s,words,DetBodyTrigger,b.trigger);
    if(c.oneWayPlatforms){writeDetState(s,words,DetBodyOneWay,b.oneWay);writeDetState(s,words,DetBodyIgnoredOneWay,b.ignoredOneWay);}
    if(c.staticSlopes)writeDetState(s,words,DetBodyShape,static_cast<std::uint32_t>(b.shape));
    return words;
}
bool detail::installDetCharacterCollision2D(DeterministicSession& s,DetCollision2DConfig c,std::uint32_t controllerStateSchema,D maxSlopeRisePerRun,
    const DetCharacterRespawn2DConfig* respawn) {
    if(!validConfig(c) || (controllerStateSchema && (controllerStateSchema<2 || controllerStateSchema==UINT32_MAX
        || controllerStateSchema==c.bodySchema || controllerStateSchema==c.historySchema)) || !maxSlopeRisePerRun.isFinite()
        || !(maxSlopeRisePerRun>D{}) || maxSlopeRisePerRun>D::fromInt(16)
        || c.characterRespawn!=bool(respawn) || (respawn && (!detail::validRespawnConfig(*respawn) || controllerStateSchema!=respawn->character.stateSchema)))return false;
    if(!(s.registerTypedSchema(bodySchema(c)) && s.registerTypedSchema(historySchema(c)) && s.registerGlobalState(c.historySchema)))return false;
    if(c.extended && !s.registerValidator(c.systemId,profile(c),
        [c,body=DetStateLayout(bodySchema(c)),history=DetStateLayout(historySchema(c))](const auto& state,std::string& e){
            try {validateExtendedState(state,c,body,history);return true;}catch(const std::exception& error){e=error.what();return false;}}))return false;
    const auto binding=respawn?std::optional{*respawn}:std::nullopt;
    return s.registerSystem(c.systemId,c.priority,[c,controllerStateSchema,maxSlopeRisePerRun,binding](DetTickContext& context){
        tick(context,c,controllerStateSchema,maxSlopeRisePerRun,binding?&*binding:nullptr);return true;});
}
bool installDetCollision2D(DeterministicSession& s,DetCollision2DConfig c) {
    if(c.stepUp || c.characterRespawn)return false;
    return detail::installDetCharacterCollision2D(s,c);
}
std::vector<std::uint8_t> encodeDetTriggerEvent2D(const DetTriggerEvent2D& e) {
    const auto phase=static_cast<std::uint32_t>(e.phase);if(phase<1 || phase>3 || !e.pair.first || e.pair.first>=e.pair.second)invalid();
    std::vector<std::uint8_t> out;auto put=[&](std::uint64_t v,unsigned size){for(unsigned i=0;i<size;++i)out.push_back(static_cast<std::uint8_t>(v>>(i*8)));};
    put(kDetCollision2DProfileVersion,4);put(phase,4);put(e.pair.first,8);put(e.pair.second,8);return out;
}
bool decodeDetTriggerEvent2D(std::span<const std::uint8_t> bytes,DetTriggerEvent2D& out) {
    if(bytes.size()!=24)return false;auto get=[&](unsigned start,unsigned size){std::uint64_t v=0;for(unsigned i=0;i<size;++i)v|=std::uint64_t(bytes[start+i])<<(i*8);return v;};
    const auto phase=get(4,4);DetTriggerEvent2D decoded{static_cast<DetTriggerPhase2D>(phase),{get(8,8),get(16,8)}};
    if(get(0,4)!=kDetCollision2DProfileVersion || phase<1 || phase>3 || !decoded.pair.first || decoded.pair.first>=decoded.pair.second)return false;
    out=decoded;return true;
}

DetCollisionQuery2D::DetCollisionQuery2D(DetTickContext& context,DetCollision2DConfig config) {
    if(!validConfig(config))invalid();
    (void)history(context,config);
    for(const auto& b:readBodies(context,config))
        _proxies.push_back({b.id,b.box,b.data.layer,b.data.mask,b.data.trigger,b.data.shape});
}
DetCollisionQuery2D::DetCollisionQuery2D(const DetSessionCheckpoint& state,DetCollision2DConfig config) {
    if(!validConfig(config) || state.actors.size()>detwire::maxActors)invalid();
    const auto layout=detwire::manifestLayout(state.manifest);
    const auto matchesSchema=[&](const DetTypedStateSchema& expected){
        const auto found=layout.schemas.find(expected.id);
        const auto version=layout.schemaVersions.find(expected.id);
        if(found==layout.schemas.end() || version==layout.schemaVersions.end() || version->second!=expected.version
            || found->second.size()!=expected.fields.size())return false;
        for(std::size_t i=0;i<expected.fields.size();++i){const auto& field=expected.fields[i];const auto& actual=found->second[i];
            if(actual.id!=field.id || actual.type!=detStateType(field.initial) || actual.width!=encodeDetStateValue(field.initial).size())return false;}
        return true;
    };
    if(!matchesSchema(bodySchema(config)) || !matchesSchema(historySchema(config)) || !layout.globals.contains(config.historySchema))invalid();
    const DetStateLayout bodies(bodySchema(config)),hist(historySchema(config));
    const auto found=state.globals.find(config.historySchema);
    if(found==state.globals.end() || !hist.valid(found->second))invalid();
    (void)readHistory([&](std::uint32_t field){return hist.read(found->second,field);},config);
    for(const auto& [id,actor]:state.actors) {
        const auto block=actor.blocks.find(config.bodySchema);
        if(!id || block==actor.blocks.end() || !bodies.valid(block->second))invalid();
        const auto read=[&]<class T>(std::uint32_t field){return std::get<T>(bodies.read(block->second,field));};
        DetCollisionBody2D data{static_cast<DetBodyMode2D>(read.operator()<std::uint32_t>(DetBodyMode)),
            read.operator()<V>(DetBodyHalf),read.operator()<V>(DetBodyOffset),read.operator()<V>(DetBodyVelocity),
            read.operator()<std::uint32_t>(DetBodyLayer),read.operator()<std::uint32_t>(DetBodyMask),
            read.operator()<bool>(DetBodyTrigger)};
        DetSimTransformComponent restored;
        if(!restored.restore(actor.pose))invalid();
        if(config.oneWayPlatforms){data.oneWay=read.operator()<bool>(DetBodyOneWay);data.ignoredOneWay=read.operator()<DetEntityRef>(DetBodyIgnoredOneWay);}
        if(config.staticSlopes)data.shape=static_cast<DetCollisionShape2D>(read.operator()<std::uint32_t>(DetBodyShape));
        if(config.stepUp && (read.operator()<bool>(DetBodyStepRequested) || read.operator()<bool>(DetBodyStepped)))invalid();
        validateBody(data,config.extended,config.oneWayPlatforms,config.staticSlopes,config.movingSlopes);
        if(motionState(config))validateMotionOutput(state,id,data,bodies,config.bodySchema,[&](std::uint32_t field){return bodies.read(block->second,field);},config.staticSlopes,stopping(config),config.movingSlopes);
        if(data.mode==DetBodyMode2D::Disabled)continue;
        const V position{D::fromBits(actor.pose.position[0]),D::fromBits(actor.pose.position[1])};
        const Body b{id,data,position,D::fromBits(actor.pose.position[2]),{}};
        _proxies.push_back({id,bounds(b),data.layer,data.mask,data.trigger,data.shape});
        if(_proxies.size()>config.maxBodies)invalid();
    }
}
namespace {
void validateFilter(const DetCollisionQueryFilter2D& filter) {
    if(!filter.layer)throw std::invalid_argument("deterministic query 2D: zero query layer");
}
bool matches(const DetCollisionProxy2D& proxy,const DetCollisionQueryFilter2D& filter) {
    return proxy.id!=filter.ignore && (filter.includeTriggers || !proxy.trigger)
        && masks(proxy.layer,proxy.mask,filter.layer,filter.mask);
}
void sortHits(std::vector<DetCollisionQueryHit2D>& hits) {
    std::sort(hits.begin(),hits.end(),[](const auto& a,const auto& b){
        return a.hit.fraction<b.hit.fraction || (a.hit.fraction==b.hit.fraction
            && (a.hit.axis<b.hit.axis || (a.hit.axis==b.hit.axis && a.entity<b.entity)));
    });
}
}
std::vector<SimEntityId> DetCollisionQuery2D::overlap(const math::DetAabb2& box,DetCollisionQueryFilter2D filter,bool interior) const {
    validateFilter(filter);
    if(!box.valid())throw std::invalid_argument("deterministic query 2D: invalid overlap bounds");
    std::vector<SimEntityId> ids;
    for(const auto& p:_proxies)if(matches(p,filter)){
        const bool overlap=p.shape==DetCollisionShape2D::Aabb?(interior?p.box.interiorOverlap(box):p.box.overlap(box))
            :(interior?slope(p.box,p.shape).interiorOverlapAabb(box):slope(p.box,p.shape).overlapAabb(box));
        if(overlap)ids.push_back(p.id);
    }
    return ids;
}
std::vector<DetCollisionQueryHit2D> DetCollisionQuery2D::raycast(V origin,V displacement,DetCollisionQueryFilter2D filter) const {
    validateFilter(filter);
    if(!origin.isFinite() || !displacement.isFinite())throw std::invalid_argument("deterministic query 2D: invalid ray segment");
    std::vector<DetCollisionQueryHit2D> hits;
    for(const auto& p:_proxies)if(matches(p,filter)){
        if(p.shape==DetCollisionShape2D::Aabb){if(const auto hit=math::detRaycast2D(p.box,origin,displacement))hits.push_back({p.id,*hit});}
        else if(const auto hit=math::detRaycastSlope2D(slope(p.box,p.shape),origin,displacement))hits.push_back({p.id,{hit->fraction,hit->normal,hit->axis,hit->initialOverlap}});
    }
    sortHits(hits);return hits;
}
std::vector<DetCollisionQueryHit2D> DetCollisionQuery2D::sweep(const math::DetAabb2& box,V displacement,DetCollisionQueryFilter2D filter) const {
    validateFilter(filter);
    if(!box.hasArea() || !displacement.isFinite())throw std::invalid_argument("deterministic query 2D: invalid sweep shape/displacement");
    std::vector<DetCollisionQueryHit2D> hits;
    for(const auto& p:_proxies)if(matches(p,filter)){
        if(p.shape==DetCollisionShape2D::Aabb){if(const auto hit=math::detSweepAabb2D(box,displacement,p.box))hits.push_back({p.id,*hit});}
        else if(const auto hit=math::detSweepAabbSlope2D(box,displacement,slope(p.box,p.shape)))hits.push_back({p.id,{hit->fraction,hit->normal,hit->axis,hit->initialOverlap}});
    }
    sortHits(hits);return hits;
}
} // namespace ayt::entity
