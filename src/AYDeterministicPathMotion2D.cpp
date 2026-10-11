#include <AYEntity/DeterministicPathMotion2D.h>
#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>

namespace ayt::entity {
namespace {
using D=math::DetFloat32;using V=math::DetVec2;
[[noreturn]] void invalid(){throw std::invalid_argument("deterministic path 2D: invalid policy, binding, phase or unrepresentable target");}
bool equal(V a,V b){return a.x.bits()==b.x.bits() && a.y.bits()==b.y.bits();}
bool zero(V v){return equal(v,V{});}
bool validConfig(const DetPathMotion2DConfig& c){
    if(!c.collision.extended || c.waypoints.size()<2 || c.waypoints.size()>24
        || static_cast<std::uint32_t>(c.playback)>2 || !c.tickPeriod.isFinite() || !(c.tickPeriod>D{})
        || !c.preSystemId || !c.postSystemId || !c.validatorId || !c.logicProfileId
        || !(c.preSystemId<c.collision.systemId && c.collision.systemId<c.postSystemId)
        || c.validatorId==c.collision.systemId)return false;
    std::set<std::uint32_t> ids;
    for(auto id:{c.stateSchema,c.policySchema,c.collision.bodySchema,c.collision.historySchema})
        if(id<2 || id==UINT32_MAX || !ids.insert(id).second)return false;
    if(!zero(c.waypoints.front().offset))return false;
    for(const auto& p:c.waypoints)if(!p.offset.isFinite() || !p.travelTicks || p.travelTicks>65535 || p.dwellTicks>65535)return false;
    return true;
}
DetTypedStateSchema stateSchema(const DetPathMotion2DConfig& c){return {c.stateSchema,1,{
    {DetPathBound,false},{DetPathEnabled,false},{DetPathPaused,false},{DetPathFinished,false},
    {DetPathOrigin,V{}},{DetPathWaypoint,std::uint32_t{0}},{DetPathDirection,std::int32_t{1}},
    {DetPathElapsedTicks,std::uint32_t{0}},{DetPathDwellRemaining,std::uint32_t{0}},
    {DetPathLastVelocity,V{}},{DetPathBlocked,false},{DetPathRestartRequested,false},
    {DetPathExpectedPosition,V{}},{DetPathAttempted,false}}};}
DetTypedStateSchema policySchema(const DetPathMotion2DConfig& c){
    DetTypedStateSchema s{c.policySchema,1,{{1,std::uint32_t{1}},{2,c.collision.bodySchema},{3,c.collision.historySchema},
        {4,c.collision.systemId},{5,c.preSystemId},{6,c.postSystemId},{7,c.validatorId},{8,c.logicProfileId},
        {9,c.tickPeriod},{10,static_cast<std::uint32_t>(c.playback)},{11,static_cast<std::uint32_t>(c.waypoints.size())}}};
    for(std::size_t i=0;i<c.waypoints.size();++i){auto id=static_cast<std::uint32_t>(20+3*i);const auto& p=c.waypoints[i];
        s.fields.push_back({id,p.offset});s.fields.push_back({id+1,p.travelTicks});s.fields.push_back({id+2,p.dwellTicks});}
    return s;
}
std::uint32_t nextPoint(const DetPathMotion2DConfig& c,const DetPathMotionState2D& s){
    if(s.direction<0)return s.waypoint-1;
    if(s.waypoint+1<c.waypoints.size())return s.waypoint+1;
    return 0;
}
std::uint32_t duration(const DetPathMotion2DConfig& c,const DetPathMotionState2D& s){
    return c.waypoints[s.direction<0?s.waypoint-1:s.waypoint].travelTicks;
}
void validate(const DetPathMotion2DConfig& c,const DetPathMotionState2D& s){
    if(!s.origin.isFinite() || !s.lastVelocity.isFinite() || s.waypoint>=c.waypoints.size()
        || (s.direction!=1 && s.direction!=-1))invalid();
    if(!s.bound){if(s.enabled || s.paused || s.finished || s.blocked || !zero(s.origin) || !zero(s.lastVelocity)
        || s.waypoint || s.elapsedTicks || s.dwellRemaining || s.direction!=1)invalid();return;}
    for(const auto& p:c.waypoints)if(!(s.origin+p.offset).isFinite())invalid();
    if(s.dwellRemaining>c.waypoints[s.waypoint].dwellTicks
        || (s.dwellRemaining && s.elapsedTicks)
        || (c.playback!=DetPathPlayback2D::PingPong && s.direction!=1)
        || (s.direction<0 && !s.waypoint)
        || (c.playback==DetPathPlayback2D::PingPong && s.waypoint+1==c.waypoints.size() && s.direction!= -1))invalid();
    if(s.finished){if(c.playback!=DetPathPlayback2D::Once || s.waypoint+1!=c.waypoints.size() || s.elapsedTicks || s.dwellRemaining)invalid();}
    else {if(c.playback==DetPathPlayback2D::Once && s.waypoint+1==c.waypoints.size())invalid();
        if(s.elapsedTicks>=duration(c,s))invalid();}
    if((!s.enabled || s.paused || s.finished || s.dwellRemaining) && !zero(s.lastVelocity))invalid();
    if(s.blocked && (!s.enabled || s.paused || s.finished || s.dwellRemaining))invalid();
}
DetPathMotionState2D read(DetTickContext& ctx,SimEntityId id,const DetPathMotion2DConfig& c){
    const auto sc=c.stateSchema;DetPathMotionState2D s;
    s.bound=ctx.read<bool>(id,sc,DetPathBound);s.enabled=ctx.read<bool>(id,sc,DetPathEnabled);
    s.paused=ctx.read<bool>(id,sc,DetPathPaused);s.finished=ctx.read<bool>(id,sc,DetPathFinished);
    s.blocked=ctx.read<bool>(id,sc,DetPathBlocked);s.origin=ctx.read<V>(id,sc,DetPathOrigin);
    s.lastVelocity=ctx.read<V>(id,sc,DetPathLastVelocity);s.waypoint=ctx.read<std::uint32_t>(id,sc,DetPathWaypoint);
    s.direction=ctx.read<std::int32_t>(id,sc,DetPathDirection);s.elapsedTicks=ctx.read<std::uint32_t>(id,sc,DetPathElapsedTicks);
    s.dwellRemaining=ctx.read<std::uint32_t>(id,sc,DetPathDwellRemaining);return s;
}
void write(DetTickContext& ctx,SimEntityId id,const DetPathMotion2DConfig& c,const DetPathMotionState2D& s){
    const auto sc=c.stateSchema;
    ctx.write(id,sc,DetPathBound,s.bound);ctx.write(id,sc,DetPathEnabled,s.enabled);ctx.write(id,sc,DetPathPaused,s.paused);
    ctx.write(id,sc,DetPathFinished,s.finished);ctx.write(id,sc,DetPathOrigin,s.origin);ctx.write(id,sc,DetPathWaypoint,s.waypoint);
    ctx.write(id,sc,DetPathDirection,s.direction);ctx.write(id,sc,DetPathElapsedTicks,s.elapsedTicks);
    ctx.write(id,sc,DetPathDwellRemaining,s.dwellRemaining);ctx.write(id,sc,DetPathLastVelocity,s.lastVelocity);ctx.write(id,sc,DetPathBlocked,s.blocked);
}
V position(DetTickContext& ctx,SimEntityId id){const auto p=ctx.pose(id).position;return {p.x,p.y};}
void policyMatches(DetTickContext& ctx,const DetPathMotion2DConfig& c){
    for(const auto& field:policySchema(c).fields)std::visit([&]<class T>(const T& expected){
        if(encodeDetStateValue(ctx.readGlobal<T>(c.policySchema,field.id))!=encodeDetStateValue(expected))invalid();},field.initial);
}
void pre(DetTickContext& ctx,const DetPathMotion2DConfig& c){
    policyMatches(ctx,c);if(ctx.dt().bits()!=c.tickPeriod.bits())invalid();
    for(auto id:ctx.entities()){
        auto s=read(ctx,id,c);if(!s.bound)continue;
        if(ctx.read<std::uint32_t>(id,c.collision.bodySchema,DetBodyMode)!=static_cast<std::uint32_t>(DetBodyMode2D::MovingObstacle)
            || ctx.read<bool>(id,c.collision.bodySchema,DetBodyTrigger))invalid();
        s.blocked=false;s.lastVelocity={};V velocity{};
        if(ctx.read<bool>(id,c.stateSchema,DetPathRestartRequested)){
            if(!equal(position(ctx,id),s.origin))invalid();
            s.waypoint=0;s.direction=1;s.elapsedTicks=0;s.dwellRemaining=c.waypoints.front().dwellTicks;s.finished=false;
        }
        bool attempted=false;V expected{};
        if(s.enabled && !s.paused && !s.finished){
            if(s.dwellRemaining)--s.dwellRemaining;
            else {const auto& a=c.waypoints[s.waypoint];const auto& b=c.waypoints[nextPoint(c,s)];
                const auto fraction=D::fromUInt(s.elapsedTicks+1)/D::fromUInt(duration(c,s));
                const auto delta=b.offset-a.offset;
                // Force the endpoint to the authored point (rather than a rounded lerp).
                expected=s.elapsedTicks+1==duration(c,s)?s.origin+b.offset:s.origin+a.offset+delta*fraction;
                const auto current=position(ctx,id);velocity=(expected-current)/c.tickPeriod;
                if(!delta.isFinite() || !expected.isFinite() || !velocity.isFinite()
                    || !equal(current+velocity*c.tickPeriod,expected))invalid();
                attempted=true;
            }
        }
        write(ctx,id,c,s);ctx.write(id,c.collision.bodySchema,DetBodyVelocity,velocity);
        ctx.write(id,c.stateSchema,DetPathRestartRequested,false);ctx.write(id,c.stateSchema,DetPathExpectedPosition,expected);
        ctx.write(id,c.stateSchema,DetPathAttempted,attempted);
    }
}
void post(DetTickContext& ctx,const DetPathMotion2DConfig& c){
    for(auto id:ctx.entities()){
        auto s=read(ctx,id,c);if(!s.bound)continue;
        s.lastVelocity=ctx.read<V>(id,c.collision.bodySchema,DetBodyVelocity);
        if(ctx.read<bool>(id,c.stateSchema,DetPathAttempted)){
            if(equal(position(ctx,id),ctx.read<V>(id,c.stateSchema,DetPathExpectedPosition))){
                if(++s.elapsedTicks==duration(c,s)){
                    s.waypoint=nextPoint(c,s);s.elapsedTicks=0;s.dwellRemaining=c.waypoints[s.waypoint].dwellTicks;
                    if(c.playback==DetPathPlayback2D::Once && s.waypoint+1==c.waypoints.size()){s.finished=true;s.dwellRemaining=0;}
                    if(c.playback==DetPathPlayback2D::PingPong){if(s.waypoint+1==c.waypoints.size())s.direction=-1;else if(!s.waypoint)s.direction=1;}
                }
            }else s.blocked=true;
        }
        // lastVelocity describes this committed tick, including an arrival tick.
        write(ctx,id,c,s);ctx.write(id,c.stateSchema,DetPathExpectedPosition,V{});ctx.write(id,c.stateSchema,DetPathAttempted,false);
    }
}
void validateCheckpoint(const DetSessionCheckpoint& cp,const DetPathMotion2DConfig& c,const DetStateLayout& states,
    const DetStateLayout& policies,const DetStateLayout& bodies){
    if(cp.globals.at(c.policySchema)!=policies.defaults())invalid();
    for(const auto& [id,a]:cp.actors){
        if(a.blocks.at(c.policySchema)!=policies.defaults())invalid();const auto& block=a.blocks.at(c.stateSchema);
        auto get=[&]<class T>(std::uint32_t field){return std::get<T>(states.read(block,field));};
        DetPathMotionState2D s;
        s.bound=get.operator()<bool>(DetPathBound);s.enabled=get.operator()<bool>(DetPathEnabled);s.paused=get.operator()<bool>(DetPathPaused);
        s.finished=get.operator()<bool>(DetPathFinished);s.blocked=get.operator()<bool>(DetPathBlocked);s.origin=get.operator()<V>(DetPathOrigin);
        s.lastVelocity=get.operator()<V>(DetPathLastVelocity);s.waypoint=get.operator()<std::uint32_t>(DetPathWaypoint);
        s.direction=get.operator()<std::int32_t>(DetPathDirection);s.elapsedTicks=get.operator()<std::uint32_t>(DetPathElapsedTicks);
        s.dwellRemaining=get.operator()<std::uint32_t>(DetPathDwellRemaining);
        // Arrival results keep this tick's velocity even after entering dwell/finish.
        auto phase=s;phase.lastVelocity={};validate(c,phase);
        if(!s.lastVelocity.isFinite() || (!s.bound && !zero(s.lastVelocity))
            || ((!s.enabled || s.paused) && !zero(s.lastVelocity))
            || get.operator()<bool>(DetPathRestartRequested) || get.operator()<bool>(DetPathAttempted)
            || !zero(get.operator()<V>(DetPathExpectedPosition)))invalid();
        if(s.bound){const auto& body=a.blocks.at(c.collision.bodySchema);
            if(std::get<std::uint32_t>(bodies.read(body,DetBodyMode))!=static_cast<std::uint32_t>(DetBodyMode2D::MovingObstacle)
                || std::get<bool>(bodies.read(body,DetBodyTrigger)) || !equal(std::get<V>(bodies.read(body,DetBodyVelocity)),s.lastVelocity))invalid();
            if(s.finished || s.dwellRemaining){const auto point=s.origin+c.waypoints[s.waypoint].offset;
                if(a.pose.position[0]!=point.x.bits() || a.pose.position[1]!=point.y.bits())invalid();}
            if(cp.nextTick==0 && (s.waypoint || s.elapsedTicks || s.direction!=1 || s.finished || s.blocked
                || s.dwellRemaining!=c.waypoints.front().dwellTicks || !zero(s.lastVelocity)
                || a.pose.position[0]!=s.origin.x.bits() || a.pose.position[1]!=s.origin.y.bits()))invalid();
        }
        (void)id;
    }
}
} // namespace
bool installDetPathMotion2D(DeterministicSession& session,DetPathMotion2DConfig c){
    if(!validConfig(c))return false;
    // The collision schema is shared; installation validates its canonical layout.
    DetTypedStateSchema body;
    try{body=detCollisionBodySchema2D(c.collision);}catch(const std::invalid_argument&){return false;}
    const auto state=stateSchema(c),policy=policySchema(c);const DetStateLayout states(state),policies(policy),bodies(body);
    if(!(session.registerTypedSchema(state) && session.registerTypedSchema(policy) && session.registerGlobalState(c.policySchema)
        && session.registerLogicProfile(c.logicProfileId,1,0x44504d3244503031ull)
        && session.registerValidator(c.validatorId,1,[c,states,policies,bodies](const DetSessionCheckpoint& cp,std::string& error){
            try{validateCheckpoint(cp,c,states,policies,bodies);return true;}catch(...){error="deterministic path 2D: invalid registered boundary";return false;}})
        && session.registerSystem(c.preSystemId,c.collision.priority,[c](DetTickContext& ctx){pre(ctx,c);return true;})
        && session.registerSystem(c.postSystemId,c.collision.priority,[c](DetTickContext& ctx){post(ctx,c);return true;})))return false;
    return true;
}
std::vector<std::uint64_t> detPathMotionState2D(const DetPathMotion2DConfig& c,const DetPathMotionState2D& authored){
    if(!validConfig(c))invalid();auto s=authored;
    if(s.finished || s.blocked || s.waypoint || s.elapsedTicks || s.dwellRemaining || s.direction!=1 || !zero(s.lastVelocity))invalid();
    if(!s.bound){s.enabled=false;if(s.paused || !zero(s.origin))invalid();}
    else s.dwellRemaining=c.waypoints.front().dwellTicks;
    validate(c,s);const auto schema=stateSchema(c);auto words=detStateDefaults(schema);
    writeDetState(schema,words,DetPathBound,s.bound);writeDetState(schema,words,DetPathEnabled,s.enabled);writeDetState(schema,words,DetPathPaused,s.paused);
    writeDetState(schema,words,DetPathOrigin,s.origin);writeDetState(schema,words,DetPathDwellRemaining,s.dwellRemaining);return words;
}
DetPathMotionState2D detPathMotionState2D(DetTickContext& ctx,SimEntityId actor,const DetPathMotion2DConfig& c){
    if(!validConfig(c))invalid();return read(ctx,actor,c);
}
void detPathMotionInput2D(DetTickContext& ctx,SimEntityId actor,const DetPathMotion2DConfig& c,DetPathMotionInput2D input){
    if(!validConfig(c))invalid();const auto s=read(ctx,actor,c);
    if(!s.bound || (input.restart && !equal(position(ctx,actor),s.origin)))invalid();
    ctx.write(actor,c.stateSchema,DetPathEnabled,input.enabled);ctx.write(actor,c.stateSchema,DetPathPaused,input.paused);
    ctx.write(actor,c.stateSchema,DetPathRestartRequested,input.restart);
}
} // namespace ayt::entity
