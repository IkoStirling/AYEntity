#pragma once
// Bounded, seed-reproducible engine test scenario. Only registered Sim fields,
// pose, RNG and events are mutable authority; captured policy is immutable.
#include "DetTypedSessionScenario.h"
#include <AYEntity/DeterministicRollback.h>
#include <deque>
#include <stdexcept>

namespace detsimulation_campaign {
using namespace ayt::entity;
using D=ayt::math::DetFloat32;
using V=ayt::math::DetVec3;
using V2=ayt::math::DetVec2;
inline std::uint64_t mix(std::uint64_t value){
    value+=0x9e3779b97f4a7c15ull;value=(value^(value>>30))*0xbf58476d1ce4e5b9ull;
    value=(value^(value>>27))*0x94d049bb133111ebull;return value^(value>>31);
}
struct Policy {
    std::uint64_t seed=0;
    std::uint32_t numerator=1,denominator=64,period=31,history=24,delay=4;
    bool collision=true;
};
inline Policy policy(std::uint64_t seed){
    Policy result;result.seed=seed;const auto bits=mix(seed);
    const std::pair<std::uint32_t,std::uint32_t> steps[]={{1,64},{3,128},{1,60},{7,256}};
    const auto step=steps[bits%4];result.numerator=step.first;result.denominator=step.second;
    result.period=17+static_cast<std::uint32_t>((bits>>4)%45);
    result.history=16+static_cast<std::uint32_t>((bits>>12)%33);
    result.delay=2+static_cast<std::uint32_t>((bits>>20)%5);result.collision=(bits>>32)%2!=0;return result;
}
inline DetTickInput input(const Policy& p,std::uint64_t tick,bool reversed=false){
    DetTickInput result{tick,1,{}};
    for(std::uint32_t member:{1u,2u}){
        const auto bits=mix(p.seed^mix(tick)^mix(member));
        std::vector<std::uint8_t> bytes(1+static_cast<std::size_t>((bits>>8)%9));
        for(std::size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<std::uint8_t>(mix(bits+i));
        result.commands.push_back({member,0,1,std::move(bytes)});
        result.commands.push_back({member,1,2,{static_cast<std::uint8_t>(bits>>24)}});
        if(bits%5==0)result.commands.push_back({member,2,3,{static_cast<std::uint8_t>(bits>>40)}});
    }
    if(reversed)std::reverse(result.commands.begin(),result.commands.end());return result;
}
inline bool configure(DeterministicSession& session,const Policy& p,bool reversed){
    const auto actor=dettyped_scenario::actorSchema();
    const DetTypedStateSchema global{3,1,{{1,std::uint64_t{0}},{2,std::uint64_t{0}},{3,std::uint64_t{0}},
        {4,DetEntityRef{}},{5,std::uint64_t{0}}}};
    if(reversed){if(!session.registerTypedSchema(global) || !session.registerTypedSchema(actor))return false;}
    else if(!session.registerTypedSchema(actor) || !session.registerTypedSchema(global))return false;
    if(!session.registerGlobalState(3) || !session.registerLogicProfile(90,1,0x53494d43414d5031ull))return false;
    if(reversed){if(!session.registerRandomStream(8,mix(p.seed+8)) || !session.registerRandomStream(7,mix(p.seed+7)))return false;}
    else if(!session.registerRandomStream(7,mix(p.seed+7)) || !session.registerRandomStream(8,mix(p.seed+8)))return false;
    auto sample=[p](DetTickContext& c){
        c.writeGlobal(3,1,c.readGlobal<std::uint64_t>(3,1)+1);auto hash=c.readGlobal<std::uint64_t>(3,2);
        for(const auto& command:c.input().commands){hash=hash*33+command.source;hash=hash*33+command.sequence;hash=hash*33+command.type;
            for(auto byte:command.payload)hash=hash*33+byte;}
        for(const auto& event:c.events())for(auto byte:event.payload)hash=hash*33+byte;
        c.writeGlobal(3,2,hash);c.writeGlobal(3,3,c.readGlobal<std::uint64_t>(3,3)+c.events().size());
        const std::array<std::uint8_t,2> payload{static_cast<std::uint8_t>(c.tick()),static_cast<std::uint8_t>(hash)};c.emit(100,payload);
        if(c.tick()%p.period==0){
            const bool spawn=!c.input().commands.empty() && !c.input().commands.front().payload.empty()
                && (c.input().commands.front().payload.front()&1);
            const auto id=spawn?1000+c.tick():0;
            if(id){c.spawn(id);c.writeGlobal(3,5,c.readGlobal<std::uint64_t>(3,5)+1);}
            c.writeGlobal(3,4,DetEntityRef{id});
        }
        if(c.tick()%p.period==1){const auto id=c.readGlobal<DetEntityRef>(3,4).value;
            if(id)c.despawn(id);c.writeGlobal(3,4,DetEntityRef{});}
        return true;
    };
    auto movement=[p](DetTickContext& c){
        for(auto id:c.entities()){
            if(p.collision && (id==200 || id==201))continue;
            c.write(id,2,10,c.read<std::uint64_t>(id,2,10)+ayt::math::random_uint(c.random(7),100));
            c.write(id,2,20,c.read<std::uint64_t>(id,2,20)+1);
            const V velocity{c.read<D>(id,2,30),D::fromInt(ayt::math::random_int(c.random(8),-2,2)),{}};
            c.write(id,2,40,velocity);auto& pose=c.pose(id);if(!pose.translate(velocity*c.dt()))return false;
            if(pose.rotationEnabled && !pose.integrateAngularVelocityWorld(V::fromInts(0,2,0),c.dt()))return false;
            c.write(id,2,50,ayt::math::DetQuaternion::fromBits(pose.snapshot().rotation));
            c.write(id,2,60,c.tick()%2==0);c.write(id,2,70,DetEntityRef{2});
            c.write(id,2,80,V2{velocity.x,velocity.y});c.write(id,2,90,static_cast<std::int32_t>(c.tick()));
        }
        if(c.tick()%7==0)c.emit(101);return true;
    };
    DetCollision2DConfig collision;collision.extended=true;collision.maxBodies=4;collision.maxTriggerPairs=1;
    auto reset=[collision](DetTickContext& c){
        const int sign=c.input().commands.empty() || c.input().commands.front().payload.empty()
            || !(c.input().commands.front().payload.front()&1)?1:-1;
        c.write(200,collision.bodySchema,DetBodyVelocity,V2::fromInts(1024*sign,0));
        return c.pose(200).setPosition(V::fromInts(-5*sign,40,0));
    };
    if(reversed){
        if(p.collision && (!installDetCollision2D(session,collision) || !session.registerSystem(25,0,reset)))return false;
        if(!session.registerSystem(20,0,movement) || !session.registerSystem(10,0,sample))return false;
    }else{
        if(!session.registerSystem(10,0,sample) || !session.registerSystem(20,0,movement))return false;
        if(p.collision && (!session.registerSystem(25,0,reset) || !installDetCollision2D(session,collision)))return false;
    }
    std::vector<std::pair<SimEntityId,DetActorState>> actors;
    DetActorState normal;DetSimTransformComponent pose;(void)pose.setRotation({});normal.pose=pose.snapshot();
    actors.push_back({2,normal});actors.push_back({100,normal});
    if(p.collision){
        DetActorState moving,trigger;(void)pose.setPosition(V::fromInts(-5,40,0));moving.pose=pose.snapshot();
        (void)pose.setPosition(V::fromInts(0,40,0));trigger.pose=pose.snapshot();
        moving.blocks[collision.bodySchema]=detCollisionBodyState2D(collision,{DetBodyMode2D::Kinematic,V2::fromInts(1,1)});
        DetCollisionBody2D body;body.mode=DetBodyMode2D::Static;body.half=V2::fromInts(1,1);body.trigger=true;
        trigger.blocks[collision.bodySchema]=detCollisionBodyState2D(collision,body);
        actors.push_back({200,moving});actors.push_back({201,trigger});
    }
    if(reversed)std::reverse(actors.begin(),actors.end());for(auto& [id,state]:actors)if(!session.addEntity(id,state))return false;
    return session.seal();
}
struct CaseResult {
    std::uint64_t seed=0,ticks=0,checkpointsChecked=0,eventsChecked=0,restoredTicks=0,rollbacks=0,replayedTicks=0,duplicates=0;
    std::uint64_t finalHash=0;Policy parameters;
};
struct CaseFailure : std::runtime_error {
    std::uint64_t seed=0,tick=0;std::uint32_t requestedTicks=0;std::string lane;
    std::optional<DetStateDifference> difference;
    std::vector<std::uint8_t> before,expected,actual,failingInput;
    CaseFailure(std::uint64_t s,std::uint64_t t,std::uint32_t n,std::string l,std::string text)
        :std::runtime_error(std::move(text)),seed(s),tick(t),requestedTicks(n),lane(std::move(l)){}
};
inline CaseResult runCase(std::uint64_t seed,std::uint32_t requestedTicks=0){
    const auto p=policy(seed);const auto count=requestedTicks?requestedTicks:256+static_cast<std::uint32_t>(mix(seed+1)%257);
    CaseResult result;result.seed=seed;result.ticks=count;result.parameters=p;
    std::uint64_t current=0;std::string lane="configuration";
    auto fail=[&](const std::string& text)->void{throw CaseFailure(seed,current,count,lane,text);};
    auto require=[&](bool condition,const std::string& text){if(!condition)fail(text);};
    try{
        const DetSessionConfig config{1,1,p.numerator,p.denominator,seed};
        DeterministicSession baseline(config),corrected(config),restored(config);
        require(configure(baseline,p,false) && configure(corrected,p,true) && configure(restored,p,(seed&1)!=0),"scenario configuration");
        DetRollbackConfig bounds;bounds.members={2,1};bounds.historyTicks=p.history;bounds.maxPredictionTicks=p.delay+2;
        bounds.futureTicks=8;bounds.maxBufferedBytes=16*1024*1024;
        bounds.prediction={{1,DetPredictionMode::Hold},{2,DetPredictionMode::Zero},{3,DetPredictionMode::Omit}};
        DeterministicRollback owner(corrected,bounds);
        std::map<std::uint64_t,DetSessionCheckpoint> expected;
        expected.emplace(0,*baseline.checkpoint());
        std::vector<DetConfirmedEvent> expectedEvents,actualEvents;
        struct Arrival {std::uint64_t due,order;std::uint32_t member;DetTickInput contribution;};
        std::vector<Arrival> arrivals;
        std::uint64_t checked=0;
        auto compare=[&](const DetSessionCheckpoint& reference,const DetSessionCheckpoint& observed,const std::string& text){
            if(encodeDetCheckpoint(reference)!=encodeDetCheckpoint(observed)){
                CaseFailure error(seed,current,count,lane,text);error.difference=firstDetDifference(reference,observed);
                error.expected=encodeDetCheckpoint(reference);error.actual=encodeDetCheckpoint(observed);
                const auto tick=reference.nextTick?reference.nextTick-1:0;
                if(expected.contains(tick))error.before=encodeDetCheckpoint(expected.at(tick));
                error.failingInput=encodeDetInput(input(p,tick));throw error;
            }
            ++result.checkpointsChecked;
        };
        auto confirm=[&]{
            lane="confirmed-checkpoints";
            while(checked<owner.confirmedNextTick()){
                ++checked;const auto actual=owner.checkpointAt(checked);require(actual.has_value(),"confirmed checkpoint evicted before inspection");
                compare(expected.at(checked),*actual,"first corrected confirmed state mismatch");
            }
            lane="confirmed-events";auto batch=owner.takeConfirmedEvents();
            actualEvents.insert(actualEvents.end(),batch.begin(),batch.end());
            require(actualEvents.size()<=expectedEvents.size(),"unexpected or repeated confirmed effect");
            for(std::size_t i=actualEvents.size()-batch.size();i<actualEvents.size();++i)
                require(actualEvents[i]==expectedEvents[i],"first confirmed effect identity/payload mismatch at index "+std::to_string(i));
            result.eventsChecked+=batch.size();require(owner.takeConfirmedEvents().empty(),"drained effects delivered twice");
        };
        auto ingress=[&](std::uint64_t clock){
            lane="late-input";
            std::sort(arrivals.begin(),arrivals.end(),[](const auto& a,const auto& b){return a.order<b.order;});
            for(auto it=arrivals.begin();it!=arrivals.end();){
                if(it->due>clock){++it;continue;}
                require(owner.submit(it->member,it->contribution),owner.error());
                // Short cases can legitimately have no random duplicate. The
                // bootstrap member1 frame provides explicit per-case coverage;
                // remaining duplicates still follow the seeded arrival schedule.
                if(it->order%3==0 || (it->contribution.tick==0 && it->member==1)){
                    auto duplicate=it->contribution;std::reverse(duplicate.commands.begin(),duplicate.commands.end());
                    require(owner.submit(it->member,std::move(duplicate)),owner.error());++result.duplicates;}
                it=arrivals.erase(it);confirm();lane="late-input";
            }
        };
        for(std::uint64_t tick=0;tick<count;++tick){
            current=tick;lane="baseline";require(baseline.advance(input(p,tick,(tick&1)!=0)),baseline.error());
            const auto boundary=*baseline.checkpoint();expected.emplace(tick+1,boundary);
            for(const auto& event:boundary.pendingEvents)expectedEvents.push_back({1,tick,event});
            lane="restore-lane";require(restored.advance(input(p,tick,(tick&1)==0)),restored.error());
            compare(boundary,*restored.checkpoint(),"independent registration-order lane mismatch");
            const auto bits=mix(seed^mix(tick+3));
            if(tick>8 && bits%17==0){
                const auto depth=1+bits%8,start=tick+1-depth;
                require(restored.restore(expected.at(start)),restored.error());
                for(auto t=start;t<=tick;++t){current=t;require(restored.advance(input(p,t,true)),restored.error());
                    compare(expected.at(t+1),*restored.checkpoint(),"restored/replayed full state mismatch");++result.restoredTicks;}
                current=tick;
            }
            const auto full=input(p,tick);
            for(std::uint32_t member:{1u,2u}){
                auto contribution=full;std::erase_if(contribution.commands,[member](const auto& c){return c.source!=member;});
                const auto order=mix(seed^mix(tick)^mix(member+9));
                arrivals.push_back({tick?tick+order%(p.delay+1):0,order,member,std::move(contribution)});
            }
            // A second, identical future contribution exercises future buffering
            // and duplicates without allowing it to seed predictions early.
            if(tick+2<count && bits%11==0){auto future=input(p,tick+2,true);
                const auto member=static_cast<std::uint32_t>(1+bits%2);
                std::erase_if(future.commands,[member](const auto& c){return c.source!=member;});
                lane="future-input";require(owner.submit(member,std::move(future)),owner.error());}
            ingress(tick);lane="predicted-advance";require(owner.ready() && owner.advance(),owner.error());
            require(corrected.nextTick()==tick+1,"rollback forward head mismatch");confirm();
            if(tick>p.history && bits%23==0){
                lane="explicit-history-replay";const auto before=*corrected.checkpoint();
                const auto depth=1+bits%std::min<std::uint64_t>(8,corrected.nextTick()-owner.oldestTick());
                require(owner.replayFrom(corrected.nextTick()-depth),owner.error());
                compare(before,*corrected.checkpoint(),"unchanged-input explicit history replay mismatch");
                require(owner.takeConfirmedEvents().empty(),"explicit replay repeated already drained effects");
            }
            lane="budgets";const auto diagnostic=owner.diagnostics();
            require(diagnostic.bufferedBytes<=bounds.maxBufferedBytes && diagnostic.head-diagnostic.confirmed<=bounds.maxPredictionTicks,"bounded history/prediction invariant");
            // Retain only the current correction/replay horizon, rather than all
            // prior checkpoints. Inputs are pure functions of (seed,tick).
            const auto floor=std::min(checked,tick>p.history?tick-p.history:0);
            while(expected.size()>1 && expected.begin()->first<floor)expected.erase(expected.begin());
        }
        current=count;ingress(count+p.delay+1);confirm();lane="final";
        require(arrivals.empty() && owner.confirmedNextTick()==count,"unconfirmed final contribution");
        compare(*baseline.checkpoint(),*corrected.checkpoint(),"final corrected full state mismatch");
        require(actualEvents==expectedEvents,"missing final confirmed effects");
        const auto diagnostic=owner.diagnostics();result.rollbacks=diagnostic.rollbacks;result.replayedTicks=diagnostic.replayedTicks;
        require(result.rollbacks>0 && result.replayedTicks>0 && result.duplicates>0,"seed did not exercise correction/duplicate lanes");
        result.finalHash=detCheckpointHash(*baseline.checkpoint());return result;
    }catch(const CaseFailure&){throw;}
    catch(const std::exception& error){throw CaseFailure(seed,current,count,lane,error.what());}
}
void contextFaultChecks();
} // namespace detsimulation_campaign
