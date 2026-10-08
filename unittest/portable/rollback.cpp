// design reference: AYEntity/design.md, bounded rollback and predictive networking.
#include "../DetTypedSessionScenario.h"
#include "../DetExtendedCollisionScenario.h"
#include <AYEntity/DeterministicRollback.h>
#include <iostream>
#include <cfenv>
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif
using namespace ayt::entity;
void require(bool b,const std::string& text){if(!b)throw std::runtime_error(text);}
DetTickInput part(DetTickInput in,std::uint32_t member) {
    std::erase_if(in.commands,[member](const auto& c){return c.source!=member;});return in;
}
DetRollbackConfig policy(std::uint32_t history=32) {
    DetRollbackConfig c;c.members={1,2};c.historyTicks=history;c.maxPredictionTicks=8;
    c.prediction={{1,DetPredictionMode::Hold},{2,DetPredictionMode::Hold}};return c;
}
void coreChecks() {
    for(bool collision:{false,true}) {
        DeterministicSession s({1,1,1,64,0}),baseline({1,1,1,64,0});
        require(collision?detextendedcollision_scenario::configure(s):dettyped_scenario::configure(s),"core config");
        require(collision?detextendedcollision_scenario::configure(baseline,true):dettyped_scenario::configure(baseline,true),"baseline config");
        DeterministicRollback r(s,policy());std::vector<DetConfirmedEvent> expected,actual;
        const std::uint32_t late=collision?1:2, immediate=collision?2:1;
        auto input=[collision](auto tick){return collision?detextendedcollision_scenario::input(tick):detsession_scenario::input(tick);};
        for(std::uint64_t tick=0;tick<10000;++tick) {
            const auto in=input(tick);require(baseline.advance(in),baseline.error());
            const auto base=baseline.checkpoint();for(const auto& c:base->pendingEvents)expected.push_back({1,tick,c});
            require(r.submit(immediate,part(in,immediate)),r.error());
            // Seed both members at tick zero. Later inputs are actually delayed three ticks.
            if(!tick)require(r.submit(late,part(in,late)),r.error());
            if(tick>=3)require(r.submit(late,part(input(tick-3),late)),r.error());
            require(r.advance(),r.error());
            auto effects=r.takeConfirmedEvents();actual.insert(actual.end(),effects.begin(),effects.end());
            if(collision)require(detextendedcollision_scenario::matchesGolden(*r.checkpointAt(r.confirmedNextTick())),"confirmed collision oracle");
            if(tick==5000){const auto before=encodeDetCheckpoint(*s.checkpoint());
                require(r.replayFrom(s.nextTick()-16),r.error());
                require(encodeDetCheckpoint(*s.checkpoint())==before && r.takeConfirmedEvents().empty(),"explicit replay must not repeat effects");}
        }
        for(std::uint64_t tick=9997;tick<10000;++tick)require(r.submit(late,part(input(tick),late)),r.error());
        auto effects=r.takeConfirmedEvents();actual.insert(actual.end(),effects.begin(),effects.end());
        require(r.confirmedNextTick()==10000 && encodeDetCheckpoint(*s.checkpoint())==encodeDetCheckpoint(*baseline.checkpoint()),"10000 corrected full bytes");
        require(actual==expected && r.takeConfirmedEvents().empty(),"confirmed effects exactly once and no predicted effects");
        require(r.rollbackCount()>1 && r.replayedTicks()>0 && r.oldestTick()>9000,"rollback and bounded eviction exercised");
        const auto d=r.diagnostics();
        require(d.head==10000 && d.confirmed==10000 && d.verified==10000 && d.epoch==1
            && d.maxDepth>=d.lastDepth && d.maxDepth>0 && d.predictedTicks>0 && d.bufferedBytes<=r.config().maxBufferedBytes,
            "read-only rollback diagnostics and canonical byte budget");
        auto expectedReal=input(9999);std::string diagnostic;require(canonicalizeDetInput(expectedReal,diagnostic),diagnostic);
        require(!r.confirmedInputAt(0) && r.confirmedInputAt(9999)==expectedReal,"only retained canonical real merged frames exposed");
        if(collision)require(detCheckpointHash(*s.checkpoint())==0x0e0940a868bb4d4dull,"existing extended collision golden");
        std::cout<<"PASS "<<(collision?"collision":"RNG/typed/structural")<<" rollback 10000 ticks, exact state/effects, replayed="<<r.replayedTicks()<<'\n';
    }
    // Semantic prediction, including future real frames and matching late seeds.
    DeterministicSession s;require(s.registerGlobalState(2)==false,"unknown schema");
    require(s.registerSchema({2,1,{1,2,3}}) && s.registerGlobalState(2),"prediction schema");
    require(s.registerSystem(1,0,[](auto& c){auto g=c.globals(2);
        for(const auto& in:c.input().commands)g[in.type-1]+=in.payload[0];return true;}) && s.seal(),"prediction setup");
    auto c=policy();c.prediction={{1,DetPredictionMode::Hold},{2,DetPredictionMode::Zero}};c.maxPredictionTicks=3;
    DeterministicRollback r(s,c);
    require(r.submit(1,{0,1,{{1,0,1,{2}},{1,1,2,{3}},{1,2,3,{5}}}}) && r.submit(2,{0,1,{}}) && r.advance(),"real inputs");
    require(r.submit(1,{3,1,{{1,0,1,{9}}}}),"future contribution");
    require(r.advance(),"prediction tick1");auto cp=*s.checkpoint();
    require(cp.globals.at(2)==std::vector<std::uint64_t>({4,3,5}),"Hold/Zero/Omit and future input not used");
    require(r.submit(1,{1,1,{{1,0,1,{0}}}}),"late release");
    require(s.checkpoint()->globals.at(2)==std::vector<std::uint64_t>({2,3,5}),"release correction");
    require(r.advance() && r.advance() && !r.ready() && !r.advance(),"bounded prediction horizon");
    require(!r.submit(9,{0,1,{}}) && !r.submit(1,{4,2,{}}) && !r.submit(2,{4,1,{{1,0,1,{2}}}}),"invalid member/version/source");
    require(!r.replayFrom(999) && !r.faulted(),"bad replay range does not fault");
    require(r.submit(1,{4,1,{}})&&r.submit(2,{4,1,{}})&&!r.ready(),"current actual frames cannot bypass an older unconfirmed horizon");
    // A matching late frame can still change later predictions (event omitted originally).
    DeterministicSession seed;require(detsession_scenario::configure(seed),"matching seed config");
    auto omit=policy();omit.prediction.clear();DeterministicRollback sr(seed,omit);
    require(sr.submit(1,{0,1,{}})&&sr.submit(2,{0,1,{}})&&sr.advance()&&sr.advance()&&sr.advance(),"neutral seeds");
    require(sr.submit(2,{1,1,{}})&&sr.rollbackCount()==0,"matching correction no replay");
    require(!sr.submit(2,{1,1,{{2,0,1,{3}}}})&&sr.faulted(),"actual conflict is terminal");
    // A callback failure restores pose, RNG, writes and topology to the pre-operation boundary.
    DeterministicSession f;require(f.registerSchema({2,1,{1}})&&f.registerGlobalState(2),"fault state");
    require(f.registerSystem(1,0,[](auto& ctx){ctx.globals(2)[0]=99;return false;})&&f.seal(),"fault callback");
    const auto before=encodeDetCheckpoint(*f.checkpoint());DeterministicRollback owner(f);
    require(owner.submit(1,{0,1,{}})&&!owner.advance()&&owner.faulted(),"owner faults");
    require(encodeDetCheckpoint(*f.checkpoint())==before,"failed tick atomic registered state");
    DeterministicSession memory;require(detsession_scenario::configure(memory),"budget state");
    auto tiny=policy();tiny.maxBufferedBytes=1024;
    DeterministicRollback limited(memory,tiny);const auto original=encodeDetCheckpoint(*memory.checkpoint());
    require(limited.submit(1,part(detsession_scenario::input(0),1)) && limited.submit(2,part(detsession_scenario::input(0),2)),"budget inputs");
    require(!limited.advance() && limited.faulted() && encodeDetCheckpoint(*memory.checkpoint())==original,"budget failure atomic state");
    std::cout<<"PASS semantic predictors, bounds, invalid input, conflicts and atomic execution/budget failure\n";
}
void networkChecks(std::uint32_t delay) {
    DeterministicSession a({1,1,1,64,0}),b({1,1,1,64,0}),baseline({1,1,1,64,0});
    require(dettyped_scenario::configure(a)&&dettyped_scenario::configure(b,true)&&dettyped_scenario::configure(baseline),"network state");
    DetRollbackNetworkConfig cfg{91,1,1,1,delay,policy(16)};
    DeterministicRollbackNetwork x(a,cfg);cfg.localMember=2;DeterministicRollbackNetwork y(b,cfg);
    std::vector<DetConfirmedEvent> expected,xe,ye;
    constexpr std::uint64_t count=10000;
    for(std::uint64_t tick=0;tick<count;++tick) {
        DetTickInput in{tick,1,{}};if(tick>=delay){in=detsession_scenario::input(tick-delay);in.tick=tick;}
        require(baseline.advance(in),baseline.error());
        const auto cp=baseline.checkpoint();for(const auto& c:cp->pendingEvents)expected.push_back({1,tick,c});
    }
    struct Queued {std::uint64_t at;std::uint32_t sender;std::vector<std::uint8_t> bytes;};std::vector<Queued> queue;
    std::uint64_t packets=0,round=0,samplesA=0,samplesB=0;
    auto sample=[&](auto& net,auto& s,std::uint32_t member,std::uint64_t& samples){
        if(s.nextTick()>=count || net.history().hasInput(member,net.localInputTick()))return;
        auto in=part(detsession_scenario::input(net.localInputTick()-delay),member);in.tick=net.localInputTick();
        require(net.submitLocal(std::move(in)),net.error());++samples;};
    for(;round<100000 && !(a.nextTick()==count && b.nextTick()==count && x.synchronized() && y.synchronized());++round) {
        sample(x,a,1,samplesA);sample(y,b,2,samplesB);
        for(auto* net:{&x,&y}) {auto list=net->packets();if(net==&y)std::reverse(list.begin(),list.end());
            for(auto& packet:list) {if(++packets%37==0)continue;
                queue.push_back({round+(packets%3),net->config().localMember,std::move(packet)});}}
        // Deliver newest ready packets first; retransmission repairs application-level loss.
        for(auto it=queue.end();it!=queue.begin();) {--it;if(it->at>round)continue;
            auto& target=it->sender==1?y:x;
            require(target.receive(it->sender,it->bytes),target.error());it=queue.erase(it);}
        if(a.nextTick()<count && x.ready())require(x.advance(),x.error());
        if(b.nextTick()<count && y.ready())require(y.advance(),y.error());
        auto e=x.takeConfirmedEvents();xe.insert(xe.end(),e.begin(),e.end());e=y.takeConfirmedEvents();ye.insert(ye.end(),e.begin(),e.end());
    }
    auto e=x.takeConfirmedEvents();xe.insert(xe.end(),e.begin(),e.end());e=y.takeConfirmedEvents();ye.insert(ye.end(),e.begin(),e.end());
    require(round<100000 && samplesA==count && samplesB==count,"network horizon/retransmit and sampling");
    require(encodeDetCheckpoint(*a.checkpoint())==encodeDetCheckpoint(*baseline.checkpoint()) &&
        encodeDetCheckpoint(*b.checkpoint())==encodeDetCheckpoint(*baseline.checkpoint()),"network corrected complete bytes");
    require(xe==expected && ye==expected && x.takeConfirmedEvents().empty(),"all-peer verified effects exactly once");
    require(!y.beginRecovery(2) && !x.beginRecovery(1),"authority and newer epoch enforced");
    auto stale=x.packets();x.disconnect(2);y.disconnect(1);require(x.faulted()&&y.faulted(),"disconnect faults");
    require(x.beginRecovery(2),x.error());
    for(unsigned n=0;n<5;++n) {auto list=x.packets();std::reverse(list.begin(),list.end());
        for(const auto& p:list)(void)y.receive(1,p);
        for(const auto& p:y.packets())require(x.receive(2,p),x.error());}
    require(x.synchronized()&&y.synchronized()&&y.config().epoch==2,"authority checkpoint transfer and roster handshake");
    for(const auto& p:stale)require(y.receive(1,p),"stale epoch ignored");
    require(x.takeConfirmedEvents().empty() && y.takeConfirmedEvents().empty(),"recovery repeats no effects");
    require(encodeDetCheckpoint(*a.checkpoint())==encodeDetCheckpoint(*b.checkpoint()),"recovery full bytes");
    std::cout<<"PASS predictive network 10000 ticks, delay="<<delay<<", dropped/reordered/duplicate packets, confirmed effects and new epoch recovery\n";
}
void transferChecks() {
    auto configure=[](DeterministicSession& s){DetStateSchema schema{2,1,{}};
        for(unsigned i=1;i<=128;++i)schema.fields.push_back(i);
        if(!s.registerSchema(schema))return false;
        for(unsigned id=1;id<=64;++id)if(!s.addEntity(id))return false;
        return s.seal();};
    DeterministicSession a,b;require(configure(a)&&configure(b),"large recovery state");
    DetRollbackNetworkConfig cfg{93,1,1,1,0,policy()};DeterministicRollbackNetwork x(a,cfg);
    cfg.localMember=2;DeterministicRollbackNetwork y(b,cfg);
    const auto before=encodeDetCheckpoint(*b.checkpoint());require(before.size()>4*16384,"multi-chunk fixture");
    x.disconnect(2);y.disconnect(1);require(x.beginRecovery(2),x.error());
    auto list=x.packets();std::vector<std::vector<std::uint8_t>> chunks;
    for(const auto& p:list)if(p[24]==4)chunks.push_back(p);
    require(chunks.size()>4,"checkpoint chunk count");
    auto corrupted=chunks.back();corrupted[60]^=1;
    require(!y.receive(1,corrupted) && encodeDetCheckpoint(*b.checkpoint())==before,"corrupt chunk leaves state unchanged");
    require(!y.receive(99,chunks.back()),"authenticate transfer sender");
    for(std::size_t i=chunks.size();i-->1;) {
        require(y.receive(1,chunks[i])&&y.receive(1,chunks[i]),"reordered duplicate chunks");
        require(y.config().epoch==1 && encodeDetCheckpoint(*b.checkpoint())==before,"incomplete transfer never restores");
    }
    require(y.receive(1,chunks.front())&&y.config().epoch==2,"complete validated transfer restores");
    for(unsigned round=0;round<3;++round) {
        for(const auto& p:x.packets())require(y.receive(1,p),y.error());
        for(const auto& p:y.packets())require(x.receive(2,p),x.error());
    }
    require(x.synchronized()&&y.synchronized()&&encodeDetCheckpoint(*a.checkpoint())==before,"multi-chunk recovery bytes/handshake");
    // Different immutable prediction semantics fail even with identical Session manifests.
    DeterministicSession m,n;require(m.seal()&&n.seal(),"policy mismatch state");
    cfg.sessionId=94;cfg.localMember=1;DeterministicRollbackNetwork left(m,cfg);
    cfg.localMember=2;cfg.rollback.prediction.clear();DeterministicRollbackNetwork right(n,cfg);
    require(!right.receive(1,left.packets().front())&&right.faulted(),"prediction policy identity handshake");
    std::cout<<"PASS multi-chunk atomic recovery, admission/corruption/duplicates and prediction identity\n";
}
void effectRecoveryChecks() {
    DeterministicSession a,b;require(detsession_scenario::configure(a)&&detsession_scenario::configure(b),"effect recovery state");
    DetRollbackNetworkConfig cfg{96,1,1,1,0,policy()};DeterministicRollbackNetwork x(a,cfg);
    cfg.localMember=2;DeterministicRollbackNetwork y(b,cfg);
    for(const auto& p:x.packets())require(y.receive(1,p),y.error());
    for(const auto& p:y.packets())require(x.receive(2,p),x.error());
    std::vector<DetConfirmedEvent> expected;
    for(std::uint64_t tick=0;tick<3;++tick) {
        require(x.submitLocal(part(detsession_scenario::input(tick),1)),x.error());
        require(y.submitLocal(part(detsession_scenario::input(tick),2)),y.error());
        for(const auto& p:x.packets())if(p[24]!=3)require(y.receive(1,p),y.error());
        for(const auto& p:y.packets())require(x.receive(2,p),x.error());
        require(x.advance()&&y.advance(),"effect recovery advance");
        for(const auto& p:y.packets())require(x.receive(2,p),x.error());
        const auto cp=a.checkpoint();for(const auto& c:cp->pendingEvents)expected.push_back({1,tick,c});
    }
    require(x.takeConfirmedEvents()==expected&&y.takeConfirmedEvents().empty(),"asymmetric effect confirmation");
    require(x.beginRecovery(2),x.error());
    for(unsigned round=0;round<4;++round) {
        for(const auto& p:x.packets())require(y.receive(1,p),y.error());
        for(const auto& p:y.packets())require(x.receive(2,p),x.error());
    }
    require(x.takeConfirmedEvents().empty()&&y.takeConfirmedEvents()==expected&&y.takeConfirmedEvents().empty(),"recovery journal preserves pending effects without duplicating played effects");
    // Continue through the new epoch using normal inputs and new event identities.
    require(x.submitLocal(part(detsession_scenario::input(3),1))&&y.submitLocal(part(detsession_scenario::input(3),2)),"post recovery sample");
    for(const auto& p:x.packets())require(y.receive(1,p),y.error());
    for(const auto& p:y.packets())require(x.receive(2,p),x.error());
    require(x.advance()&&y.advance(),"post recovery advance");
    for(unsigned round=0;round<2;++round) {
        for(const auto& p:x.packets())require(y.receive(1,p),y.error());
        for(const auto& p:y.packets())require(x.receive(2,p),x.error());
    }
    auto next=x.takeConfirmedEvents();require(next==y.takeConfirmedEvents()&&next.size()==1&&next[0].epoch==2&&next[0].tick==3,"new epoch effects");
    std::cout<<"PASS asymmetric confirmation recovery journal, old effect deduplication and continued new epoch Sim\n";
}
int main(){try {
    std::fesetround(FE_UPWARD);
#if defined(_M_X64) || defined(__x86_64__)
    _mm_setcsr(_mm_getcsr()|0x8040u);
#endif
    coreChecks();networkChecks(0);networkChecks(2);transferChecks();effectRecoveryChecks();
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
