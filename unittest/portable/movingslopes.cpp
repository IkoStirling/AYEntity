// design reference: AYEntity/design.md Stage30/31 moving wedges and continuous terrain.
// Small dyadic physical oracles; whole-checkpoint comparisons certify execution
// path equivalence, not independently generated manifest/interpolation bytes.
#include <AYEntity/DeterministicCharacterController2D.h>
#include <AYEntity/DeterministicCollisionQuery2D.h>
#include <AYEntity/DeterministicPathMotion2D.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace ayt::entity;
namespace {
using D=ayt::math::DetFloat32;using V=ayt::math::DetVec2;
using Bytes=std::vector<std::uint8_t>;using Actors=std::vector<std::pair<SimEntityId,DetActorState>>;
constexpr SimEntityId hero=10,platform=20;
unsigned movingCases=0,terrainCases=0,legacyRegressionCases=0,physicalRows=0,completeComparisons=0;
void require(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
D q(int n,int denominator=4){return D::fromInt(n)/D::fromInt(denominator);}
bool equal(V a,V b){return a.x.bits()==b.x.bits() && a.y.bits()==b.y.bits();}
DetCharacterController2DConfig policy(bool continuous=false){DetCharacterController2DConfig c;
    c.collision.staticSlopes=c.collision.movingSlopes=true;c.collision.continuousTerrain=continuous;
    c.collision.maxBodies=8;c.collision.maxTriggerPairs=8;
    c.maxHorizontalSpeed=D::fromInt(4);c.acceleration=c.deceleration=D::fromInt(64);
    c.gravity=D::fromInt(4);c.jumpSpeed=D::fromInt(4);return c;}
DetTypedStateSchema characterSchema(const DetCharacterController2DConfig& c){return {c.stateSchema,
    c.collision.continuousTerrain?kDetCharacterController2DTerrainProfileVersion:
        (c.collision.movingSlopes?kDetCharacterController2DMovingSlopeProfileVersion:kDetCharacterController2DSlopeProfileVersion),{
        {DetCharacterEnabled,false},{DetCharacterGrounded,false},{DetCharacterSupport,DetEntityRef{}},{DetCharacterVelocity,V{}},
        {DetCharacterSupportVelocity,V{}},{DetCharacterTargetSpeed,D{}},{DetCharacterJumpRequested,false},{DetCharacterAppliedCarry,V{}},
        {DetCharacterIntentVelocity,V{}},{DetCharacterJumped,false},{DetCharacterSnapped,false},{DetCharacterSupportTick,std::uint64_t{}},
        {DetCharacterGroundNormal,V{}}}};}
DetTypedStateSchema pathSchema(const DetPathMotion2DConfig& p){return {p.stateSchema,1,{
    {10,false},{20,false},{30,false},{40,false},{50,V{}},{60,std::uint32_t{}},{70,std::int32_t{1}},
    {80,std::uint32_t{}},{90,std::uint32_t{}},{100,V{}},{110,false},{120,false},{130,V{}},{140,false}}};}
DetSessionCheckpoint checkpoint(DeterministicSession& s){auto cp=s.checkpoint();require(bool(cp),s.error());return *cp;}
Bytes bytes(DeterministicSession& s){return encodeDetCheckpoint(checkpoint(s));}
template<class T>T state(const DetSessionCheckpoint& cp,const DetCharacterController2DConfig& c,std::uint32_t field){
    return readDetState<T>(characterSchema(c),cp.actors.at(hero).blocks.at(c.stateSchema),field);}
template<class T>T body(const DetSessionCheckpoint& cp,const DetCharacterController2DConfig& c,SimEntityId id,std::uint32_t field){
    return readDetState<T>(detCollisionBodySchema2D(c.collision),cp.actors.at(id).blocks.at(c.collision.bodySchema),field);}
V position(const DetSessionCheckpoint& cp,SimEntityId id){const auto& p=cp.actors.at(id).pose.position;return {D::fromBits(p[0]),D::fromBits(p[1])};}
DetActorState actor(const DetCharacterController2DConfig& c,DetBodyMode2D mode,V at,V half,V velocity={},
    DetCollisionShape2D shape=DetCollisionShape2D::Aabb,std::uint32_t layer=1,std::uint32_t mask=UINT32_MAX){
    DetActorState a;DetSimTransformComponent pose;require(pose.setPosition({at.x,at.y,{}}),"fixture pose");a.pose=pose.snapshot();
    DetCollisionBody2D b{mode,half,{},velocity,layer,mask,false};b.shape=shape;
    a.blocks[c.collision.bodySchema]=detCollisionBodyState2D(c.collision,b);return a;}
V normal(bool left){return {D::fromInt(left?2:-2),D::fromInt(4)};}
Actors movingScene(const DetCharacterController2DConfig& c,bool left,V velocity={}){
    auto h=actor(c,DetBodyMode2D::Kinematic,{D::fromInt(left?3:1),q(7,8)},{q(1),q(1)},velocity);
    DetCharacterState2D initial;initial.enabled=initial.grounded=true;initial.support={platform};
    initial.supportVelocity=velocity;initial.groundNormal=normal(left);h.blocks[c.stateSchema]=detCharacterState2D(c,initial);
    return {{hero,std::move(h)},{platform,actor(c,DetBodyMode2D::MovingObstacle,{D::fromInt(2),D::fromInt(1)},
        {D::fromInt(2),D::fromInt(1)},velocity,left?DetCollisionShape2D::SlopeRisesLeft:DetCollisionShape2D::SlopeRisesRight)}};}
DetTickInput frame(std::uint64_t tick,int halfSpeed=0,bool jump=false,bool paused=false){
    return {tick,1,{{1,0,1,{static_cast<std::uint8_t>(halfSpeed),static_cast<std::uint8_t>(jump)}},
        {1,1,2,{static_cast<std::uint8_t>(paused)}}}};}
bool configure(DeterministicSession& s,const DetCharacterController2DConfig& c,Actors actors,bool reverse=false,
    const DetPathMotion2DConfig* path=nullptr){
    const auto p=path?*path:DetPathMotion2DConfig{};const bool usePath=path!=nullptr;
    const auto input=[c,p,usePath](DetTickContext& ctx){for(const auto& command:ctx.input().commands){
        if(command.source!=1)return false;
        if(command.type==1 && command.payload.size()==2 && command.payload[1]<=1){
            const int speed=command.payload[0]<128?command.payload[0]:int(command.payload[0])-256;
            detCharacterInput2D(ctx,hero,c,{D::fromInt(speed)/D::fromInt(2),bool(command.payload[1])});
        }else if(command.type==2 && command.payload.size()==1 && command.payload[0]<=1){
            if(usePath)detPathMotionInput2D(ctx,platform,p,{true,bool(command.payload[0]),false});
        }else return false;}return true;};
    if(!reverse && !s.registerSystem(10,0,input))return false;
    if(!installDetCharacterController2D(s,c) || (path && !installDetPathMotion2D(s,p)))return false;
    if(reverse && !s.registerSystem(10,0,input))return false;
    if(reverse)std::reverse(actors.begin(),actors.end());
    for(auto& [id,a]:actors)if(!s.addEntity(id,std::move(a)))return false;return s.seal();}
void advance(DeterministicSession& s,int halfSpeed=0,bool jump=false,bool paused=false){require(s.advance(frame(s.nextTick(),halfSpeed,jump,paused)),s.error());}
void expect(DeterministicSession& s,const DetCharacterController2DConfig& c,V at,V own,V carry,SimEntityId support,V n,const std::string& why){
    const auto cp=checkpoint(s);const auto actual=position(cp,hero);
    require(equal(actual,at),why+" expectedXY="+std::to_string(at.x.bits())+","+std::to_string(at.y.bits())
        +" actualXY="+std::to_string(actual.x.bits())+","+std::to_string(actual.y.bits()));
    require(equal(state<V>(cp,c,DetCharacterVelocity),own) && equal(state<V>(cp,c,DetCharacterSupportVelocity),carry),why+" own/carry");
    require(state<bool>(cp,c,DetCharacterGrounded)==bool(support) && state<DetEntityRef>(cp,c,DetCharacterSupport).value==support
        && equal(state<V>(cp,c,DetCharacterGroundNormal),n),why+" support/normal");
    require(state<std::uint64_t>(cp,c,DetCharacterSupportTick)==(support?cp.nextTick:0),why+" support stamp");++physicalRows;}
void carryChecks(){const auto c=policy();
    // Four ticks translate by one integer in each active coordinate. Relative
    // pose and own velocity stay constant, even for a downward-moving support.
    const std::array<V,4> velocities{{{D::fromInt(1),{}},{D{},D::fromInt(1)},
        {D::fromInt(1),D::fromInt(1)},{D{},-D::fromInt(1)}}};
    for(bool left:{false,true})for(const auto velocity:velocities){
        DeterministicSession a({1,1,1,4,30}),b({1,1,1,4,30});
        require(configure(a,c,movingScene(c,left,velocity)) && configure(b,c,movingScene(c,left,velocity),true),"moving slope seal");
        std::vector<DetSessionCheckpoint> rows{checkpoint(a)};
        for(int n=1;n<=4;++n){advance(a);advance(b);require(bytes(a)==bytes(b),"moving slope full CP reverse order");++completeComparisons;
            expect(a,c,{D::fromInt(left?3:1)+velocity.x*q(n),q(7,8)+velocity.y*q(n)},V{},velocity,platform,normal(left),"dyadic translating wedge");
            require(equal(position(checkpoint(a),platform),V{D::fromInt(2)+velocity.x*q(n),D::fromInt(1)+velocity.y*q(n)}),"actual wedge translation");rows.push_back(checkpoint(a));}
        require(a.restore(rows[2]),"moving support typed restore");
        for(unsigned n=2;n<4;++n){advance(a);require(bytes(a)==encodeDetCheckpoint(rows[n+1]),"moving wedge restore continuation all bytes");++completeComparisons;}
        ++movingCases;}
    std::cout<<"PASS left/right horizontal, upward/downward and diagonal carry; order and restore\n";
}
void walkJumpChecks(){
    for(bool left:{false,true}){const auto c=policy();const V carrier{D::fromInt(1),D::fromInt(1)};
        DeterministicSession s({1,1,1,4,30});require(configure(s,c,movingScene(c,left,carrier)),"relative walk seal");
        for(int n=1;n<=2;++n){advance(s,left?-2:2);
            expect(s,c,{D::fromInt(left?3:1)+(left?D{}:q(2))*D::fromInt(n),q(7+3*n,8)},
                {D::fromInt(left?-1:1),D{}},carrier,platform,normal(left),"relative uphill walking on translated triangle");}++movingCases;
        for(auto leave:{DetCharacterLeaveVelocity2D::Discard,DetCharacterLeaveVelocity2D::Inherit}){
            auto jumpPolicy=c;jumpPolicy.leaveVelocity=leave;DeterministicSession jump({1,1,1,4,30});
            require(configure(jump,jumpPolicy,movingScene(jumpPolicy,left,carrier)),"jumping support seal");advance(jump);
            advance(jump,0,true);const bool inherit=leave==DetCharacterLeaveVelocity2D::Inherit;
            expect(jump,jumpPolicy,{D::fromInt(left?3:1)+q(inherit?2:1),q(inherit?19:17,8)},
                {D::fromInt(inherit?1:0),D::fromInt(inherit?5:4)},V{},0,V{},"jump leaves moving slope with one policy inheritance");++movingCases;}
    }
    std::cout<<"PASS relative slope walking, jump release and Discard/Inherit\n";
}
void pathChecks(){auto c=policy();DetPathMotion2DConfig p;p.collision=c.collision;p.tickPeriod=q(1);
    p.playback=DetPathPlayback2D::Once;p.waypoints={{{},4,0},{{D::fromInt(1),D::fromInt(1)},4,0}};
    auto scene=movingScene(c,false);DetPathMotionState2D bound;bound.bound=true;bound.origin={D::fromInt(2),D::fromInt(1)};
    scene[1].second.blocks[p.stateSchema]=detPathMotionState2D(p,bound);
    DeterministicSession a({1,1,1,4,30}),b({1,1,1,4,30});
    require(configure(a,c,scene,false,&p) && configure(b,c,scene,true,&p),"moving triangle path bind");
    std::vector<DetSessionCheckpoint> rows{checkpoint(a)};
    const std::array<int,8> quarterOffsets{1,2,2,3,4,4,4,4};
    for(unsigned t=0;t<8;++t){advance(a,0,false,t==2);advance(b,0,false,t==2);
        require(bytes(a)==bytes(b),"moving path full CP reverse order");++completeComparisons;
        const auto n=quarterOffsets[t];const V carry=t==2 || t>=5?V{}:V{D::fromInt(1),D::fromInt(1)};
        expect(a,c,{D::fromInt(1)+q(n),q(7,8)+q(n)},V{},carry,platform,normal(false),"path actual carry, paused cursor and terminal stop");
        const auto cp=checkpoint(a);require(readDetState<bool>(pathSchema(p),cp.actors.at(platform).blocks.at(p.stateSchema),DetPathFinished)==(t>=4),"Once completion after actual target");
        rows.push_back(cp);}
    require(a.restore(rows[2]),"path moving support restore");for(unsigned t=2;t<8;++t){advance(a,0,false,t==2);
        require(bytes(a)==encodeDetCheckpoint(rows[t+1]),"path continuation exact CP");++completeComparisons;}++movingCases;
    // High vertical wedge edge touches the ordinary wall. No phantom AABB upper
    // corner is involved in this hand-proved prescribed-motion conflict.
    c.collision.motionPolicy=DetCollisionMotionPolicy2D::StopPrescribedOnConflict;p.collision=c.collision;
    p.waypoints[1].offset={D::fromInt(1),D{}};scene=movingScene(c,false);
    scene[1].second.blocks[p.stateSchema]=detPathMotionState2D(p,bound);
    scene.emplace_back(30,actor(c,DetBodyMode2D::Static,{q(18),D::fromInt(1)},{q(2),D::fromInt(1)}));
    DeterministicSession stopped({1,1,1,4,30});require(configure(stopped,c,std::move(scene),false,&p),"stopped wedge path seal");advance(stopped);
    expect(stopped,c,{D::fromInt(1),q(7,8)},V{},V{},platform,normal(false),"blocked moving wedge cancels applied carry");
    const auto cp=checkpoint(stopped);require(equal(position(cp,platform),V{D::fromInt(2),D::fromInt(1)})
        && body<std::uint32_t>(cp,c,platform,DetBodyMotionResult)==static_cast<std::uint32_t>(DetCollisionMotionResult2D::Blocked)
        && body<DetEntityRef>(cp,c,platform,DetBodyMotionOther).value==30,"typed prescribed wedge block");
    const auto& pathWords=cp.actors.at(platform).blocks.at(p.stateSchema);
    require(readDetState<bool>(pathSchema(p),pathWords,DetPathBlocked)
        && readDetState<std::uint32_t>(pathSchema(p),pathWords,DetPathElapsedTicks)==0,"blocked actual target does not advance path");
    require(stopped.restore(cp) && bytes(stopped)==encodeDetCheckpoint(cp),"stopped wedge/path result restores");++completeComparisons;++movingCases;
    std::cout<<"PASS moving wedge path, pause/resume, blocked actual motion and typed restore\n";
}
void rejectChecks(){const auto c=policy();
    for(unsigned i=0;i<3;++i){auto bad=c;if(i==0)bad.collision.staticSlopes=false;if(i==1)bad.collision.extended=false;
        if(i==2)bad.collision.maxTriggerPairs=58;DeterministicSession s({1,1,1,4,30});
        require(!installDetCharacterController2D(s,bad) && installDetCharacterController2D(s,c),"invalid moving-slope policy rejects without registration pollution");}
    auto invalidShape=[&](DetBodyMode2D mode,bool oneWay,bool trigger){DetCollisionBody2D value{mode,{q(1),q(1)}};
        value.shape=DetCollisionShape2D::SlopeRisesRight;value.oneWay=oneWay;value.trigger=trigger;bool rejected=false;
        try{(void)detCollisionBodyState2D(c.collision,value);}catch(const std::invalid_argument&){rejected=true;}require(rejected,"unsupported moving wedge shape combination");};
    invalidShape(DetBodyMode2D::Kinematic,false,false);invalidShape(DetBodyMode2D::MovingObstacle,false,true);
    auto old=c;old.collision.movingSlopes=false;DetCollisionBody2D moving{DetBodyMode2D::MovingObstacle,{q(1),q(1)}};
    moving.shape=DetCollisionShape2D::SlopeRisesRight;bool rejected=false;
    try{(void)detCollisionBodyState2D(old.collision,moving);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"profile6 retains its static-only wedge contract");
}
void restoreQueryChecks(){const auto c=policy();DeterministicSession s({1,1,1,4,30});
    require(configure(s,c,movingScene(c,false,{D::fromInt(1),D::fromInt(1)})),"restore query seal");advance(s);
    const auto cp=checkpoint(s);const auto before=bytes(s);auto bad=cp;
    writeDetState(characterSchema(c),bad.actors.at(hero).blocks.at(c.stateSchema),DetCharacterGroundNormal,V{D{},D::fromInt(1)});
    require(!s.restore(bad) && bytes(s)==before,"wrong translated wedge ground normal restore atomic");
    bad=cp;writeDetState(detCollisionBodySchema2D(c.collision),bad.actors.at(platform).blocks.at(c.collision.bodySchema),DetBodyMotionOther,DetEntityRef{hero});
    require(!s.restore(bad) && bytes(s)==before,"Clear motion output cannot retain other identity");
    DetCollisionQuery2D query(cp,c.collision);DetCollisionQueryFilter2D filter;filter.ignore=hero;
    const auto hits=query.raycast({q(5),q(13)},{D{},-D::fromInt(4)},filter);
    require(hits.size()==1 && hits.front().entity==platform && hits.front().hit.fraction.bits()==q(5,8).bits()
        && equal(hits.front().hit.normal,normal(false)),"frozen translated wedge ray uses real triangle");
    auto old=c;old.collision.movingSlopes=false;bool refused=false;
    try{(void)DetCollisionQuery2D(cp,old.collision);}catch(const std::invalid_argument&){refused=true;}
    require(refused,"profile6 query cannot consume profile7 checkpoint");
    Actors legacy{{hero,actor(old,DetBodyMode2D::Kinematic,{D::fromInt(1),q(7,8)},{q(1),q(1)})},
        {platform,actor(old,DetBodyMode2D::Static,{D::fromInt(2),D::fromInt(1)},{D::fromInt(2),D::fromInt(1)},V{},DetCollisionShape2D::SlopeRisesRight)}};
    DetCharacterState2D h;h.enabled=true;legacy[0].second.blocks[old.stateSchema]=detCharacterState2D(old,h);
    DeterministicSession oldSession({1,1,1,4,30});require(configure(oldSession,old,std::move(legacy)),"legacy static profile seal");
    const auto oldBefore=bytes(oldSession);require(!oldSession.restore(cp) && bytes(oldSession)==oldBefore,"profile7 cannot restore into6");
    require(!s.restore(checkpoint(oldSession)) && bytes(s)==before,"profile6 cannot restore into7");++movingCases;
    std::cout<<"PASS profile/shape rejection, motion/normal atomic restore and translated triangle query\n";
}
void terrainChecks(){const auto c=policy(true);
    for(bool valley:{false,true})for(bool mirror:{false,true})for(bool swapIds:{false,true}){
        // The envelope of two real triangles: crest has a footprint-wide flat
        // peak; valley changes its active highest foot corner at centerX4.
        const auto leftId=swapIds?30u:20u,rightId=swapIds?20u:30u;
        const auto firstShape=valley?DetCollisionShape2D::SlopeRisesLeft:DetCollisionShape2D::SlopeRisesRight;
        const auto secondShape=valley?DetCollisionShape2D::SlopeRisesRight:DetCollisionShape2D::SlopeRisesLeft;
        const auto startX=mirror?q(18):q(14),startY=valley?q(5,8):q(17,8);
        Actors actors{{hero,actor(c,DetBodyMode2D::Kinematic,{startX,startY},{q(1),q(1)})},
            {leftId,actor(c,DetBodyMode2D::Static,{D::fromInt(2),D::fromInt(1)},{D::fromInt(2),D::fromInt(1)},V{},firstShape)},
            {rightId,actor(c,DetBodyMode2D::Static,{D::fromInt(6),D::fromInt(1)},{D::fromInt(2),D::fromInt(1)},V{},secondShape)}};
        DetCharacterState2D initial;initial.enabled=true;actors[0].second.blocks[c.stateSchema]=detCharacterState2D(c,initial);
        DeterministicSession a({1,1,1,4,31}),b({1,1,1,4,31});
        require(configure(a,c,actors) && configure(b,c,actors,true),"continuous terrain seal");std::vector<DetSessionCheckpoint> rows{checkpoint(a)};
        const std::array<int,4> valleyY{3,5,7,9},crestY{18,17,15,13};
        for(unsigned t=0;t<4;++t){advance(a,mirror?-4:4);advance(b,mirror?-4:4);
            require(bytes(a)==bytes(b),"continuous terrain reverse registration all CP bytes");++completeComparisons;
            const auto cp=checkpoint(a);const auto expectedX=startX+(mirror?-q(2):q(2))*D::fromInt(t+1);
            require(equal(position(cp,hero),V{expectedX,q(valley?valleyY[t]:crestY[t],8)}),"hand envelope crest/valley physical pose row="+std::to_string(t+1));
            require(state<bool>(cp,c,DetCharacterGrounded) && equal(state<V>(cp,c,DetCharacterVelocity),V{D::fromInt(mirror?-2:2),D{}})
                && equal(state<V>(cp,c,DetCharacterSupportVelocity),V{}),"terrain keeps own X speed and grounded closure");
            const auto support=state<DetEntityRef>(cp,c,DetCharacterSupport).value;
            // At a valley endpoint the solver commits the outgoing uphill
            // tangent even when no time remains. A crest has two local flat caps.
            const auto expectedSupport=t==0 && !valley?std::min(leftId,rightId):(mirror?leftId:rightId);
            require(support==expectedSupport,"true handoff, outgoing valley tangent and stable-ID crest tie");
            const auto supportedLeft=support==leftId;const bool risesLeft=supportedLeft?valley:!valley;
            require(equal(state<V>(cp,c,DetCharacterGroundNormal),normal(risesLeft)),"selected shape raw ground normal");
            ++physicalRows;rows.push_back(cp);}
        require(a.restore(rows[1]),"seam checkpoint restores");for(unsigned t=1;t<4;++t){advance(a,mirror?-4:4);
            require(bytes(a)==encodeDetCheckpoint(rows[t+1]),"continuous seam restore continuation all bytes");++completeComparisons;}
        ++terrainCases;}
    auto bad=c;bad.collision.movingSlopes=false;DeterministicSession rejected({1,1,1,4,31});
    require(!installDetCharacterController2D(rejected,bad) && installDetCharacterController2D(rejected,c),"terrain rejects missing moving-slope profile without pollution");
    std::cout<<"PASS crest/valley upper envelope, both directions, swapped stable IDs, actual support handoff and restore\n";
}
void terrainBoundaryChecks(){const auto c=policy(true);
    for(bool left:{false,true}){
        // The first .5-wide move ends with zero slope-footprint overlap. Its
        // outgoing tangent is still flat; only the next move enters the wedge.
        // Rows then retrace the same envelope, including the true low-end exit.
        const auto startX=left?q(19):q(-3);
        Actors actors{{hero,actor(c,DetBodyMode2D::Kinematic,{startX,q(1)},{q(1),q(1)})},
            {20,actor(c,DetBodyMode2D::Static,{D::fromInt(2),D::fromInt(1)},
                {D::fromInt(2),D::fromInt(1)},V{},left?DetCollisionShape2D::SlopeRisesLeft:DetCollisionShape2D::SlopeRisesRight)},
            {30,actor(c,DetBodyMode2D::Static,{D::fromInt(left?6:-2),q(-2)},
                {D::fromInt(2),q(2)})}};
        DetCharacterState2D initial;initial.enabled=true;actors[0].second.blocks[c.stateSchema]=detCharacterState2D(c,initial);
        DeterministicSession s({1,1,1,4,31});require(configure(s,c,std::move(actors)),"low-end flat/wedge seal");
        const std::array<int,6> distance{2,4,6,4,2,0},heights{1,2,3,2,1,1};
        std::vector<DetSessionCheckpoint> rows{checkpoint(s)};
        for(unsigned t=0;t<6;++t){const bool uphill=t<3;const int speed=(left?-4:4)*(uphill?1:-1);
            advance(s,speed);const bool onSlope=t>=1 && t<=3;
            expect(s,c,{startX+(left?-q(distance[t]):q(distance[t])),q(heights[t])},
                {D::fromInt((left?-2:2)*(uphill?1:-1)),D{}},V{},onSlope?20:30,
                onSlope?normal(left):V{D{},D::fromInt(1)},"flat/slope low endpoint row="+std::to_string(t+1));
            const auto cp=checkpoint(s);
            if(t==0 || t>=4)require(body<V>(cp,c,hero,DetBodyVelocity).y.bits()==D{}.bits(),
                "zero-overlap low endpoint and flat exit retain flat body Y");
            rows.push_back(cp);}
        require(s.restore(rows[1]),"zero-overlap endpoint keeps a restorable flat support");
        for(unsigned t=1;t<6;++t){const int speed=(left?-4:4)*(t<3?1:-1);advance(s,speed);
            require(bytes(s)==encodeDetCheckpoint(rows[t+1]),"low-end handoff restore continuation all bytes");++completeComparisons;}
        ++terrainCases;
    }
    // A wall cancels the carrier's X contribution before a later slope contact.
    // The narrow old carrier really leaves the footprint, so the static wedge
    // is the sole endpoint support. A handoff must not turn 0 - oldCarryX into
    // reverse own motion. Masks isolate prescribed geometry from the statics;
    // the character still collides with both groups throughout the real tick.
    auto h=actor(c,DetBodyMode2D::Kinematic,{D::fromInt(1),q(9,8)},{q(1),q(1)},
        {D::fromInt(1),-D::fromInt(1)},DetCollisionShape2D::Aabb,1,6);
    DetCharacterState2D initial;initial.enabled=initial.grounded=true;initial.support={platform};
    initial.supportVelocity={D::fromInt(1),-D::fromInt(1)};initial.groundNormal={D{},D::fromInt(1)};
    h.blocks[c.stateSchema]=detCharacterState2D(c,initial);
    Actors clippedActors{{hero,std::move(h)},
        {platform,actor(c,DetBodyMode2D::MovingObstacle,{q(19,16),q(5,8)},{q(1,16),q(1)},
            {D::fromInt(1),-D::fromInt(1)},DetCollisionShape2D::Aabb,2,1)},
        {30,actor(c,DetBodyMode2D::Static,{D::fromInt(2),D::fromInt(1)},
            {D::fromInt(2),D::fromInt(1)},V{},DetCollisionShape2D::SlopeRisesRight,4,1)},
        {40,actor(c,DetBodyMode2D::Static,{q(6),D::fromInt(2)},{q(1),D::fromInt(1)},
            V{},DetCollisionShape2D::Aabb,4,1)}};
    DeterministicSession clipped({1,1,1,4,31});require(configure(clipped,c,std::move(clippedActors)),"clipped carry to static wedge seal");
    advance(clipped);expect(clipped,c,{D::fromInt(1),q(7,8)},V{},V{},30,normal(false),"wall-clipped carry hands off without reverse own X");
    const auto first=checkpoint(clipped);
    require(equal(position(first,platform),V{q(23,16),q(3,8)})
        && equal(body<V>(first,c,hero,DetBodyVelocity),V{}),"clipped carrier actually leaves and solved world velocity stops");
    advance(clipped);expect(clipped,c,{D::fromInt(1),q(7,8)},V{},V{},30,normal(false),"static slope after clipped handoff does not drift");
    const auto second=bytes(clipped);require(clipped.restore(first),"clipped handoff typed restore");advance(clipped);
    require(bytes(clipped)==second,"clipped handoff restore continuation all bytes");++completeComparisons;++terrainCases;
    // There is a real unit-wide hole between floor [-4,-1] and slope [0,4].
    // A half-width move leaves the old floor, without reaching any triangle.
    // Continuous terrain must neither invent a bridge nor retain old grounding.
    Actors gapActors{{hero,actor(c,DetBodyMode2D::Kinematic,{q(-5),q(1)},{q(1),q(1)})},
        {20,actor(c,DetBodyMode2D::Static,{D::fromInt(2),D::fromInt(1)},
            {D::fromInt(2),D::fromInt(1)},V{},DetCollisionShape2D::SlopeRisesRight)},
        {30,actor(c,DetBodyMode2D::Static,{q(-10),q(-2)},{q(6),q(2)})}};
    initial={};initial.enabled=true;gapActors[0].second.blocks[c.stateSchema]=detCharacterState2D(c,initial);
    DeterministicSession gap({1,1,1,4,31});require(configure(gap,c,std::move(gapActors)),"true floor/slope gap seal");advance(gap,4);
    expect(gap,c,{q(-3),q(1)},{D::fromInt(2),D{}},V{},0,V{},"real gap has no support or magnetic lift");
    const auto gapCp=checkpoint(gap);require(gap.restore(gapCp) && bytes(gap)==encodeDetCheckpoint(gapCp),"unsupported gap state restores");
    ++completeComparisons;++terrainCases;
    std::cout<<"PASS mirrored flat/low-slope entry and exit, clipped carry handoff, actual gap and restore\n";
}
void legacySignedZeroChecks(){auto c=policy();c.collision.movingSlopes=false;
    // Profile6 admits an authored Static -0 velocity. At the true high-end
    // surface split its character body must still receive the historical +0 Y,
    // while the support's authored bits remain intact. Both runs are physical
    // hand oracles, not a claim that differing authored checkpoints are equal.
    for(bool negative:{false,true}){const auto authoredY=D::fromBits(negative?0x80000000u:0u);
        Actors actors{{hero,actor(c,DetBodyMode2D::Kinematic,{q(14),q(17,8)},{q(1),q(1)})},
            {platform,actor(c,DetBodyMode2D::Static,{D::fromInt(2),D::fromInt(1)},
                {D::fromInt(2),D::fromInt(1)},{D{},authoredY},DetCollisionShape2D::SlopeRisesRight)}};
        DetCharacterState2D initial;initial.enabled=true;actors[0].second.blocks[c.stateSchema]=detCharacterState2D(c,initial);
        DeterministicSession s({1,1,1,4,31});require(configure(s,c,std::move(actors)),"legacy signed-zero high surface seal");advance(s,4);
        expect(s,c,{D::fromInt(4),q(9)},{D::fromInt(2),D{}},{D{},authoredY},platform,normal(false),"legacy Static signed-zero high surface");
        const auto cp=checkpoint(s);require(body<V>(cp,c,hero,DetBodyVelocity).y.bits()==0
            && body<V>(cp,c,platform,DetBodyVelocity).y.bits()==authoredY.bits(),"legacy high-end body +0 and authored Static Y bits");
        require(s.restore(cp) && bytes(s)==encodeDetCheckpoint(cp),"legacy signed-zero endpoint restores all bytes");++completeComparisons;
    }
    ++legacyRegressionCases;std::cout<<"PASS legacy profile6 Static signed-zero high-surface body bits and restore\n";
}
void disabledLandingChecks(){
    // This actor has the facade's disabled defaults, so it remains an ordinary
    // Kinematic body. On n=(-2,4), incoming (3/2,-1/2) has dot=-5 and n.n=20:
    // projection is exactly (1,1/2), moving (1/4,1/8). No character landing
    // policy may clamp it to the disabled intent's zero X or adopt a carrier.
    std::array<DetSessionCheckpoint,2> rows;unsigned lane=0;
    for(bool continuous:{false,true}){const auto c=policy(continuous);
        Actors actors{{hero,actor(c,DetBodyMode2D::Kinematic,{D::fromInt(1),q(7,8)},{q(1),q(1)},
                {q(6),q(-2)})},
            {platform,actor(c,DetBodyMode2D::Static,{D::fromInt(2),D::fromInt(1)},
                {D::fromInt(2),D::fromInt(1)},V{},DetCollisionShape2D::SlopeRisesRight)}};
        DeterministicSession s({1,1,1,4,31});require(configure(s,c,std::move(actors)),"disabled ordinary Kinematic slope seal");
        require(s.advance({0,1,{}}),s.error());
        expect(s,c,{q(5),D::fromInt(1)},V{},V{},0,V{},"disabled controller retains ordinary relative projection");
        rows[lane++]=checkpoint(s);const auto& cp=rows[lane-1];
        require(equal(body<V>(cp,c,hero,DetBodyVelocity),V{D::fromInt(1),q(2)})
            && cp.actors.at(hero).blocks.at(c.stateSchema)==detStateDefaults(characterSchema(c)),"disabled default state stays unchanged on real slope hit");
    }
    // Profiles/manifest/policy differ by design; compare only the actual physical
    // pose and body lanes. This is not a whole-checkpoint comparison counter.
    require(rows[0].actors.at(hero).pose==rows[1].actors.at(hero).pose
        && rows[0].actors.at(hero).blocks.at(policy().collision.bodySchema)==rows[1].actors.at(hero).blocks.at(policy(true).collision.bodySchema),
        "disabled ordinary Kinematic physical result agrees across profiles7/8");
    ++movingCases;std::cout<<"PASS disabled Kinematic n2 projection remains independent of character landing policy\n";
}
void initialLandingCapChecks(){const auto c=policy(true);
    // The airborne foot starts 1/8 above the right wedge's surface. Input2 and
    // gravity4 at dt1/4 give (2,-1), making the real plane contact at fraction
    // 1/4. The remaining movement crosses the highest foot corner at X4 and
    // must finish on the top-flat envelope, rather than launching above it.
    Actors actors{{hero,actor(c,DetBodyMode2D::Kinematic,{q(14),q(9)},{q(1),q(1)})},
        {platform,actor(c,DetBodyMode2D::Static,{D::fromInt(2),D::fromInt(1)},
            {D::fromInt(2),D::fromInt(1)},V{},DetCollisionShape2D::SlopeRisesRight)}};
    DetCharacterState2D initial;initial.enabled=true;actors[0].second.blocks[c.stateSchema]=detCharacterState2D(c,initial);
    DeterministicSession s({1,1,1,4,31});require(configure(s,c,std::move(actors)),"airborne initial landing then cap seal");
    advance(s,4);expect(s,c,{D::fromInt(4),q(9)},{D::fromInt(2),D{}},V{},platform,normal(false),
        "actual first landing establishes same-tick high-cap following");
    const auto cp=checkpoint(s);require(body<V>(cp,c,hero,DetBodyVelocity).y.bits()==0,
        "same-tick initial landing/high-cap body Y is canonical flat zero");
    require(s.restore(cp) && bytes(s)==encodeDetCheckpoint(cp),"same-tick initial landing/high-cap full checkpoint restores");
    ++completeComparisons;++terrainCases;std::cout<<"PASS true initial landing then high-cap transition in one tick\n";
}
void shapePairChecks(){auto c=policy();c.collision.motionPolicy=DetCollisionMotionPolicy2D::StopPrescribedOnConflict;
    for(auto mode:{DetBodyMode2D::Static,DetBodyMode2D::MovingObstacle}){
        // Bounding boxes overlap in [0,1]x[1,2], but A's true upper surface in
        // the shared X interval is <=.5, strictly below B's base at Y1.
        DeterministicSession empty({1,1,1,4,30});require(installDetCollision2D(empty,c.collision),"triangle-pair install");
        require(empty.addEntity(20,actor(c,mode,{D::fromInt(2),D::fromInt(1)},{D::fromInt(2),D::fromInt(1)},V{},DetCollisionShape2D::SlopeRisesRight))
            && empty.addEntity(30,actor(c,DetBodyMode2D::MovingObstacle,{q(-2),D::fromInt(2)},{q(6),D::fromInt(1)},
                {D::fromInt(1),D{}},DetCollisionShape2D::SlopeRisesRight)) && empty.seal(),"true separated triangle pair seal");
        require(empty.advance({0,1,{}}),empty.error());const auto separated=checkpoint(empty);
        require(equal(position(separated,30),V{q(-1),D::fromInt(2)}) && equal(body<V>(separated,c,30,DetBodyVelocity),V{D::fromInt(1),D{}})
            && body<std::uint32_t>(separated,c,30,DetBodyMotionResult)==0,"empty triangle corners do not block prescribed motion");++physicalRows;++movingCases;
        // The opposing high vertical faces really touch at X4. No empty bounding
        // corner participates; a translated triangle would otherwise penetrate.
        DeterministicSession contact({1,1,1,4,30});require(installDetCollision2D(contact,c.collision),"touch triangle-pair install");
        const V firstVelocity=mode==DetBodyMode2D::MovingObstacle?V{D::fromInt(1),D{}}:V{};
        require(contact.addEntity(20,actor(c,mode,{D::fromInt(2),D::fromInt(1)},{D::fromInt(2),D::fromInt(1)},firstVelocity,DetCollisionShape2D::SlopeRisesRight))
            && contact.addEntity(30,actor(c,DetBodyMode2D::MovingObstacle,{D::fromInt(6),D::fromInt(1)},{D::fromInt(2),D::fromInt(1)},
                {-D::fromInt(1),D{}},DetCollisionShape2D::SlopeRisesLeft)) && contact.seal(),"touching high-face triangles seal");
        require(contact.advance({0,1,{}}),contact.error());const auto stopped=checkpoint(contact);
        require(equal(position(stopped,30),V{D::fromInt(6),D::fromInt(1)}) && equal(body<V>(stopped,c,30,DetBodyVelocity),V{})
            && body<std::uint32_t>(stopped,c,30,DetBodyMotionResult)==1 && body<DetEntityRef>(stopped,c,30,DetBodyMotionOther).value==20
            && body<std::uint32_t>(stopped,c,30,DetBodyMotionAxis)==0,"true moving-wedge contact commits typed Blocked");
        if(mode==DetBodyMode2D::MovingObstacle)require(equal(body<V>(stopped,c,20,DetBodyVelocity),V{})
            && body<std::uint32_t>(stopped,c,20,DetBodyMotionResult)==1 && body<DetEntityRef>(stopped,c,20,DetBodyMotionOther).value==30,"both directly opposed prescribed wedges stop");
        require(contact.restore(stopped) && bytes(contact)==encodeDetCheckpoint(stopped),"accepted wedge-pair output restores");++completeComparisons;++physicalRows;++movingCases;
    }
    // Only the high-end horizontal support segment establishes this direct Y
    // contradiction. This is not evidence for general inclined-face squeezing.
    auto h=actor(c,DetBodyMode2D::Kinematic,{D::fromInt(4),q(9)},{q(1),q(1)},{D{},D::fromInt(1)});
    DetCharacterState2D initial;initial.enabled=initial.grounded=true;initial.support={platform};
    initial.supportVelocity={D{},D::fromInt(1)};initial.groundNormal=normal(false);h.blocks[c.stateSchema]=detCharacterState2D(c,initial);
    Actors actors{{hero,std::move(h)},{platform,actor(c,DetBodyMode2D::MovingObstacle,{D::fromInt(2),D::fromInt(1)},
        {D::fromInt(2),D::fromInt(1)},{D{},D::fromInt(1)},DetCollisionShape2D::SlopeRisesRight)},
        {30,actor(c,DetBodyMode2D::Static,{D::fromInt(4),q(11)},{D::fromInt(2),q(1)})}};
    DeterministicSession pinched({1,1,1,4,30});require(configure(pinched,c,std::move(actors)),"high-cap Y pinch seal");advance(pinched);
    expect(pinched,c,{D::fromInt(4),q(9)},V{},V{},platform,normal(false),"true high-cap pinch cancels continuous carry");
    const auto cp=checkpoint(pinched);require(equal(position(cp,platform),V{D::fromInt(2),D::fromInt(1)})
        && body<std::uint32_t>(cp,c,platform,DetBodyMotionResult)==2 && body<DetEntityRef>(cp,c,platform,DetBodyMotionOther).value==hero
        && body<std::uint32_t>(cp,c,platform,DetBodyMotionAxis)==1 && body<std::uint32_t>(cp,c,hero,DetBodyMotionResult)==2,
        "represented direct cap/roof contradiction commits CrushPrevented");
    require(pinched.restore(cp) && bytes(pinched)==encodeDetCheckpoint(cp),"high-cap classified result restores");++completeComparisons;++movingCases;
    std::cout<<"PASS true triangle/triangle separation and contact, direct high-cap roof pinch\n";
}
} // namespace
int main(int argc,char** argv){try{bool moving=true,terrain=true;
    if(argc==2 && std::string(argv[1])=="--moving-only")terrain=false;
    else if(argc==2 && std::string(argv[1])=="--terrain-only")moving=false;
    else require(argc==1,"usage: AYEntity_MovingSlopesChecks [--moving-only|--terrain-only]");
    if(moving){carryChecks();walkJumpChecks();pathChecks();rejectChecks();restoreQueryChecks();shapePairChecks();legacySignedZeroChecks();disabledLandingChecks();}
    if(terrain){terrainChecks();terrainBoundaryChecks();initialLandingCapChecks();}
    std::cout<<"{\"schema\":1,\"status\":\"passed\",\"movingProfile\":7,\"terrainProfile\":8,\"movingCases\":"<<movingCases
        <<",\"terrainCases\":"<<terrainCases<<",\"legacyRegressionCases\":"<<legacyRegressionCases
        <<",\"physicalRows\":"<<physicalRows<<",\"completeCheckpointComparisons\":"<<completeComparisons<<"}\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL moving-slopes: "<<e.what()<<'\n';return 1;}}
