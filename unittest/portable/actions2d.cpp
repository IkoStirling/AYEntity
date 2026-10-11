// design reference: AYEntity/design.md Stage33 registered direct actions.
// Hand-authored action/result and dyadic path cases. Complete CP comparisons are
// actual order/restore equivalence, not independently generated all-byte goldens.
#include <AYEntity/DeterministicActions2D.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace ayt::entity;
namespace {
using D=ayt::math::DetFloat32;using V=ayt::math::DetVec2;
using Actors=std::vector<std::pair<SimEntityId,DetActorState>>;
unsigned cases=0,fullComparisons=0,negativeRestores=0;
void require(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
D q(int n,int denominator=4){return D::fromInt(n)/D::fromInt(denominator);}
bool equal(V a,V b){return a.x.bits()==b.x.bits() && a.y.bits()==b.y.bits();}
DetActions2DConfig config(){DetActions2DConfig c;c.collision.extended=true;c.collision.maxBodies=8;c.collision.maxTriggerPairs=8;
    DetPathMotion2DConfig path;path.collision=c.collision;path.tickPeriod=q(1);
    path.waypoints={{{},4,0},{{D::fromInt(1),D{}},4,0}};
    c.targets={{20,1,true,{}},{30,1,true,{}},{40,1,true,path},{50,1,true,{}},{60,1,true,{}}};return c;}
DetActorState actor(const DetActions2DConfig& c,DetBodyMode2D mode,V at,V half,std::uint32_t layer,std::uint32_t mask,bool trigger=false){
    DetActorState a;DetSimTransformComponent pose;require(pose.setPosition({at.x,at.y,D{}}),"fixture pose");a.pose=pose.snapshot();
    a.blocks[c.collision.bodySchema]=detCollisionBodyState2D(c.collision,{mode,half,{},V{},layer,mask,trigger});return a;}
Actors scene(const DetActions2DConfig& c){
    Actors a{{10,actor(c,DetBodyMode2D::Kinematic,V{},{q(1),q(1)},1,6)},
        {20,actor(c,DetBodyMode2D::Static,V{},{q(2),q(2)},2,0)},
        {30,actor(c,DetBodyMode2D::Static,V{},{q(2),q(2)},4,0,true)},
        {40,actor(c,DetBodyMode2D::MovingObstacle,{D::fromInt(4),D{}},{q(1),q(1)},2,1)},
        {50,actor(c,DetBodyMode2D::Kinematic,{D::fromInt(8),D{}},{q(1),q(1)},2,1)}};
    const auto& path=*c.targets[2].path;DetPathMotionState2D bound;bound.bound=true;bound.enabled=false;bound.origin={D::fromInt(4),D{}};
    a[3].second.blocks[path.stateSchema]=detPathMotionState2D(path,bound);return a;}
DetTickCommand action(std::uint32_t sequence,SimEntityId target,DetActionKind2D kind,DetActionCode2D expected){
    require(target<=255,"fixture byte target");return {1,sequence,1,{static_cast<std::uint8_t>(target),
        static_cast<std::uint8_t>(kind),static_cast<std::uint8_t>(expected)}};}
DetTickCommand move(std::uint32_t sequence,int speed){return {1,sequence,2,{static_cast<std::uint8_t>(speed)}};}
DetTickInput frame(std::uint64_t tick,std::initializer_list<DetTickCommand> commands){return {tick,1,std::vector<DetTickCommand>(commands)};}
bool configure(DeterministicSession& s,const DetActions2DConfig& c,Actors actors,bool reverse=false){
    const auto input=[c](DetTickContext& ctx){for(const auto& command:ctx.input().commands){
        if(command.source!=1)return false;
        if(command.type==1 && command.payload.size()==3){const DetActionRequest2D request{command.payload[0],static_cast<DetActionKind2D>(command.payload[1])};
            const auto before=detActionCount2D(ctx,c);const auto result=executeDetAction2D(ctx,c,request);
            require(result.code==static_cast<DetActionCode2D>(command.payload[2]),"hand action result target="+std::to_string(request.target)
                +" kind="+std::to_string(command.payload[1])+" expected="+std::to_string(command.payload[2])+" actual="+std::to_string(static_cast<unsigned>(result.code)));
            const auto last=detActionResult2D(ctx,c);require(result.sequence==before+1 && result.tick==ctx.tick()
                && last.sequence==result.sequence && last.tick==result.tick && last.code==result.code
                && last.request.target==request.target && last.request.kind==request.kind,"synchronous last result and monotonic attempt sequence");
        }else if(command.type==2 && command.payload.size()==1){const int speed=command.payload[0]<128?command.payload[0]:int(command.payload[0])-256;
            ctx.write(10,c.collision.bodySchema,DetBodyVelocity,V{D::fromInt(speed),D{}});
        }else return false;}return true;};
    if(!installDetActions2D(s,c) || (!reverse && !s.registerSystem(10,c.collision.priority,input)) || !installDetCollision2D(s,c.collision))return false;
    std::vector<std::uint32_t> installed;
    for(const auto& target:c.targets)if(target.path && std::find(installed.begin(),installed.end(),target.path->stateSchema)==installed.end()){
        if(!installDetPathMotion2D(s,*target.path))return false;installed.push_back(target.path->stateSchema);}
    if(reverse && !s.registerSystem(10,c.collision.priority,input))return false;
    if(reverse)std::reverse(actors.begin(),actors.end());for(auto& [id,a]:actors)if(!s.addEntity(id,std::move(a)))return false;return s.seal();}
DetSessionCheckpoint checkpoint(DeterministicSession& s){const auto cp=s.checkpoint();require(bool(cp),s.error());return *cp;}
std::vector<std::uint8_t> bytes(DeterministicSession& s){return encodeDetCheckpoint(checkpoint(s));}
template<class T>T body(const DetSessionCheckpoint& cp,const DetActions2DConfig& c,SimEntityId id,std::uint32_t field){
    return readDetState<T>(detCollisionBodySchema2D(c.collision),cp.actors.at(id).blocks.at(c.collision.bodySchema),field);}
template<class T>T global(const DetSessionCheckpoint& cp,const DetActions2DConfig& c,std::uint32_t field){
    return readDetState<T>(detActionsStateSchema2D(c),cp.globals.at(c.stateSchema),field);}
V position(const DetSessionCheckpoint& cp,SimEntityId id){const auto& p=cp.actors.at(id).pose.position;return {D::fromBits(p[0]),D::fromBits(p[1])};}
DetTypedStateSchema pathSchema(const DetPathMotion2DConfig& p){return {p.stateSchema,1,{
    {10,false},{20,false},{30,false},{40,false},{50,V{}},{60,std::uint32_t{}},{70,std::int32_t{1}},
    {80,std::uint32_t{}},{90,std::uint32_t{}},{100,V{}},{110,false},{120,false},{130,V{}},{140,false}}};}
template<class T>T pathState(const DetSessionCheckpoint& cp,const DetPathMotion2DConfig& p,std::uint32_t field){return readDetState<T>(pathSchema(p),cp.actors.at(40).blocks.at(p.stateSchema),field);}
void advance(DeterministicSession& s,DetTickInput input){require(s.advance(std::move(input)),s.error());}
void rejectRestore(DeterministicSession& s,const DetSessionCheckpoint& bad,const std::string& why){const auto before=bytes(s);
    require(!s.restore(bad) && !s.faulted() && bytes(s)==before,why+" atomic rejection");++negativeRestores;}
void collisionChecks(){const auto c=config();DeterministicSession a({1,1,1,4,33}),b({1,1,1,4,33});
    require(configure(a,c,scene(c)) && configure(b,c,scene(c),true),"action scene seal");
    const auto initial=checkpoint(a);require(global<std::uint64_t>(initial,c,DetActionsTotalCount)==0,"initial attempt state canonical zero");
    const std::array<DetTickInput,4> inputs{
        frame(0,{action(0,20,DetActionKind2D::CollisionEnable,DetActionCode2D::Occupied),
            action(1,99,DetActionKind2D::CollisionEnable,DetActionCode2D::UnknownTarget),
            action(2,60,DetActionKind2D::CollisionEnable,DetActionCode2D::MissingActor),
            action(3,50,DetActionKind2D::CollisionDisable,DetActionCode2D::Unsupported),
            action(4,30,DetActionKind2D::CollisionEnable,DetActionCode2D::Applied),
            action(5,30,DetActionKind2D::CollisionEnable,DetActionCode2D::Unchanged)}),
        frame(1,{action(0,30,DetActionKind2D::CollisionDisable,DetActionCode2D::Applied),move(1,4)}),
        frame(2,{move(0,0),action(1,20,DetActionKind2D::CollisionEnable,DetActionCode2D::Applied),
            action(2,30,DetActionKind2D::CollisionEnable,DetActionCode2D::Applied)}),
        frame(3,{action(0,20,DetActionKind2D::CollisionEnable,DetActionCode2D::Unchanged),
            action(1,20,DetActionKind2D::CollisionDisable,DetActionCode2D::Applied),
            action(2,20,DetActionKind2D::CollisionDisable,DetActionCode2D::Unchanged)})};
    std::vector<DetSessionCheckpoint> rows{initial};
    for(unsigned t=0;t<inputs.size();++t){advance(a,inputs[t]);advance(b,inputs[t]);require(bytes(a)==bytes(b),"actions reverse registration all CP bytes");++fullComparisons;
        const auto cp=checkpoint(a);rows.push_back(cp);
        require(equal(position(cp,20),V{}) && body<std::uint32_t>(cp,c,20,DetBodyMode)==static_cast<std::uint32_t>(DetBodyMode2D::Static),"collision switch writes neither pose nor mode");
        require(body<std::uint32_t>(cp,c,20,DetBodyMask)==(t==2?1u:0u),"authorized gate mask only");
        if(t<2){require(cp.pendingEvents.size()==1,"actual enabled/disabled trigger transition");DetTriggerEvent2D event;
            require(decodeDetTriggerEvent2D(cp.pendingEvents[0].payload,event) && event.pair==DetCollisionPair2D{10,30}
                && event.phase==(t==0?DetTriggerPhase2D::Enter:DetTriggerPhase2D::Exit),"actual trigger Enter/Exit, no enable clearance refusal");}
        else require(cp.pendingEvents.empty(),"separated trigger has no fabricated event");}
    require(global<std::uint64_t>(rows.back(),c,DetActionsTotalCount)==12
        && global<std::uint64_t>(rows.back(),c,DetActionsLastTick)==4,"hand counted attempts and last zero-based tick stamp");
    require(a.restore(rows[1]),"action mid-result restore");for(unsigned t=1;t<inputs.size();++t){advance(a,inputs[t]);
        require(bytes(a)==encodeDetCheckpoint(rows[t+1]),"action restore continuation all bytes");++fullComparisons;}
    ++cases;std::cout<<"PASS direct gate/trigger actions, occupied/unknown/missing/unsupported, real events, order and restore\n";
}
void budgetChecks(){auto c=config();c.maxActionsPerTick=2;DeterministicSession s({1,1,1,4,33});require(configure(s,c,scene(c)),s.error());
    advance(s,frame(0,{action(0,30,DetActionKind2D::CollisionEnable,DetActionCode2D::Applied),
        action(1,30,DetActionKind2D::CollisionDisable,DetActionCode2D::Applied),
        action(2,99,DetActionKind2D::CollisionEnable,DetActionCode2D::BudgetExceeded)}));
    const auto cp=checkpoint(s);require(global<std::uint64_t>(cp,c,DetActionsTotalCount)==3 && global<std::uint32_t>(cp,c,DetActionsTickCount)==2
        && body<std::uint32_t>(cp,c,30,DetBodyMask)==0 && !s.faulted(),"budget denial records once without target mutation or fault");
    const auto next=frame(1,{action(0,30,DetActionKind2D::CollisionEnable,DetActionCode2D::Applied)});advance(s,next);const auto after=bytes(s);
    require(global<std::uint64_t>(checkpoint(s),c,DetActionsTotalCount)==4 && global<std::uint32_t>(checkpoint(s),c,DetActionsTickCount)==1,"fresh tick resets capped opportunity count");
    require(s.restore(cp),"budget result restores");advance(s,next);require(bytes(s)==after,"budget restore continuation all bytes");++fullComparisons;++cases;
    std::cout<<"PASS bounded per-tick budget and monotonic denial sequence\n";
}
void pathChecks(){const auto c=config();const auto& p=*c.targets[2].path;DeterministicSession s({1,1,1,4,33});require(configure(s,c,scene(c)),s.error());
    advance(s,frame(0,{action(0,40,DetActionKind2D::PathPause,DetActionCode2D::Applied),
        action(1,40,DetActionKind2D::PathDisable,DetActionCode2D::Unchanged),action(2,40,DetActionKind2D::PathResume,DetActionCode2D::Applied),
        action(3,40,DetActionKind2D::PathEnable,DetActionCode2D::Applied)}));
    require(equal(position(checkpoint(s),40),V{q(17),D{}}),"enabled actual path advances one quarter");
    advance(s,frame(1,{action(0,40,DetActionKind2D::PathPause,DetActionCode2D::Applied)}));
    require(pathState<bool>(checkpoint(s),p,DetPathEnabled) && pathState<bool>(checkpoint(s),p,DetPathPaused),"pause preserves enabled");
    advance(s,frame(2,{action(0,40,DetActionKind2D::PathDisable,DetActionCode2D::Applied)}));
    require(!pathState<bool>(checkpoint(s),p,DetPathEnabled) && pathState<bool>(checkpoint(s),p,DetPathPaused),"disable preserves paused");
    advance(s,frame(3,{action(0,40,DetActionKind2D::PathResume,DetActionCode2D::Applied)}));
    require(!pathState<bool>(checkpoint(s),p,DetPathEnabled) && !pathState<bool>(checkpoint(s),p,DetPathPaused),"resume preserves disabled");
    advance(s,frame(4,{action(0,40,DetActionKind2D::PathRestart,DetActionCode2D::RestartAwayFromOrigin)}));
    require(equal(position(checkpoint(s),40),V{q(17),D{}}) && pathState<std::uint32_t>(checkpoint(s),p,DetPathElapsedTicks)==1,"denied restart preserves pose and phase");
    advance(s,frame(5,{action(0,40,DetActionKind2D::PathEnable,DetActionCode2D::Applied)}));
    advance(s,frame(6,{}));advance(s,frame(7,{}));
    require(equal(position(checkpoint(s),40),V{D::fromInt(5),D{}}),"real normal travel reaches turn");
    advance(s,frame(8,{action(0,40,DetActionKind2D::PathPause,DetActionCode2D::Applied)}));
    advance(s,frame(9,{action(0,40,DetActionKind2D::PathResume,DetActionCode2D::Applied)}));
    advance(s,frame(10,{}));advance(s,frame(11,{}));advance(s,frame(12,{}));
    require(equal(position(checkpoint(s),40),V{D::fromInt(4),D{}}),"restart opportunity reached by real return leg");
    advance(s,frame(13,{action(0,40,DetActionKind2D::PathRestart,DetActionCode2D::Applied),
        action(1,40,DetActionKind2D::PathPause,DetActionCode2D::Applied),action(2,40,DetActionKind2D::PathDisable,DetActionCode2D::Applied)}));
    const auto cp=checkpoint(s);require(equal(position(cp,40),V{D::fromInt(4),D{}}) && !pathState<bool>(cp,p,DetPathEnabled)
        && pathState<bool>(cp,p,DetPathPaused) && pathState<std::uint32_t>(cp,p,DetPathElapsedTicks)==0
        && pathState<std::uint32_t>(cp,p,DetPathWaypoint)==0 && !pathState<bool>(cp,p,DetPathRestartRequested)
        && equal(pathState<V>(cp,p,DetPathLastVelocity),V{}) && global<std::uint64_t>(cp,c,DetActionsTotalCount)==14,
        "same-tick restart survives later controls, clears in pre and never teleports");
    require(s.restore(cp) && bytes(s)==encodeDetCheckpoint(cp),"path action closed boundary restores all bytes");++fullComparisons;++cases;
    std::cout<<"PASS all path controls preserve other state; origin-only restart and actual movement\n";
}
void restoreChecks(){const auto c=config();DeterministicSession s({1,1,1,4,33});require(configure(s,c,scene(c)),s.error());
    advance(s,frame(0,{action(0,20,DetActionKind2D::CollisionEnable,DetActionCode2D::Occupied)}));const auto cp=checkpoint(s);
    const auto state=detActionsStateSchema2D(c);const auto policies=detActionsPolicySchemas2D(c);
    auto bad=cp;writeDetState(state,bad.globals.at(c.stateSchema),DetActionsTotalCount,std::uint64_t{});rejectRestore(s,bad,"zero total with nonzero record");
    bad=cp;writeDetState(state,bad.globals.at(c.stateSchema),DetActionsBudgetTick,std::uint64_t{2});rejectRestore(s,bad,"future budget stamp");
    bad=cp;writeDetState(state,bad.globals.at(c.stateSchema),DetActionsLastCode,std::uint32_t{});rejectRestore(s,bad,"None with nonzero attempt");
    bad=cp;writeDetState(state,bad.globals.at(c.stateSchema),DetActionsLastKind,std::uint32_t{8});rejectRestore(s,bad,"unknown result kind");
    bad=cp;writeDetState(state,bad.globals.at(c.stateSchema),DetActionsTickCount,c.maxActionsPerTick+1);rejectRestore(s,bad,"oversized tick count");
    bad=cp;writeDetState(policies[0],bad.globals.at(policies[0].id),1,std::uint32_t{1});rejectRestore(s,bad,"changed global immutable policy");
    bad=cp;writeDetState(policies[0],bad.actors.at(20).blocks.at(policies[0].id),1,std::uint32_t{1});rejectRestore(s,bad,"changed actor immutable policy");
    bad=cp;writeDetState(state,bad.actors.at(20).blocks.at(c.stateSchema),DetActionsTotalCount,std::uint64_t{1});rejectRestore(s,bad,"actor result authority forbidden");
    bad=cp;writeDetState(detCollisionBodySchema2D(c.collision),bad.actors.at(20).blocks.at(c.collision.bodySchema),DetBodyMask,std::uint32_t{3});rejectRestore(s,bad,"unauthorized target mask");
    const auto& p=*c.targets[2].path;bad=cp;writeDetState(pathSchema(p),bad.actors.at(40).blocks.at(p.stateSchema),DetPathBound,false);rejectRestore(s,bad,"configured path loses binding");
    require(s.restore(cp) && bytes(s)==encodeDetCheckpoint(cp),"valid action record restores exact bytes");++fullComparisons;
    auto changed=c;changed.targets[0].collisionMask=2;DeterministicSession masks({1,1,1,4,33});require(configure(masks,changed,scene(changed)),masks.error());rejectRestore(masks,cp,"changed authorized mask manifest");
    changed=c;changed.targets[2].path->waypoints[1].offset.x=D::fromInt(2);DeterministicSession points({1,1,1,4,33});
    require(configure(points,changed,scene(changed)),points.error());rejectRestore(points,cp,"changed complete path manifest");
    ++cases;std::cout<<"PASS canonical record, immutable global/actor policy, masks, binding, and changed-config atomic restore\n";
}
void configurationChecks(){const auto good=config();
    const auto rejected=[&](DetActions2DConfig bad,const std::string& why){DeterministicSession s({1,1,1,4,33});
        require(!installDetActions2D(s,bad),why+" configuration refused");
        require(installDetActions2D(s,good),why+" preflight has no registration writes");};
    auto bad=good;bad.targets.clear();rejected(bad,"empty table");
    bad=good;bad.targets[1].actor=bad.targets[0].actor;rejected(bad,"duplicate table identity");
    bad=good;bad.targets[0].collisionMask=0;rejected(bad,"zero authorized mask");
    bad=good;bad.maxActionsPerTick=0;rejected(bad,"zero tick budget");
    bad=good;bad.maxActionsPerTick=1025;rejected(bad,"tick budget bound");
    bad=good;bad.stateSchema=bad.collision.bodySchema;rejected(bad,"state/body namespace alias");
    bad=good;bad.policySchemaBase=bad.targets[2].path->stateSchema;rejected(bad,"policy/path namespace alias");
    bad=good;bad.targets[2].path->waypoints[1].travelTicks=0;rejected(bad,"invalid path duration");
    bad=good;bad.targets[2].path->collision.maxBodies=7;rejected(bad,"different embedded collision policy");
    bad=good;bad.targets[2].path->tickPeriod=q(2);rejected(bad,"path/Session period mismatch");
    bad=good;auto owner=*bad.targets[2].path;owner.policySchema=owner.stateSchema+2;owner.waypoints[1].offset.x=D::fromInt(2);
    bad.targets[4].path=owner;rejected(bad,"same path state with different complete policy");
    // Full bounded table: repeated identical path identity is supported. Literal
    // policy bytes, rather than a digest alone, enter each registered chunk.
    auto maximum=good;maximum.targets.clear();auto path=*good.targets[2].path;
    path.waypoints.clear();for(unsigned i=0;i<24;++i)path.waypoints.push_back({{q(static_cast<int>(i)),D{}},4,0});
    for(unsigned i=0;i<8;++i)maximum.targets.push_back({100+i,1,true,path});
    const auto encoded=encodeDetActionsConfig2D(maximum);const auto chunks=detActionsPolicySchemas2D(maximum);
    require(encoded.size()<=8000 && chunks.size()>1 && chunks.size()<=8,"maximum table canonical policy byte budget");
    for(const auto& chunk:chunks)require(chunk.fields.size()<=128,"fixed typed chunk field bound");
    // The action table must match the actually installed path, not merely its IDs.
    const auto mismatch=[&](bool differentPath){DeterministicSession s({1,1,1,4,33});
        require(installDetActions2D(s,good),"mismatch action installation");
        auto actual=good.collision;if(!differentPath)actual.maxBodies=7;
        require(installDetCollision2D(s,actual),"mismatch actual collision installation");
        auto p=*good.targets[2].path;if(differentPath)p.waypoints[1].offset.x=D::fromInt(2);
        else p.collision=actual;
        require(installDetPathMotion2D(s,p),"mismatch actual path installation");
        for(auto& [id,a]:scene(good))require(s.addEntity(id,std::move(a)),"mismatch actor registration");
        require(!s.seal(),differentPath?"same IDs/different actual points reject seal":"different actual collision capacity rejects seal");};
    mismatch(true);mismatch(false);
    // No path is present to indirectly expose a collision ID/priority mismatch.
    // Even legacy collision policies must bind the actual registered callback.
    for(unsigned priority=0;priority<2;++priority){auto declared=good;declared.targets={{20,1,true,{}}};declared.collision.extended=false;
        auto actual=declared.collision;if(priority)declared.collision.priority=-1;else declared.collision.systemId=31;
        DeterministicSession s({1,1,1,4,33});require(installDetActions2D(s,declared) && installDetCollision2D(s,actual),"gate-only mismatch installs independently");
        require(s.addEntity(20,actor(declared,DetBodyMode2D::Static,V{},{q(1),q(1)},1,1)),"gate-only mismatch actor");
        require(!s.seal(),priority?"actual collision priority mismatch rejects seal":"actual collision system ID mismatch rejects seal");}
    ++cases;
    std::cout<<"PASS bounded canonical policy, invalid preflight, and actual installed path/collision identity\n";
}
void clearanceChecks(){
    auto c=config();c.collision.staticSlopes=true;c.targets={{20,2,true,{}}};
    const auto wedge=[&](SimEntityId id){DetActorState a=actor(c,DetBodyMode2D::Static,{D::fromInt(2),D::fromInt(1)},
        {D::fromInt(2),D::fromInt(1)},2,1);
        DetCollisionBody2D b{DetBodyMode2D::Static,{D::fromInt(2),D::fromInt(1)},{},{},2,1,false};
        b.shape=DetCollisionShape2D::SlopeRisesRight;a.blocks[c.collision.bodySchema]=detCollisionBodyState2D(c.collision,b);return std::pair{id,std::move(a)};};
    // The wedge's upper-left enclosing-box corner is empty; the lower-right
    // point is inside its actual filled triangle. Both are exact dyadic boxes.
    for(unsigned inside=0;inside<2;++inside){DeterministicSession s({1,1,1,4,33});const V at=inside?V{q(13),q(1)}:V{q(1),q(7)};
        Actors a{{20,actor(c,DetBodyMode2D::Static,at,{q(1,16),q(1,16)},1,0)},wedge(30)};
        require(configure(s,c,std::move(a)),s.error());advance(s,frame(0,{action(0,20,DetActionKind2D::CollisionEnable,
            inside?DetActionCode2D::Occupied:DetActionCode2D::Applied)}));
        require(body<std::uint32_t>(checkpoint(s),c,20,DetBodyMask)==(inside?0u:2u),"actual wedge clearance, no enclosing-box false occupancy");}
    c.targets={{20,1,true,{}}};DeterministicSession unsupported({1,1,1,4,33});
    require(configure(unsupported,c,Actors{wedge(20)}),unsupported.error());
    advance(unsupported,frame(0,{action(0,20,DetActionKind2D::CollisionDisable,DetActionCode2D::Unsupported)}));
    require(body<std::uint32_t>(checkpoint(unsupported),c,20,DetBodyMask)==1,"wedge target denial does not change mask");
    c=config();c.collision.oneWayPlatforms=true;c.targets={{20,2,true,{}}};
    auto floor=actor(c,DetBodyMode2D::Static,V{},{q(2),q(2)},2,1);
    DetCollisionBody2D platform{DetBodyMode2D::Static,{q(2),q(2)},{},{},2,1,false};platform.oneWay=true;
    floor.blocks[c.collision.bodySchema]=detCollisionBodyState2D(c.collision,platform);
    DeterministicSession conservative({1,1,1,4,33});require(configure(conservative,c,
        Actors{{20,actor(c,DetBodyMode2D::Static,V{},{q(1),q(1)},1,0)},{30,std::move(floor)}}),conservative.error());
    advance(conservative,frame(0,{action(0,20,DetActionKind2D::CollisionEnable,DetActionCode2D::Occupied)}));
    require(body<std::uint32_t>(checkpoint(conservative),c,20,DetBodyMask)==0,"one-way neighbour conservatively blocks ordinary enable");
    ++cases;std::cout<<"PASS real wedge clearance, unsupported wedge target, and conservative one-way enable\n";
}
} // namespace
int main(){try{collisionChecks();budgetChecks();pathChecks();restoreChecks();configurationChecks();clearanceChecks();
    std::cout<<"{\"schema\":1,\"status\":\"passed\",\"cases\":"<<cases<<",\"completeCheckpointComparisons\":"<<fullComparisons
        <<",\"negativeRestores\":"<<negativeRestores<<"}\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL actions2d: "<<e.what()<<'\n';return 1;}}
