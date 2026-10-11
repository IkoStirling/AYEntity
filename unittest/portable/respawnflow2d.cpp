// design reference: AYEntity/design.md registered content respawn routing.
// Hand-calculated short content transitions, then complete execution-equivalence
// closure. Manifest bytes and all checkpoint fields are not independent goldens.
#include <AYEntity/DeterministicCharacterRespawnFlow2D.h>
#include <AYEntity/DeterministicReplay.h>
#include <AYEntity/DeterministicRollback.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using namespace ayt::entity;namespace fs=std::filesystem;
using D=ayt::math::DetFloat32;using V=ayt::math::DetVec2;using Bytes=std::vector<std::uint8_t>;
using Actors=std::vector<std::pair<SimEntityId,DetActorState>>;
void require(bool value,const std::string& why){if(!value)throw std::runtime_error(why);}
D q(int value,int denominator=4){return D::fromInt(value)/D::fromInt(denominator);}
DetCharacterRespawnFlow2DConfig policy(){DetCharacterRespawnFlow2DConfig c;auto& k=c.respawn.character;
    k.advanced=true;k.collision.oneWayPlatforms=true;k.collision.characterRespawn=true;
    k.gravity={};k.acceleration=k.deceleration=D::fromInt(32);k.collision.maxBodies=16;k.collision.maxTriggerPairs=8;
    c.respawn.spawns={{1,10,{{},q(2)}},{2,10,{D::fromInt(4),q(2)}},
        {3,10,{D::fromInt(6),q(2)}},{4,10,{D::fromInt(8),q(2)}}};
    c.bindings={{10,1}};
    // Trigger63's lower region ID must not beat trigger60's earlier real event.
    // Death region100 arrives before checkpoint region200 by region ID, yet all
    // checkpoint selections must finish before the first death request.
    c.regions={{50,63,10,DetRespawnFlowRegionKind2D::Checkpoint,3},
        {100,61,10,DetRespawnFlowRegionKind2D::Death,0},{110,61,10,DetRespawnFlowRegionKind2D::Death,0},
        {200,60,10,DetRespawnFlowRegionKind2D::Checkpoint,2},{210,62,10,DetRespawnFlowRegionKind2D::Checkpoint,3}};
    return c;
}
DetCharacterRespawnFlow2DConfig externalPolicy(){auto c=policy();c.externalDeaths=true;
    c.externalDeathPermissions={{1,10,6},{2,10,6}};return c;}
DetActorState actor(const DetCharacterRespawnFlow2DConfig& c,DetBodyMode2D mode,V position,V half,bool hero=false,bool trigger=false){
    DetActorState a;DetSimTransformComponent pose;require(pose.setPosition({position.x,position.y,{}}),"fixture pose");a.pose=pose.snapshot();
    a.blocks[c.respawn.character.collision.bodySchema]=detCollisionBodyState2D(c.respawn.character.collision,
        {mode,half,{}, {},hero?1u:(trigger?4u:2u),hero?6u:1u,trigger});
    if(hero){DetCharacterState2D state;state.enabled=true;a.blocks[c.respawn.character.stateSchema]=detCharacterState2D(c.respawn.character,state);}return a;
}
Actors scene(const DetCharacterRespawnFlow2DConfig& c){return {
    {10,actor(c,DetBodyMode2D::Kinematic,{{},q(2)},{q(1),q(2)},true)},
    {20,actor(c,DetBodyMode2D::Static,{D::fromInt(4),-q(1)},{D::fromInt(12),q(1)})},
    {30,actor(c,DetBodyMode2D::Static,{D::fromInt(8),q(2)},{q(1),q(2)})},
    {60,actor(c,DetBodyMode2D::Static,{q(2),q(2)},{q(1,8),q(2)},false,true)},
    {61,actor(c,DetBodyMode2D::Static,{q(2),q(2)},{q(1,8),q(2)},false,true)},
    {62,actor(c,DetBodyMode2D::Static,{D::fromInt(4),q(2)},{q(1,8),q(2)},false,true)},
    {63,actor(c,DetBodyMode2D::Static,{q(2),q(2)},{q(1,8),q(2)},false,true)}};}
bool configure(DeterministicSession& s,const DetCharacterRespawnFlow2DConfig& c,Actors actors,bool reverse=false,bool late=false,bool foreign=false){
    auto input=[c](DetTickContext& ctx){for(const auto& command:ctx.input().commands){
        if(command.source!=1 || command.type!=1 || (command.payload.size()!=3 && !(c.externalDeaths && command.payload.size()==4))
            || command.payload[1]>1 || command.payload[2]>1 || (command.payload.size()==4 && command.payload[3]>1))return false;
        const auto entities=ctx.entities();if(std::binary_search(entities.begin(),entities.end(),SimEntityId{10})){
            const int speed=command.payload[0]<128?command.payload[0]:int(command.payload[0])-256;
            detCharacterInput2D(ctx,10,c.respawn.character,{D::fromInt(speed),false,false,false});}
        detCharacterRespawnFlowInput2D(ctx,10,c,bool(command.payload[1]));
        // An explicit release does not revoke an already registered same-tick request.
        detCharacterRespawnFlowInput2D(ctx,10,c,false);
        if(c.externalDeaths){require(ctx.systemId()==6 && command.source==1,"actual callback identity differs from canonical command source");
            if(command.payload.size()==4 && command.payload[3]){detCharacterRespawnFlowDeath2D(ctx,c,1);
                detCharacterRespawnFlowDeath2D(ctx,c,2);detCharacterRespawnFlowDeath2D(ctx,c,1);}}
        if(command.payload[2])for(auto id:ctx.entities())ctx.despawn(id);
    }return true;};
    if(foreign && !s.registerSystem(5,0,[c](DetTickContext& ctx){if(!ctx.tick())
        ctx.emit(c.respawn.character.collision.eventType,encodeDetTriggerEvent2D({DetTriggerPhase2D::Enter,{10,63}}));return true;}))return false;
    if(late && !s.registerSystem(8,0,[c](DetTickContext& ctx){detCharacterRespawnFlowInput2D(ctx,10,c,true);return true;}))return false;
    if(reverse){if(!installDetCharacterRespawn2D(s,c.respawn) || !installDetCharacterRespawnFlow2D(s,c) || !s.registerSystem(6,0,input))return false;
        std::reverse(actors.begin(),actors.end());}
    else if(!s.registerSystem(6,0,input) || !installDetCharacterRespawn2D(s,c.respawn) || !installDetCharacterRespawnFlow2D(s,c))return false;
    for(auto& [id,value]:actors)if(!s.addEntity(id,std::move(value)))return false;return s.seal();
}
DetTickInput input(std::uint64_t tick,int speed=0,bool manual=false,bool retire=false,bool external=false){
    DetTickInput result{tick,1,{{1,0,1,{std::uint8_t(speed),std::uint8_t(manual),std::uint8_t(retire)}}}};
    if(external)result.commands.front().payload.push_back(1);return result;
}
DetSessionCheckpoint checkpoint(DeterministicSession& s){const auto cp=s.checkpoint();require(bool(cp),s.error());return *cp;}
Bytes bytes(DeterministicSession& s){return encodeDetCheckpoint(checkpoint(s));}
void advance(DeterministicSession& s,int speed=0,bool manual=false,bool retire=false,bool external=false){require(s.advance(input(s.nextTick(),speed,manual,retire,external)),s.error());}
V position(const DetSessionCheckpoint& cp){const auto& p=cp.actors.at(10).pose.position;return {D::fromBits(p[0]),D::fromBits(p[1])};}
auto state(DeterministicSession& s,const DetCharacterRespawnFlow2DConfig& c){return detCharacterRespawnFlowState2D(checkpoint(s),10,c);}
void directed(){const auto c=policy();
    {DeterministicSession s({46,1,1,4,46});require(configure(s,c,scene(c)),s.error());advance(s,2);
        require(position(checkpoint(s))==V{q(2),q(2)} && state(s,c).requestCount==0,"real motion emits events, no same-tick routing");
        advance(s,0,true);auto r=state(s,c);const auto cp=checkpoint(s);const auto resolved=detCharacterRespawnState2D(cp,10,c.respawn);
        require(r.activeSpawn==2 && r.checkpointCount==1 && r.lastCheckpointRegion==200 && r.lastCheckpointTick==1
            && r.deathCount==1 && r.lastDeathRegion==100 && r.lastDeathTick==1 && r.manualCount==0 && r.requestCount==1
            && r.lastRequestKind==DetRespawnFlowRequestKind2D::Death && r.lastRequestSpawn==2
            && r.lastRequestCode==DetRespawnRequestCode2D::Queued,"all checkpoints before one death; death consumes simultaneous manual");
        require(position(cp)==V{D::fromInt(4),q(2)} && resolved.result==DetRespawnResult2D::Accepted && resolved.resultTick==2,"real collision accepts new checkpoint");
        advance(s,0,true);r=state(s,c);
        require(r.activeSpawn==3 && r.checkpointCount==2 && r.lastCheckpointRegion==210 && r.lastCheckpointTick==2
            && r.manualCount==1 && r.deathCount==1 && r.requestCount==2 && r.lastRequestKind==DetRespawnFlowRequestKind2D::Manual
            && position(checkpoint(s))==V{D::fromInt(6),q(2)},"real endpoint Enter selects new checkpoint before manual request");
        advance(s);require(state(s,c).requestCount==2,"Stay/release do not repeat a death or manual request");}
    {auto m=c;m.regions.clear();m.bindings[0].spawn=4;DeterministicSession s({46,1,1,4,46});require(configure(s,m,scene(m)),s.error());advance(s,0,true);
        require(state(s,m).requestCount==1 && detCharacterRespawnState2D(checkpoint(s),10,m.respawn).result==DetRespawnResult2D::Occupied
            && position(checkpoint(s))==V{{},q(2)},"manual-only occupied destination is a normal resolved denial");
        advance(s);require(state(s,m).requestCount==1,"no implicit retry after Occupied");}
    {DeterministicSession s({46,1,1,4,46});require(configure(s,c,scene(c),false,false,true),s.error());advance(s);advance(s);
        require(state(s,c).activeSpawn==1 && state(s,c).checkpointCount==0 && state(s,c).requestCount==0,"valid payload from other producer cannot grant checkpoint permission");}
    {DeterministicSession s({46,1,1,4,46});require(configure(s,c,scene(c),false,true),s.error());require(!s.advance(input(0)) && s.faulted(),"manual setter after Flow faults instead of hidden next-tick work");}
    {DeterministicSession s({46,1,1,4,46});require(configure(s,c,scene(c)),s.error());advance(s,2,false,true);
        require(checkpoint(s).actors.empty(),"actual deferred retirement removes all handles");advance(s,0,true);const auto r=state(s,c);
        require(r.activeSpawn==2 && r.deathCount==1 && r.requestCount==1 && r.lastRequestCode==DetRespawnRequestCode2D::Unavailable
            && checkpoint(s).actors.empty(),"retired real Enter retains permissions but returns Unavailable, including empty world");advance(s);
        require(state(s,c).requestCount==1,"retired permission has no hidden retry");}
    std::cout<<"PASS real Enter checkpoint/death/manual ordering, endpoint re-entry, source filtering, normal denial and retired identity\n";
}
void contracts(){const auto c=policy();
    for(unsigned kind=0;kind<5;++kind){auto bad=c;if(kind==0)bad.regions[0].spawn=9;if(kind==1)bad.regions[1].spawn=1;
        if(kind==2)bad.systemId=20;if(kind==3)bad.stateSchema=c.respawn.stateSchema;if(kind==4)bad.bindings.push_back(bad.bindings.front());
        DeterministicSession s;require(!installDetCharacterRespawnFlow2D(s,bad),"invalid routing rejected before registration");
        require(installDetCharacterRespawn2D(s,c.respawn) && installDetCharacterRespawnFlow2D(s,c),"invalid preflight leaves fresh namespace available");}
    {auto missing=scene(c);missing.erase(missing.begin()+3);DeterministicSession s({46,1,1,4,46});require(!configure(s,c,std::move(missing)),"unknown initial trigger permission fails seal");}
    DeterministicSession s({46,1,1,4,46});require(configure(s,c,scene(c)),s.error());advance(s,2);advance(s);const auto cp=checkpoint(s);const auto before=bytes(s);
    const auto schema=detCharacterRespawnFlowStateSchema2D(c);
    for(unsigned kind=0;kind<7;++kind){auto bad=cp;auto& words=bad.globals.at(c.stateSchema);
        if(kind==0)writeDetState(schema,words,detCharacterRespawnFlowField2D(c,10,DetRespawnFlowManualRequested),true);
        if(kind==1)writeDetState(schema,words,detCharacterRespawnFlowField2D(c,10,DetRespawnFlowProcessedTick),std::uint64_t{});
        if(kind==2)writeDetState(schema,words,detCharacterRespawnFlowField2D(c,10,DetRespawnFlowActiveSpawn),std::uint32_t{});
        if(kind==3)writeDetState(schema,words,detCharacterRespawnFlowField2D(c,10,DetRespawnFlowLastCheckpointRegion),std::uint32_t{100});
        if(kind==4)writeDetState(schema,words,detCharacterRespawnFlowField2D(c,10,DetRespawnFlowDeathCount),UINT64_MAX);
        if(kind==5)writeDetState(schema,words,detCharacterRespawnFlowField2D(c,10,DetRespawnFlowLastRequestCode),std::uint32_t{2});
        if(kind==6)writeDetState(schema,bad.actors.at(10).blocks.at(c.stateSchema),detCharacterRespawnFlowField2D(c,10,DetRespawnFlowManualRequested),true);
        require(!s.restore(bad) && bytes(s)==before,"malformed authority/manual/counter/stamp/actor-copy restore is atomic");}
    auto other=c;other.respawn.spawns[1].position.x=D::fromInt(5);bool threw=false;
    try{(void)detCharacterRespawnFlowState2D(cp,10,other);}catch(const std::exception&){threw=true;}require(threw,"getter binds full nested Respawn bytes, not only IDs or hash");
    auto maximum=c;maximum.bindings.clear();maximum.regions.clear();maximum.respawn.spawns.clear();
    for(unsigned n=0;n<8;++n){maximum.respawn.spawns.push_back({n+1,10+n,{D::fromInt(n),q(2)}});maximum.bindings.push_back({10+n,n+1});}
    for(unsigned n=0;n<16;++n)maximum.regions.push_back({n+1,100+n,10+n%8,DetRespawnFlowRegionKind2D::Checkpoint,n%8+1});
    const auto layout=detCharacterRespawnFlowStateSchema2D(maximum);const auto policies=detCharacterRespawnFlowPolicySchemas2D(maximum);
    require(layout.fields.size()==121 && policies.size()<=5 && encodeDetCharacterRespawnFlowConfig2D(maximum).size()<=4096,"explicit maximum record/policy bounds");
    std::cout<<"PASS explicit registration/full policy and atomic mutable-global/manual/permission restore boundaries\n";
}
void externalDirected(){const auto c=externalPolicy();
    {DeterministicSession s({48,1,1,4,48});require(configure(s,c,scene(c)),s.error());advance(s,2);advance(s,0,true,false,true);auto r=state(s,c);
        require(r.activeSpawn==2 && r.lastRequestKind==DetRespawnFlowRequestKind2D::Death && r.lastDeathRegion==100
            && r.deathCount==1 && r.externalDeathCount==0 && r.manualCount==0,"real region death consumes simultaneous external/manual after checkpoint");
        advance(s,0,true,false,true);r=state(s,c);const auto cp=checkpoint(s);const auto bottom=detCharacterRespawnState2D(cp,10,c.respawn);
        require(r.activeSpawn==3 && r.checkpointCount==2 && r.deathCount==2 && r.externalDeathCount==1 && r.lastExternalDeathPermission==1
            && r.lastExternalDeathTick==2 && r.lastDeathRegion==0 && r.lastRequestKind==DetRespawnFlowRequestKind2D::ExternalDeath
            && r.manualCount==0 && r.requestCount==2 && bottom.result==DetRespawnResult2D::Accepted && bottom.resultTick==3
            && position(cp)==V{D::fromInt(6),q(2)},"first external permission wins and uses new endpoint checkpoint before manual");
        const auto schemas=detCharacterRespawnFlowStateSchemas2D(c);require(schemas.size()==2 && schemas[0].version==2 && schemas[1].version==2,"explicit profile2 mutable descriptors");
        const auto& external=schemas[1];require(!readDetState<bool>(external,cp.globals.at(c.externalStateSchema),detCharacterRespawnFlowExternalField2D(c,10,DetRespawnFlowExternalRequested))
            && !readDetState<std::uint32_t>(external,cp.globals.at(c.externalStateSchema),detCharacterRespawnFlowExternalField2D(c,10,DetRespawnFlowExternalPendingPermission)),"pending clears before checkpoint");
        const auto before=bytes(s);for(unsigned n=0;n<7;++n){auto bad=cp;auto& words=bad.globals.at(c.externalStateSchema);
            if(n==0)writeDetState(external,words,detCharacterRespawnFlowExternalField2D(c,10,DetRespawnFlowExternalRequested),true);
            if(n==1)writeDetState(external,words,detCharacterRespawnFlowExternalField2D(c,10,DetRespawnFlowExternalPendingPermission),std::uint32_t{1});
            if(n==2)writeDetState(external,words,detCharacterRespawnFlowExternalField2D(c,10,DetRespawnFlowExternalCount),std::uint64_t{3});
            if(n==3)writeDetState(external,words,detCharacterRespawnFlowExternalField2D(c,10,DetRespawnFlowExternalLastPermission),std::uint32_t{99});
            if(n==4)writeDetState(external,words,detCharacterRespawnFlowExternalField2D(c,10,DetRespawnFlowExternalLastTick),std::uint64_t{2});
            if(n==5)writeDetState(schemas[0],bad.globals.at(c.stateSchema),detCharacterRespawnFlowField2D(c,10,DetRespawnFlowLastRequestKind),std::uint32_t{1});
            if(n==6)writeDetState(external,bad.actors.at(10).blocks.at(c.externalStateSchema),detCharacterRespawnFlowExternalField2D(c,10,DetRespawnFlowExternalCount),std::uint64_t{1});
            require(!s.restore(bad) && bytes(s)==before,"external pending/provenance/count/kind/actor-copy restore rejection atomic");}
        advance(s);require(state(s,c).requestCount==2,"external flag has no cross-tick retry");}
    {auto m=c;m.regions.clear();m.bindings[0].spawn=4;DeterministicSession s({48,1,1,4,48});require(configure(s,m,scene(m)),s.error());advance(s,0,false,false,true);
        require(state(s,m).externalDeathCount==1 && detCharacterRespawnState2D(checkpoint(s),10,m.respawn).result==DetRespawnResult2D::Occupied
            && position(checkpoint(s))==V{{},q(2)},"occupied external death keeps baseline movement and distinct attempted kind");advance(s);
        require(state(s,m).requestCount==1,"occupied external request never auto retries");}
    {DeterministicSession s({48,1,1,4,48});require(s.registerSystem(5,0,[c](DetTickContext& ctx){detCharacterRespawnFlowDeath2D(ctx,c,1);return true;}),"foreign source registration");
        require(configure(s,c,scene(c)),s.error());require(!s.advance(input(0)) && s.faulted(),"actual unauthorized system cannot spoof input/source permission");}
    {DeterministicSession s({48,1,1,4,48});require(s.registerSystem(8,0,[c](DetTickContext& ctx){detCharacterRespawnFlowDeath2D(ctx,c,1);return true;}),"post-Flow source registration");
        require(configure(s,c,scene(c)),s.error());require(!s.advance(input(0)) && s.faulted(),"later callback cannot register external hidden work");}
    {auto wrong=c;wrong.externalDeathPermissions.front().producerSystem=5;DeterministicSession s({48,1,1,4,48});
        require(!configure(s,wrong,scene(wrong)),"noninstalled declared producer identity fails seal");}
    for(unsigned n=0;n<4;++n){auto bad=c;if(n==0)bad.externalDeaths=false;if(n==1)bad.externalStateSchema=144;
        if(n==2)bad.externalDeathPermissions[0].producerSystem=7;if(n==3)bad.externalDeathPermissions[1].id=1;DeterministicSession s;
        require(!installDetCharacterRespawnFlow2D(s,bad),"new profile/optional schema/source/duplicate preflight rejects before installation");}
    {auto max=c;max.bindings.clear();max.respawn.spawns.clear();max.regions.clear();max.externalDeathPermissions.clear();
        for(unsigned n=0;n<8;++n){max.respawn.spawns.push_back({n+1,10+n,{D::fromInt(n),q(2)}});max.bindings.push_back({10+n,n+1});}
        for(unsigned n=0;n<16;++n){max.regions.push_back({n+1,100+n,10+n%8,DetRespawnFlowRegionKind2D::Checkpoint,n%8+1});max.externalDeathPermissions.push_back({n+1,10+n%8,6});}
        const auto descriptors=detCharacterRespawnFlowStateSchemas2D(max);require(descriptors.size()==2 && descriptors[0].fields.size()==121
            && descriptors[1].fields.size()==41 && encodeDetCharacterRespawnFlowConfig2D(max).size()<=4096,"eight bindings fit separate bounded external state");}
    std::cout<<"PASS explicit external producer, region/external/manual arbitration, zero pending, normal denial and atomic restore\n";
}
DetTickInput closureInput(std::uint64_t tick){return input(tick,tick==0?2:0,tick==2 || tick==6);}
void closure(const fs::path& directory,bool external=false){const auto c=external?externalPolicy():policy();constexpr std::uint64_t ticks=12;
    const auto recipeInput=[external](std::uint64_t tick){return external?input(tick,tick==0?2:0,tick==1 || tick==2,false,tick==1 || tick==2 || tick==6):closureInput(tick);};
    std::vector<Bytes> boundaries;std::vector<DetSessionCheckpoint> copies;std::vector<DetConfirmedEvent> expectedEvents;
    DeterministicSession a({46,1,1,4,46}),b({46,1,1,4,46});require(configure(a,c,scene(c)) && configure(b,c,scene(c),true),"closure registration");
    boundaries.push_back(bytes(a));copies.push_back(checkpoint(a));
    for(std::uint64_t tick=0;tick<ticks;++tick){require(a.advance(recipeInput(tick)) && b.advance(recipeInput(tick)),"short actual closure ticks");
        require(bytes(a)==bytes(b),"complete checkpoint bytes reverse registration");boundaries.push_back(bytes(a));copies.push_back(checkpoint(a));
        for(const auto& event:copies.back().pendingEvents)expectedEvents.push_back({1,tick,event});}
    require(a.restore(copies[2]) && bytes(a)==boundaries[2],"restore actual death/checkpoint accepted boundary");
    for(std::uint64_t tick=2;tick<ticks;++tick){require(a.advance(recipeInput(tick)),a.error());require(bytes(a)==boundaries[tick+1],"full restored continuation");}
    DeterministicSession late({46,1,1,4,46});require(configure(late,c,scene(c)),late.error());DetRollbackConfig rc;rc.members={1};rc.historyTicks=32;rc.maxPredictionTicks=4;
    // This command contains a one-shot manual request, so guesses omit it rather
    // than inventing repeated presses by holding the last complete payload.
    rc.prediction={{1,DetPredictionMode::Omit}};
    DeterministicRollback owner(late,rc);std::vector<DetConfirmedEvent> actualEvents;
    for(std::uint64_t tick=0;tick<ticks;++tick){if(!tick)require(owner.submit(1,recipeInput(0)),owner.error());if(tick>=2)require(owner.submit(1,recipeInput(tick-2)),owner.error());require(owner.advance(),owner.error());
        const auto confirmed=owner.checkpointAt(owner.confirmedNextTick());require(confirmed && encodeDetCheckpoint(*confirmed)==boundaries[owner.confirmedNextTick()],"late input corrected full boundary");
        const auto drained=owner.takeConfirmedEvents();actualEvents.insert(actualEvents.end(),drained.begin(),drained.end());}
    for(auto tick=ticks-2;tick<ticks;++tick)require(owner.submit(1,recipeInput(tick)),owner.error());const auto drained=owner.takeConfirmedEvents();actualEvents.insert(actualEvents.end(),drained.begin(),drained.end());
    require(owner.rollbackCount()>0 && owner.replayedTicks()>0 && bytes(late)==boundaries.back() && actualEvents==expectedEvents,"actual late rollback and once-only trigger release");
    DeterministicSession recording({46,1,1,4,46});require(configure(recording,c,scene(c)),recording.error());DetReplayWriter writer;
    require(writer.begin(recording,(directory/(external?"respawnflow-external":"respawnflow")).string(),2),writer.error());
    for(std::uint64_t tick=0;tick<ticks;++tick){require(writer.advance(recipeInput(tick)),writer.error());require(bytes(recording)==boundaries[tick+1],"real recording full boundary");}require(writer.finish(),writer.error());
    DeterministicSession playback({46,1,1,4,46});require(configure(playback,c,scene(c),true),playback.error());DetReplayReader reader;require(reader.open(writer.path()) && reader.restoreInitial(playback),reader.error());
    for(std::uint64_t target:{0u,2u,3u}){require(reader.seek(playback,target) && bytes(playback)==boundaries[target],"real seek initial/death/manual boundary");
        while(!reader.atEnd()){require(reader.advance(playback),reader.error());require(bytes(playback)==boundaries[playback.nextTick()],"actual replay complete continuation");}}
    // Captured from the actual pre-Stage48 Flow1 gate, not an authenticity claim.
    // New context.systemId() adds no manifest/state/wire bytes.
    if(!external)require(detCheckpointHash(copies.back())==UINT64_C(9151289997190477091),"published Flow1 default checkpoint hash unchanged");
    const auto r=detCharacterRespawnFlowState2D(copies.back(),10,c);std::ostringstream summary;
    summary<<"{\"status\":\"passed\",\"ticks\":12,\"checkpointRows\":13,\"confirmedEvents\":"<<expectedEvents.size()
        <<",\"checkpointSelections\":"<<r.checkpointCount<<",\"deathRequests\":"<<r.deathCount<<",\"manualRequests\":"<<r.manualCount
        <<",\"requestCount\":"<<r.requestCount<<",\"rollbacks\":"<<owner.rollbackCount()<<",\"replayedTicks\":"<<owner.replayedTicks()
        <<",\"replayFiles\":1,\"seeks\":3,\"flowProfile\":"<<(external?2:1)<<",\"respawnProfile\":1,\"controllerProfile\":8,\"collisionProfile\":9";
    if(external)summary<<",\"externalDeathRequests\":"<<r.externalDeathCount;
    summary<<",\"finalHash\":\""<<detCheckpointHash(copies.back())<<"\"}\n";
    std::ofstream out(directory/(external?"summary-external.json":"summary.json"),std::ios::binary);out<<summary.str();out.close();require(!out.fail(),"summary close");std::cout<<summary.str();
}
} // namespace
int main(int argc,char** argv){try{fs::path parent=fs::temp_directory_path();if(argc==3 && std::string(argv[1])=="--output")parent=argv[2];
    else if(argc!=1)throw std::invalid_argument("usage: [--output EXISTING_PARENT_DIRECTORY]");require(fs::is_directory(parent),"existing output parent required");
    const auto start=std::chrono::steady_clock::now().time_since_epoch().count();fs::path directory;
    for(unsigned n=0;n<1024;++n){auto p=parent/("respawnflow-"+std::to_string(start)+"-"+std::to_string(n));if(fs::create_directory(p)){directory=p;break;}}
    require(!directory.empty(),"fresh output directory");directed();contracts();externalDirected();closure(directory);closure(directory,true);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
