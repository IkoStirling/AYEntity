// design reference: AYEntity/design.md Stage55 finite character input ownership (OUT draft).
// Selected hand windows/velocity/geometry are separate from whole CP execution equality.
#include <AYEntity/DeterministicCharacterControl2D.h>
#include <AYEntity/DeterministicReplay.h>
#include <AYEntity/DeterministicRollback.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
using namespace ayt::entity;
namespace fs=std::filesystem;
namespace {
using D=ayt::math::DetFloat32;using V=ayt::math::DetVec2;using Bytes=std::vector<std::uint8_t>;
using Phase=DetCharacterControlPhase2D;using Outcome=DetCharacterControlOutcome2D;
std::uint64_t handRows=0,advanceCalls=0,cpComparisons=0,negativeRestores=0,negativeConfigs=0,confirmedEvents=0,rollbacks=0,replayedTicks=0;
struct Witness {std::string phase;std::optional<DetSessionCheckpoint> before,actual,expected;std::optional<DetTickInput> input;} witness;
void require(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why.empty()?"Unspecified setup/execution failure":why);}
D q(int n){return D::fromInt(n)/D::fromInt(4);}
DetSessionConfig recipe(){return {55,1,1,4,55};}
DetSessionCheckpoint cp(DeterministicSession& s){const auto value=s.checkpoint();require(bool(value),s.error());return *value;}
Bytes bytes(DeterministicSession& s){return encodeDetCheckpoint(cp(s));}
void advance(DeterministicSession& s,const DetTickInput& frame,const std::string& phase){witness={phase,cp(s),{},{},frame};
    const bool moved=s.advance(frame);witness.actual=s.checkpoint();require(moved,s.error());++advanceCalls;}
void equal(DeterministicSession& s,const DetSessionCheckpoint& expected,const std::string& why){witness.actual=cp(s);witness.expected=expected;
    if(encodeDetCheckpoint(*witness.actual)!=encodeDetCheckpoint(expected)){const auto difference=firstDetDifference(expected,*witness.actual);
        throw std::runtime_error(why+(difference?" field="+std::to_string(difference->field):" bytes"));}++cpComparisons;}
DetCharacterControl2DConfig policy(){DetCharacterControl2DConfig c;c.character.advanced=true;c.character.collision.oneWayPlatforms=true;
    c.character.maxHorizontalSpeed=D::fromInt(4);c.character.acceleration=c.character.deceleration=D::fromInt(128);
    c.character.gravity=c.character.jumpSpeed=D{};c.owners={{10,D::fromInt(4),3,2,false,true}};return c;}
struct Geometry {D ownerX{},ownerY=q(1);bool supported=false,wall=false;};
DetActorState actor(const DetCharacterControl2DConfig& c,SimEntityId id,Geometry g){DetActorState value;V center,half;std::uint32_t layer,mask;bool trigger=false;
    if(id==10){center={g.ownerX,g.ownerY};half={q(1),q(1)};layer=1;mask=6;}
    else if(id==30){center={D{},-q(1)};half={D::fromInt(128),q(1)};layer=2;mask=1;}
    else if(id==40){center={D::fromInt(3),q(1)};half={D::fromInt(16),D::fromInt(1)};layer=4;mask=1;trigger=true;}
    else if(id==50){center={D::fromInt(2),q(1)};half={q(1),q(1)};layer=2;mask=1;}
    else throw std::invalid_argument("Unknown directed actor");
    DetSimTransformComponent pose;require(pose.setPosition({center.x,center.y,D{}}),"Author actual pose");value.pose=pose.snapshot();
    value.blocks[c.character.collision.bodySchema]=detCollisionBodyState2D(c.character.collision,
        {id==10?DetBodyMode2D::Kinematic:DetBodyMode2D::Static,half,V{},V{},layer,mask,trigger});
    if(id==10){DetCharacterState2D state;state.enabled=true;if(g.supported){state.grounded=true;state.support=DetEntityRef{30};}
        value.blocks[c.character.stateSchema]=detCharacterState2D(c.character,state);}return value;}
DetTickInput frame(std::uint64_t tick,DetCharacterControlInput2D input={}){const auto speed=input.character.targetSpeed.toInt();
    require(speed.has_value() && D::fromInt(*speed).bits()==input.character.targetSpeed.bits() && *speed>=-4 && *speed<=4,"Fixture encodes exact small integer speed");
    return {tick,1,{{1,0,1,{static_cast<std::uint8_t>(*speed+4),static_cast<std::uint8_t>(input.character.jumpPressed),
        static_cast<std::uint8_t>(input.character.crouchHeld),static_cast<std::uint8_t>(input.character.dropPressed),
        static_cast<std::uint8_t>(input.dashHeld),static_cast<std::uint8_t>(input.facingRight),static_cast<std::uint8_t>(input.cancel)}},{1,1,99,{55}}}};}
DetCharacterControlInput2D sample(int speed,bool held,bool right=true,bool cancel=false,bool jump=false){DetCharacterControlInput2D in;
    in.character={D::fromInt(speed),jump,false,false};in.dashHeld=held;in.facingRight=right;in.cancel=cancel;return in;}
void extra(DetTickInput& in,std::uint32_t type,std::uint8_t payload){in.commands.push_back({1,static_cast<std::uint32_t>(in.commands.size()),type,{payload}});}
bool inputSystem(DeterministicSession& s,const DetCharacterControl2DConfig& c){return s.registerSystem(4,c.character.collision.priority,[c](DetTickContext& ctx){
    for(const auto& cmd:ctx.input().commands){if(cmd.source!=1)return false;
        if(cmd.type==1 && cmd.payload.size()==7 && cmd.payload[0]<=8){for(unsigned n=1;n<7;++n)if(cmd.payload[n]>1)return false;
            const auto ids=ctx.entities();if(std::binary_search(ids.begin(),ids.end(),SimEntityId{10})){
                DetCharacterControlInput2D in;in.character={D::fromInt(int(cmd.payload[0])-4),cmd.payload[1]!=0,cmd.payload[2]!=0,cmd.payload[3]!=0};
                in.dashHeld=cmd.payload[4]!=0;in.facingRight=cmd.payload[5]!=0;in.cancel=cmd.payload[6]!=0;detCharacterControlInput2D(ctx,c,10,in);}}
        else if(cmd.type==2 && cmd.payload==Bytes{10})ctx.despawn(10);
        else if(cmd.type==3 && cmd.payload.size()==1 && c.respawn){const auto result=detCharacterRespawnRequest2D(ctx,10,*c.respawn,cmd.payload[0]);
            if(result!=DetRespawnRequestCode2D::Queued)return false;}
        else if(cmd.type==99 && cmd.payload==Bytes{55})continue;else return false;}return true;});}
bool configure(DeterministicSession& s,const DetCharacterControl2DConfig& c,bool reverse=false,Geometry g={}){
    witness={"configure-control",{},{},{},{}};if(reverse && !inputSystem(s,c))return false;
    if(c.respawn){if(!installDetCharacterRespawn2D(s,*c.respawn))return false;}else if(!installDetCharacterController2D(s,c.character))return false;
    if(!installDetCharacterControl2D(s,c) || (!reverse && !inputSystem(s,c)))return false;
    std::vector<SimEntityId> ids{10,30,40};if(g.wall)ids.push_back(50);if(reverse)std::reverse(ids.begin(),ids.end());
    for(auto id:ids)if(!s.addEntity(id,actor(c,id,g)))return false;return s.seal();}
V own(const DetSessionCheckpoint& checkpoint,const DetCharacterControl2DConfig& c){const auto schema=detCharacterStateSchema2D(c.character);
    return readDetState<V>(schema,checkpoint.actors.at(10).blocks.at(schema.id),DetCharacterVelocity);}
D x(const DetSessionCheckpoint& checkpoint){return D::fromBits(checkpoint.actors.at(10).pose.position[0]);}
D y(const DetSessionCheckpoint& checkpoint){return D::fromBits(checkpoint.actors.at(10).pose.position[1]);}
std::vector<DetTickInput> inputs(){std::vector<DetTickInput> frames;for(unsigned t=0;t<16;++t){
    const bool held=t!=2 && t!=6 && t!=10 && t!=13,right=t==0 || (t>=8 && t!=15);
    frames.push_back(frame(t,sample(1,held,right,t==8)));}return frames;}
void hand(const DetSessionCheckpoint& checkpoint,const DetCharacterControl2DConfig& c,unsigned t){
    const auto state=detCharacterControlState2D(checkpoint,c,10);
    static constexpr std::array<Phase,16> phase{Phase::Active,Phase::Active,Phase::Active,Phase::Cooldown,Phase::Cooldown,Phase::Ready,Phase::Ready,
        Phase::Active,Phase::Cooldown,Phase::Cooldown,Phase::Cooldown,Phase::Cooldown,Phase::Ready,Phase::Ready,Phase::Active,Phase::Active};
    static constexpr std::array<Outcome,16> outcome{Outcome::Started,Outcome::Active,Outcome::Active,Outcome::DeniedCooldown,Outcome::Normal,Outcome::Normal,
        Outcome::Normal,Outcome::Started,Outcome::Canceled,Outcome::Normal,Outcome::Normal,Outcome::DeniedCooldown,Outcome::Normal,Outcome::Normal,Outcome::Started,Outcome::Active};
    static constexpr std::array<int,16> speed{4,4,4,1,1,1,1,-4,1,1,1,1,1,1,4,4};
    static constexpr std::array<int,16> quarters{4,8,12,13,14,15,16,12,13,14,15,16,17,18,22,26};
    static constexpr std::array<unsigned,16> requests{1,1,1,2,2,2,2,3,3,3,3,4,4,4,5,5};
    const unsigned started=t<7?1:(t<14?2:3),denied=t<3?0:(t<11?1:2),cancelled=t<8?0:1;
    const std::uint64_t start=t<7?0:(t<14?7:14),end=start+3,ready=start+5;
    require(state.phase==phase[t] && state.outcome==outcome[t] && state.emitted.targetSpeed.bits()==D::fromInt(speed[t]).bits(),"Hand actual active/cooldown/ready and edge-denial table tick="+std::to_string(t));
    require(state.startTick==start && state.endTick==end && state.readyTick==ready && state.lockedFacingRight==(t<7 || t>=14),"Hand fixed facing and original finite deadlines");
    require(state.requestCount==requests[t] && state.startCount==started && state.deniedCount==denied && state.cancelCount==cancelled
        && state.decisionCount==t+1 && state.lastDecisionTick==t,"Hand attempt/start/deny/cancel and real processed counts");
    require(state.ownerPresent && !state.observedAccepted && state.observedGrounded==(t!=0),"Pre-Control actual ground snapshot is not post contact");
    require(own(checkpoint,c).x.bits()==D::fromInt(speed[t]).bits() && x(checkpoint).bits()==q(quarters[t]).bits()
        && y(checkpoint).bits()==q(1).bits(),"Hand dyadic physical movement through the sole Character/collision pipeline");++handRows;}
void selected(){auto c=policy();
    // Initial floor contact is ungrounded until a real Character pre/post runs.
    auto grounded=c;grounded.owners[0].groundOnly=true;grounded.owners[0].durationTicks=1;grounded.owners[0].cooldownTicks=0;
    DeterministicSession ground(recipe());require(configure(ground,grounded),ground.error());
    for(unsigned t=0;t<7;++t){const bool held=t!=2 && t!=5;advance(ground,frame(t,sample(0,held)),"selected-ground-ready");
        const auto state=detCharacterControlState2D(cp(ground),grounded,10);
        require(state.observedGrounded==(t!=0) && state.startCount==(t<3?0u:(t<6?1u:2u)),"Hand groundOnly uses previous committed contact and held does not repeat");
        if(t==0)require(state.outcome==Outcome::DeniedGround && state.deniedCount==1,"Initial ground-only refusal consumes the edge");
        if(t==3 || t==6)require(state.phase==Phase::Active && state.outcome==Outcome::Started && state.endTick==t+1 && state.readyTick==t+1,"Duration1/cooldown0 exact last emitted tick");++handRows;}
    auto busy=c;busy.owners[0].cooldownTicks=0;DeterministicSession active(recipe());require(configure(active,busy),active.error());
    for(unsigned t=0;t<3;++t){advance(active,frame(t,sample(0,t!=1)),"selected-active-edge");const auto state=detCharacterControlState2D(cp(active),busy,10);
        if(t==2)require(state.phase==Phase::Active && state.outcome==Outcome::DeniedActive && state.startCount==1 && state.deniedCount==1
            && state.emitted.targetSpeed.bits()==D::fromInt(4).bits(),"Busy rising edge consumed while original override continues");++handRows;}
    DeterministicSession canceled(recipe());require(configure(canceled,c),canceled.error());
    advance(canceled,frame(0,sample(2,true,true,true)),"selected-canceled-edge");auto state=detCharacterControlState2D(cp(canceled),c,10);
    require(state.outcome==Outcome::DeniedCancel && !state.startCount && state.deniedCount==1 && state.emitted.targetSpeed.bits()==D::fromInt(2).bits(),"Cancel denies fresh start but preserves normal target");++handRows;
    advance(canceled,frame(1,sample(2,true)),"selected-cancel-release-held");state=detCharacterControlState2D(cp(canceled),c,10);
    require(!state.startCount && state.phase==Phase::Ready,"Cancel release does not replay consumed held edge");++handRows;
    auto slow=c;slow.character.acceleration=slow.character.deceleration=D::fromInt(1);DeterministicSession acceleration(recipe());require(configure(acceleration,slow),acceleration.error());
    for(unsigned t=0;t<2;++t){advance(acceleration,frame(t,sample(0,true)),"selected-real-acceleration");const auto checkpoint=cp(acceleration);
        require(own(checkpoint,slow).x.bits()==q(t+1).bits() && x(checkpoint).bits()==(D::fromInt(t?3:1)/D::fromInt(16)).bits(),"Dash target does not bypass acceleration or author an exact distance");++handRows;}
    DeterministicSession wall(recipe());require(configure(wall,c,false,{D{},q(1),false,true}),wall.error());
    for(unsigned t=0;t<3;++t){advance(wall,frame(t,sample(0,true)),"selected-real-wall");const auto checkpoint=cp(wall);const auto st=detCharacterControlState2D(checkpoint,c,10);
        require(st.phase==Phase::Active && st.emitted.targetSpeed.bits()==D::fromInt(4).bits() && x(checkpoint).bits()==(t?q(6):D::fromInt(1)).bits(),"Active dash is clipped by real wall");
        if(t)require(own(checkpoint,c).x.bits()==0,"Wall clears own speed while control window remains active");++handRows;}
    auto jumping=c;jumping.character.gravity=D::fromInt(4);jumping.character.jumpSpeed=D::fromInt(4);
    DeterministicSession jump(recipe());require(configure(jump,jumping,false,{D{},q(1),true,false}),jump.error());
    for(unsigned t=0;t<2;++t){advance(jump,frame(t,sample(0,true,true,false,t==0)),"selected-preserved-jump");const auto checkpoint=cp(jump);const auto st=detCharacterControlState2D(checkpoint,jumping,10);
        require(st.emitted.jumpPressed==(t==0) && st.phase==Phase::Active && own(checkpoint,jumping).y.bits()==D::fromInt(t?3:4).bits()
            && y(checkpoint).bits()==(t?D::fromInt(2):q(5)).bits(),"Dash preserves actual jump and subsequent gravity");++handRows;}
    DeterministicSession retired(recipe());require(configure(retired,c),retired.error());auto input=frame(0,sample(0,true));extra(input,2,10);advance(retired,input,"selected-queued-retirement");
    advance(retired,frame(1,sample(0,true)),"selected-retired-control");state=detCharacterControlState2D(cp(retired),c,10);
    require(!cp(retired).actors.contains(10) && state.phase==Phase::Retired && state.outcome==Outcome::Retired && !state.ownerPresent
        && state.emitted.targetSpeed.bits()==0 && !state.emitted.jumpPressed && !state.emitted.crouchHeld && !state.emitted.dropPressed,"Retired stable ID clears emitted input without a missing actor read");++handRows;
    std::cout<<"PASS selected exact finite edges, ground observation, real acceleration/wall/jump and retirement\n";}
void relocation(){auto c=policy();c.character.collision.characterRespawn=true;DetCharacterRespawn2DConfig r;r.character=c.character;
    r.spawns={{1,10,{D::fromInt(4),q(1)}},{2,10,{D::fromInt(2),q(1)}}};c.respawn=r;
    for(bool occupied:{false,true}){DeterministicSession session(recipe());require(configure(session,c,false,{D{},q(1),false,true}),session.error());
        auto input=frame(0,sample(0,true));extra(input,3,occupied?2:1);advance(session,input,"selected-resolved-relocation");auto checkpoint=cp(session);
        const auto resolved=detCharacterRespawnState2D(checkpoint,10,r);auto state=detCharacterControlState2D(checkpoint,c,10);
        require(resolved.result==(occupied?DetRespawnResult2D::Occupied:DetRespawnResult2D::Accepted) && resolved.resultTick==1
            && state.phase==Phase::Active && state.startCount==1 && state.readyTick==5 && !state.cancelCount,"Only real completed relocation is a future control cancellation source");++handRows;
        const auto before=encodeDetCheckpoint(checkpoint);require(session.restore(checkpoint) && bytes(session)==before,"Current Accepted input-reset exception restores complete CP");++cpComparisons;
        advance(session,frame(1,sample(0,true)),"selected-relocation-observation");checkpoint=cp(session);state=detCharacterControlState2D(checkpoint,c,10);
        require(state.observedAccepted!=occupied && state.cancelCount==(occupied?0u:1u) && state.readyTick==5 && state.startCount==1
            && state.outcome==(occupied?Outcome::Active:Outcome::RelocationCanceled) && state.phase==(occupied?Phase::Active:Phase::Cooldown),"Accepted cancels next decision; Occupied leaves the same finite active window");++handRows;
        if(!occupied)require(x(checkpoint).bits()==D::fromInt(4).bits() && own(checkpoint,c).x.bits()==0,"Accepted reset plus normal0 produces no hidden continuation");}
    DeterministicSession freshEdge(recipe());require(configure(freshEdge,c),freshEdge.error());auto input=frame(0,sample(0,false));extra(input,3,1);advance(freshEdge,input,"selected-relocation-before-edge");
    advance(freshEdge,frame(1,sample(0,true)),"selected-fresh-relocation-edge");const auto state=detCharacterControlState2D(cp(freshEdge),c,10);
    require(state.observedAccepted && state.outcome==Outcome::DeniedRelocation && state.requestCount==1 && state.deniedCount==1
        && !state.startCount && !state.cancelCount,"Fresh Accepted boundary consumes a same-tick rising dash instead of starting");++handRows;
    std::cout<<"PASS current Accepted post/reset, next-tick cancel and Occupied/fresh-edge boundaries\n";}
void write(const fs::path& path,const Bytes& data){std::ofstream output(path,std::ios::binary);output.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));
    output.close();require(bool(output),"Write "+path.filename().string());}
void closure(const fs::path& output){const auto c=policy();const auto frames=inputs();DeterministicSession baseline(recipe());require(configure(baseline,c),baseline.error());
    std::vector<DetSessionCheckpoint> rows{cp(baseline)};std::vector<DetConfirmedEvent> events;
    for(unsigned t=0;t<16;++t){advance(baseline,frames[t],"closure-baseline");rows.push_back(cp(baseline));hand(rows.back(),c,t);
        for(const auto& event:rows.back().pendingEvents)events.push_back({1,t,event});}require(!events.empty(),"Real trigger-confirmation lane is not empty");confirmedEvents=events.size();
    DeterministicSession reversed(recipe());require(configure(reversed,c,true),reversed.error());equal(reversed,rows[0],"Reverse initial whole CP");
    for(unsigned t=0;t<16;++t){auto in=frames[t];std::reverse(in.commands.begin(),in.commands.end());advance(reversed,in,"closure-reversed");equal(reversed,rows[t+1],"Reverse whole CP");}
    DeterministicSession restored(recipe());require(configure(restored,c,true),restored.error());const bool accepted=restored.restore(rows[2]);require(accepted,restored.error());equal(restored,rows[2],"Restore during locked active window");
    for(unsigned t=2;t<16;++t){advance(restored,frames[t],"closure-restored");equal(restored,rows[t+1],"Continued whole CP");}
    DeterministicSession corrected(recipe());require(configure(corrected,c),corrected.error());DetRollbackConfig rc;rc.maxPredictionTicks=4;
    // Complete Control held samples use Hold. Metadata and one-shot topology/relocation requests use Omit.
    rc.prediction={{1,DetPredictionMode::Hold},{2,DetPredictionMode::Omit},{3,DetPredictionMode::Omit},{99,DetPredictionMode::Omit}};DeterministicRollback rollback(corrected,rc);
    for(unsigned t=0;t<16;++t){if(t==7){witness={"closure-predicted-advance",cp(corrected),{},rows[t+1],{}};const bool moved=rollback.advance();witness.actual=corrected.checkpoint();require(moved,rollback.error());
            witness={"closure-late-member-submit",cp(corrected),{},rows[t+1],frames[t]};const bool submitted=rollback.submit(1,frames[t]);witness.actual=corrected.checkpoint();require(submitted,rollback.error());}
        else {witness={"closure-member-submit",cp(corrected),{},rows[t+1],frames[t]};const bool submitted=rollback.submit(1,frames[t]);witness.actual=corrected.checkpoint();require(submitted,rollback.error());
            witness={"closure-confirmed-advance",cp(corrected),{},rows[t+1],frames[t]};const bool moved=rollback.advance();witness.actual=corrected.checkpoint();require(moved,rollback.error());}
        ++advanceCalls;equal(corrected,rows[t+1],"Actual late left-dash correction whole CP");}
    rollbacks=rollback.rollbackCount();replayedTicks=rollback.replayedTicks();require(rollbacks && replayedTicks,"Late tick7 really restores/replays");
    require(rollback.takeConfirmedEvents()==events && rollback.takeConfirmedEvents().empty(),"Real confirmed events drain exactly once");
    DeterministicSession recording(recipe());require(configure(recording,c),recording.error());DetReplayWriter writer;const auto requested=(output/"control.rpl").string();
    const bool began=writer.begin(recording,requested,2);require(began,writer.error());for(unsigned t=0;t<16;++t){witness={"closure-rpl-record",cp(recording),{},rows[t+1],frames[t]};
        const bool moved=writer.advance(frames[t]);witness.actual=recording.checkpoint();require(moved,writer.error());++advanceCalls;equal(recording,rows[t+1],"Actual sealed recorder whole CP");}
    const bool finished=writer.finish();require(finished,writer.error());const auto physicalPath=writer.path();DetReplayReader reader;
    const bool opened=reader.open(physicalPath);require(opened,reader.error());DeterministicSession playback(recipe());require(configure(playback,c,true),playback.error());
    const bool initialized=reader.restoreInitial(playback);require(initialized,reader.error());equal(playback,rows.front(),"Actual replay initial whole CP");
    while(!reader.atEnd()){const auto t=playback.nextTick();witness={"closure-rpl-playback",cp(playback),{},rows.at(t+1),frames.at(t)};const bool moved=reader.advance(playback);
        witness.actual=playback.checkpoint();require(moved,reader.error());++advanceCalls;equal(playback,rows.at(playback.nextTick()),"Actual replay whole CP");}
    for(auto t:{std::uint64_t{2},std::uint64_t{16}}){witness={"closure-rpl-seek",cp(playback),{},rows.at(t),{}};const bool sought=reader.seek(playback,t);
        witness.actual=playback.checkpoint();require(sought,reader.error());equal(playback,rows.at(t),"Actual indexed seek whole CP");}
    write(output/"initial.state",encodeDetCheckpoint(rows.front()));write(output/"final.state",encodeDetCheckpoint(rows.back()));
    std::ofstream trace(output/"tick-hashes.txt",std::ios::binary);for(const auto& row:rows)trace<<row.nextTick<<' '<<detCheckpointHash(row)<<'\n';trace.close();require(bool(trace),"Write finite tick hash trace");
    Bytes raw;for(const auto& input:frames){const auto encoded=encodeDetInput(input);const auto size=static_cast<std::uint32_t>(encoded.size());
        for(unsigned n=0;n<4;++n)raw.push_back(static_cast<std::uint8_t>(size>>(8*n)));raw.insert(raw.end(),encoded.begin(),encoded.end());}write(output/"canonical-inputs.bin",raw);
    const auto immutable=bytes(restored);const auto schemas=detCharacterControlStateSchemas2D(c);const auto& inputSchema=schemas.at(0);const auto& stateSchema=schemas.at(1);
    for(unsigned n=0;n<8;++n){auto bad=rows[2];const auto field=[&](auto member){return detCharacterControlField2D(c,10,member);};
        switch(n){case 0:writeDetState(inputSchema,bad.globals.at(c.inputSchema),detCharacterControlInputField2D(c,10,DetControlDashLatch),false);break;
        case 1:writeDetState(stateSchema,bad.globals.at(c.stateSchema),DetControlProcessedTick,std::uint64_t{1});break;
        case 2:writeDetState(stateSchema,bad.globals.at(c.stateSchema),field(DetControlEndTick),std::uint64_t{4});break;
        case 3:writeDetState(stateSchema,bad.globals.at(c.stateSchema),field(DetControlReadyTick),std::uint64_t{4});break;
        case 4:writeDetState(stateSchema,bad.globals.at(c.stateSchema),field(DetControlStartCount),std::uint64_t{2});break;
        case 5:writeDetState(inputSchema,bad.globals.at(c.inputSchema),detCharacterControlInputField2D(c,10,DetControlEmittedSpeed),D::fromInt(3));break;
        case 6:writeDetState(stateSchema,bad.actors.at(10).blocks.at(c.stateSchema),field(DetControlRequestCount),std::uint64_t{1});break;
        case 7:bad.globals.at(c.policySchemaBase).at(3)^=1;break;}
        witness={"malformed-control-restore",cp(restored),{},bad,{}};const bool loaded=restored.restore(bad);witness.actual=restored.checkpoint();
        require(!loaded && bytes(restored)==immutable,"Malformed registered control record/output/policy is atomic case="+std::to_string(n));++negativeRestores;}
    auto different=c;different.owners[0].dashSpeed=D::fromInt(3);DeterministicSession foreign(recipe());require(configure(foreign,different),foreign.error());const auto before=bytes(foreign);
    require(!foreign.restore(rows.back()) && bytes(foreign)==before,"Complete dash policy alters manifest even with identical session content identity");++negativeRestores;
    std::cout<<"PASS one16tick reverse/active restore/late-held correction/real effects/RPL2seek closure\n";}
void limits(){const auto c=policy();witness={"invalid-control-config",{},{},{},{}};
    for(unsigned n=0;n<9;++n){auto bad=c;switch(n){case 0:bad.owners.clear();break;case 1:bad.owners[0].dashSpeed=D{};break;
        case 2:bad.owners[0].dashSpeed=D::fromBits(0x7fc00000u);break;case 3:bad.owners[0].dashSpeed=D::fromInt(5);break;
        case 4:bad.owners[0].durationTicks=0;break;case 5:bad.owners[0].cooldownTicks=65536;break;
        case 6:bad.inputSchema=bad.stateSchema;break;case 7:bad.systemId=bad.character.preSystemId;break;case 8:bad.character.collision.characterRespawn=true;break;}
        DeterministicSession session(recipe());require(!installDetCharacterControl2D(session,bad) && !session.faulted(),"Bounded malformed policy rejected before callback use case="+std::to_string(n));++negativeConfigs;}
    auto maximum=c;maximum.owners.clear();for(unsigned n=0;n<8;++n){auto owner=c.owners.front();owner.actor=10*(n+1);maximum.owners.push_back(owner);}
    const auto descriptors=detCharacterControlStateSchemas2D(maximum);require(descriptors.size()==2 && descriptors[0].fields.size()==120 && descriptors[1].fields.size()==121
        && encodeDetCharacterControlConfig2D(maximum).size()<=kDetCharacterControl2DMaxPolicyBytes && detCharacterControlPolicySchemas2D(maximum).size()<=9,"Actual bounded eight-owner descriptors/full policy; not a joint capacity promise");
    maximum.owners.push_back(maximum.owners.back());DeterministicSession ninth(recipe());require(!installDetCharacterControl2D(ninth,maximum),"Ninth control owner refused");++negativeConfigs;
    DeterministicSession wrongOrder(recipe());require(installDetCharacterController2D(wrongOrder,c.character) && installDetCharacterControl2D(wrongOrder,c)
        && wrongOrder.registerSystem(16,c.character.collision.priority,[c](DetTickContext& ctx){detCharacterControlInput2D(ctx,c,10,sample(0,false));return true;})
        && wrongOrder.addEntity(10,actor(c,10,{})) && wrongOrder.addEntity(30,actor(c,30,{})) && wrongOrder.seal(),wrongOrder.error());
    const auto initial=cp(wrongOrder);const auto before=encodeDetCheckpoint(initial);const DetTickInput emptyFrame{0,1,{}};
    witness={"late-actual-input-setter",initial,{},{},emptyFrame};
    const bool moved=wrongOrder.advance(emptyFrame);witness.actual=wrongOrder.checkpoint();require(!moved && wrongOrder.faulted(),"Actual callback after Control cannot reacquire this tick's input");
    require(wrongOrder.restore(initial) && bytes(wrongOrder)==before,"Explicit fault recovery restores initial whole CP");++negativeRestores;
    std::cout<<"PASS bounded policy/descriptors, malformed records and actual setter ordering\n";}
std::string json(const std::string& text){std::string out="\"";for(char ch:text){if(ch=='\"' || ch=='\\')out+='\\';
    if(ch=='\n')out+="\\n";else if(ch=='\r')out+="\\r";else if(ch=='\t')out+="\\t";else if(static_cast<unsigned char>(ch)>=0x20)out+=ch;}return out+'\"';}
void failure(const fs::path& out,const std::string& reason){if(out.empty())return;if(witness.before)write(out/"failure-before.state",encodeDetCheckpoint(*witness.before));
    if(witness.actual)write(out/"failure-actual.state",encodeDetCheckpoint(*witness.actual));if(witness.expected)write(out/"failure-expected.state",encodeDetCheckpoint(*witness.expected));
    if(witness.input)write(out/"failure-canonical-input.bin",encodeDetInput(*witness.input));std::ofstream f(out/"failure.json",std::ios::binary);
    f<<"{\"status\":\"failed\",\"phase\":"<<json(witness.phase)<<",\"reason\":"<<json(reason);if(witness.input)f<<",\"tick\":\""<<witness.input->tick<<'\"';
    if(witness.expected && witness.actual){const auto diff=firstDetDifference(*witness.expected,*witness.actual);if(diff)f<<",\"firstDifference\":{\"section\":"<<json(diff->section)
        <<",\"entity\":\""<<diff->entity<<"\",\"component\":"<<diff->component<<",\"field\":"<<diff->field<<",\"lane\":"<<diff->lane<<",\"expected\":\""<<diff->expected<<"\",\"actual\":\""<<diff->actual<<"\"}";}
    f<<"}\n";f.close();require(bool(f),"Write failure witness");}
fs::path fresh(const fs::path& parent){require(fs::is_directory(parent),"--output requires existing parent");const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned n=0;n<1024;++n){const auto path=parent/("control2d-"+std::to_string(stamp)+"-"+std::to_string(n));if(fs::create_directory(path))return path;}throw std::runtime_error("No fresh artifact directory");}
} // namespace
int main(int argc,char** argv){fs::path output;try{fs::path parent;for(int n=1;n<argc;++n){const std::string key=argv[n];
    if(key=="--output" && n+1<argc && parent.empty())parent=argv[++n];else throw std::invalid_argument("Usage: AYEntity_CharacterControl2DChecks --output EXISTING_PARENT");}
    output=fresh(parent);selected();relocation();closure(output);limits();std::ofstream f(output/"summary.json",std::ios::binary);
    f<<"{\"status\":\"passed\",\"profile\":1,\"closureTicks\":16,\"closureCpRows\":17,\"rplFiles\":1,\"seeks\":2,\"handRows\":\""<<handRows
        <<"\",\"advanceCalls\":\""<<advanceCalls<<"\",\"cpComparisons\":\""<<cpComparisons<<"\",\"negativeRestores\":\""<<negativeRestores<<"\",\"negativeConfigs\":\""<<negativeConfigs
        <<"\",\"confirmedEvents\":\""<<confirmedEvents<<"\",\"rollbacks\":\""<<rollbacks<<"\",\"replayedTicks\":\""<<replayedTicks
        <<"\",\"oracleScope\":\"selected independent finite window/edge/ground/velocity/geometry/relocation checks; whole CP actual execution equality\"}\n";
    f.close();require(bool(f),"Write final summary");std::cout<<"PASS Stage55 finite Control; output="<<output.string()<<'\n';return 0;
}catch(const std::exception& e){try{failure(output,e.what());}catch(const std::exception& logging){std::cerr<<"Failure witness: "<<logging.what()<<'\n';}
    std::cerr<<"FAIL Stage55 Control: "<<e.what()<<'\n';return 1;}}
