// design reference: AYEntity/design.md Stage20; sticky callback-local access and request failures.
#include <AYEntity/DeterministicSession.h>
#include <iostream>
#include <stdexcept>

namespace detsimulation_campaign {
using namespace ayt::entity;
namespace {
void demand(bool condition,const std::string& text){if(!condition)throw std::runtime_error(text);}
bool setup(DeterministicSession& session,DeterministicSession::System callback){
    return session.registerSchema({2,1,{1}}) && session.registerGlobalState(2)
        && session.registerTypedSchema({3,1,{{10,std::uint32_t{0}}}})
        && session.registerGlobalState(3) && session.registerRandomStream(7,1)
        && session.registerSystem(1,0,std::move(callback)) && session.addEntity(1) && session.seal();
}
template<class Operation> void caughtFault(const char* name,Operation operation){
    bool caught=false,continued=false,secondRan=false;
    DeterministicSession session;
    demand(session.registerSchema({2,1,{1}}) && session.registerGlobalState(2)
        && session.registerTypedSchema({3,1,{{10,std::uint32_t{0}}}})
        && session.registerGlobalState(3) && session.registerRandomStream(7,1),"context configuration");
    demand(session.registerSystem(1,0,[&](DetTickContext& context){
        context.globals(2)[0]=99;
        try {operation(context);}catch(const std::exception&){caught=true;}
        // A caught request failure must not become successful through a later valid call.
        context.emit(1);continued=true;return true;
    }) && session.registerSystem(2,0,[&](DetTickContext&){secondRan=true;return true;})
        && session.addEntity(1) && session.seal(),"context systems");
    const auto before=*session.checkpoint();
    demand(!session.advance({0,1,{}}) && caught && continued && !secondRan && session.faulted(),std::string(name)+": caught failure must fault tick");
    demand(session.nextTick()==0 && !session.checkpoint() && !session.advance({0,1,{}}),std::string(name)+": fault quarantine");
    demand(session.restore(before) && !session.faulted() && encodeDetCheckpoint(*session.checkpoint())==encodeDetCheckpoint(before),std::string(name)+": explicit restore exact bytes");
}
void structuralBatch(DetTickContext& context){
    // Exactly 1024 valid deferred operations with at most two simultaneous actors.
    for(std::uint64_t id=1000;id<1512;++id){context.spawn(id);context.despawn(id);}
}
}
void contextFaultChecks(){
    caughtFault("unknown pose",[](auto& c){(void)c.pose(9);});
    caughtFault("unknown actor words",[](auto& c){(void)c.words(9,2);});
    caughtFault("unknown schema words",[](auto& c){(void)c.words(1,9);});
    caughtFault("typed word alias",[](auto& c){(void)c.words(1,3);});
    caughtFault("unknown global",[](auto& c){(void)c.globals(9);});
    caughtFault("typed global alias",[](auto& c){(void)c.globals(3);});
    caughtFault("unknown RNG stream",[](auto& c){(void)c.random(9);});
    caughtFault("unknown typed actor",[](auto& c){(void)c.template read<std::uint32_t>(9,3,10);});
    caughtFault("unknown typed field",[](auto& c){c.write(1,3,9,std::uint32_t{1});});
    caughtFault("wrong typed value",[](auto& c){c.write(1,3,10,std::uint64_t{1});});
    caughtFault("zero spawn",[](auto& c){c.spawn(0);});
    caughtFault("zero despawn",[](auto& c){c.despawn(0);});
    caughtFault("invalid spawn shape",[](auto& c){DetActorState actor;actor.blocks[2]={1,2};c.spawn(2,actor);});
    caughtFault("spawn queue limit",[](auto& c){structuralBatch(c);c.spawn(2000);});
    caughtFault("despawn queue limit",[](auto& c){structuralBatch(c);c.despawn(1);});
    caughtFault("zero event type",[](auto& c){c.emit(0);});
    caughtFault("oversized event payload",[](auto& c){std::vector<std::uint8_t> bytes(65537);c.emit(1,bytes);});
    caughtFault("event framing budget",[](auto& c){std::vector<std::uint8_t> bytes(65521);c.emit(1,bytes);});
    // Syntactically valid structural requests retain ordered, deferred topology
    // checks. Rejected prospective identities never reach the structural commit.
    for(unsigned mode=0;mode<3;++mode){
        bool returned=false;DeterministicSession session;
        demand(setup(session,[&](DetTickContext& c){c.globals(2)[0]=99;
            if(mode==0)c.spawn(1);else if(mode==1)c.despawn(9);else c.spawn(2);
            returned=true;return true;
        }),"deferred identity configuration");
        if(mode==2){auto retired=*session.checkpoint();retired.retiredIds={2};demand(session.restore(retired),"retired identity fixture");}
        const auto before=*session.checkpoint();
        demand(!session.advance({0,1,{}}) && returned && session.faulted() && session.nextTick()==0,
            "existing/unknown/retired identity faults prospective boundary");
        demand(!session.checkpoint() && session.restore(before)
            && encodeDetCheckpoint(*session.checkpoint())==encodeDetCheckpoint(before),"deferred topology restore exact bytes");
    }
    // A saturated queue/budget cannot append the helper's trailing valid event.
    // Check the saturation error directly while the callback catches it.
    for(bool byteLimit:{false,true}){
        bool caught=false;DeterministicSession session;
        demand(setup(session,[&](DetTickContext& c){
            if(byteLimit){std::vector<std::uint8_t> bytes(65520);c.emit(1,bytes);}
            else for(unsigned i=0;i<1024;++i)c.emit(1);
            try {c.emit(1);}catch(const std::exception&){caught=true;}return true;
        }),"event upper bound configuration");
        const auto before=*session.checkpoint();
        demand(!session.advance({0,1,{}}) && caught && session.faulted(),"caught event aggregate/count upper bound");
        demand(session.restore(before) && !firstDetDifference(before,*session.checkpoint()),"event budget restore");
    }
    for(unsigned legal=0;legal<3;++legal){
        DeterministicSession session;
        demand(setup(session,[legal](DetTickContext& c){
            if(legal==0){std::vector<std::uint8_t> bytes(65520,0xa5);c.emit(1,bytes);}
            if(legal==1)for(unsigned i=0;i<1024;++i)c.emit(1);
            if(legal==2)structuralBatch(c);return true;
        }),"exact legal upper bound configuration");
        const auto before=*session.checkpoint();demand(session.advance({0,1,{}}),session.error());
        const auto after=*session.checkpoint();
        demand(legal==2?(after.actors.size()==1 && after.retiredIds.size()==512)
            :(after.pendingEvents.size()==(legal==0?1u:1024u)),"exact legal upper bound committed");
        if(legal==0)demand(after.pendingEvents[0].payload.size()==65520,"framing leaves exact payload limit");
        demand(session.restore(before) && session.advance({0,1,{}}) && !firstDetDifference(after,*session.checkpoint()),"upper bound restore/re-execution");
    }
    // The aggregate event budget is shared across producer systems, not reset per context.
    {
        bool caught=false;DeterministicSession session;
        demand(session.registerSystem(1,0,[](auto& c){std::vector<std::uint8_t> bytes(32752);c.emit(1,bytes);return true;})
            && session.registerSystem(2,0,[&](auto& c){std::vector<std::uint8_t> bytes(32752);c.emit(1,bytes);
                try{c.emit(1);}catch(const std::exception&){caught=true;}return true;}) && session.seal(),"two producer budget setup");
        const auto before=*session.checkpoint();demand(!session.advance({0,1,{}}) && caught && session.faulted(),"two producer aggregate budget");
        demand(session.restore(before) && !firstDetDifference(before,*session.checkpoint()),"two producer restore");
    }
    std::cout<<"PASS caught context access/request errors, exact event/structural bounds and explicit restore\n";
}
} // namespace detsimulation_campaign
