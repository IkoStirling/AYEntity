#include <AYEntity/DeterministicSession.h>
#include "detail/DetSessionStorage.h"
#include "detail/DetSessionWire.h"
#include <set>
#include <algorithm>

namespace ayt::entity {
using namespace detwire;
namespace {
bool sameState(const DetSessionCheckpoint& a,const DetSessionCheckpoint& b) {
    if(a.manifest!=b.manifest || a.nextTick!=b.nextTick || a.actors!=b.actors || a.globals!=b.globals
        || a.pendingEvents!=b.pendingEvents || a.retiredIds!=b.retiredIds || a.randomStreams.size()!=b.randomStreams.size())return false;
    auto i=a.randomStreams.begin(),j=b.randomStreams.begin();
    for(;i!=a.randomStreams.end();++i,++j)if(i->first!=j->first || i->second.state!=j->second.state || i->second.inc!=j->second.inc)return false;
    return true;
}
}
struct DeterministicSession::Impl {
    struct Schema { DetStateSchema descriptor;std::vector<std::uint64_t> defaults;std::optional<DetTypedStateSchema> typed;std::optional<DetStateLayout> layout; };
    struct SystemEntry { std::uint32_t id;std::int32_t priority;System callback; };
    struct ValidatorEntry {std::uint32_t version;Validator callback;};
    std::map<std::uint32_t,ValidatorEntry> validators;
    mutable bool validating=false;
    mutable bool validationRejected=false;
    using Actor=DetActorSlot;
    std::map<std::uint32_t,std::pair<std::uint32_t,std::uint64_t>> logicProfiles;
    struct Mutation { SimEntityId id;bool spawn;DetActorState initial; };
    std::unique_ptr<DetSessionStorage> storage;DetSessionConfig config;math::DetFloat32 dt;
    DeterministicSession* owner=nullptr;
    std::map<std::uint32_t,Schema> schemas;
    std::vector<SystemEntry> systems;
    std::map<std::uint32_t,std::uint64_t> seeds;
    std::map<SimEntityId,Actor> actors;
    DetSessionCheckpoint state,last;
    DetTickInput input;
    std::vector<DetTickCommand> outgoing;
    std::vector<Mutation> mutations;
    bool sealed=false,faulted=false,inTick=false;
    bool accessFailed=false;
    mutable std::string error;
    Impl(std::unique_ptr<DetSessionStorage> value,DetSessionConfig c):storage(std::move(value)),config(c),dt(math::DetFloat32::fromUInt(c.stepNumerator)/math::DetFloat32::fromUInt(c.stepDenominator)) {}
    bool fail(std::string e) const {if(validating)validationRejected=true;error=std::move(e);return false; }
    bool configuring() {
        if (!storage->owns(owner))
            return fail("Session lost World ownership");
        if(validating)return fail("Validator session reentry");
        return !sealed || fail("Session configuration is sealed");
    }
    bool actorValid(const Actor& a) const { return storage->valid(a); }
    bool validBlocks(const DetStateBlocks& b,bool all) const {
        if(all && b.size()!=schemas.size())return false;
        for(const auto& [id,words]:b){auto it=schemas.find(id);if(it==schemas.end() || words.size()!=it->second.defaults.size())return false;
            if(it->second.layout && !it->second.layout->valid(words))return false;}
        return true;
    }
    bool validActor(const DetActorState& a) const {
        DetSimTransformComponent test;return test.restore(a.pose) && validBlocks(a.blocks,true);
    }
    void defaults(DetActorState& a) const {
        for(const auto& [id,s]:schemas)if(!a.blocks.contains(id))a.blocks[id]=s.defaults;
    }
    std::vector<std::uint8_t> buildManifest() const {
        const bool typed=!logicProfiles.empty() || !validators.empty() || std::any_of(schemas.begin(),schemas.end(),[](const auto& entry){return entry.second.typed.has_value();});
        Writer w;w.u32(manifestMagic);w.u32(!logicProfiles.empty()?4:!validators.empty()?3:typed?2:1);
        w.u32(config.applicationVersion);w.u32(config.inputVersion);w.u32(config.stepNumerator);w.u32(config.stepDenominator);w.u32(dt.bits());
        w.u32(1);w.u32(DetSimTransformComponent::kSnapshotVersion);w.u64(config.contentHash);
        w.u32(math::DetFloat32::kProfileVersion);w.u32(math::DetQuaternion::kRotationProfileVersion);
        w.u32(math::kDetMathProfileVersion);w.u32(math::DetQuaternion::kAngularIntegrationProfileVersion);
        w.u32(static_cast<std::uint32_t>(schemas.size()));
        for(const auto& [id,s]:schemas){w.u32(id);w.u32(s.descriptor.version);w.u32(static_cast<std::uint32_t>(s.descriptor.fields.size()));
            for(std::size_t i=0;i<s.descriptor.fields.size();++i){w.u32(s.descriptor.fields[i]);
                if(s.typed){const auto& field=s.typed->fields[i];w.u32(static_cast<std::uint32_t>(detStateType(field.initial)));
                    for(auto word:encodeDetStateValue(field.initial))w.u64(word);}
                else {if(typed)w.u32(static_cast<std::uint32_t>(DetStateType::Word));w.u64(s.defaults[i]);}}}
        w.u32(static_cast<std::uint32_t>(systems.size()));for(const auto& s:systems){w.u32(s.id);w.u32(static_cast<std::uint32_t>(s.priority));}
        w.u32(static_cast<std::uint32_t>(seeds.size()));for(const auto& [id,seed]:seeds){w.u32(id);w.u64(seed);}
        w.u32(static_cast<std::uint32_t>(state.globals.size()));for(const auto& [id,v]:state.globals)w.u32(id);
        if(!validators.empty() || !logicProfiles.empty()){w.u32(static_cast<std::uint32_t>(validators.size()));
            for(const auto& [id,v]:validators){w.u32(id);w.u32(v.version);}}
        if(!logicProfiles.empty()){w.u32(static_cast<std::uint32_t>(logicProfiles.size()));
            for(const auto& [id,v]:logicProfiles){w.u32(id);w.u32(v.first);w.u64(v.second);}}
        return w.finish();
    }
    std::optional<DetSessionCheckpoint> capture() const {
        if(!sealed)return fail("Session is not sealed"),std::nullopt;
        DetSessionCheckpoint s=state;s.actors.clear();
        for(const auto& [id,a]:actors){
            if(!actorValid(a))return fail("Managed entity topology changed"),std::nullopt;
            DetActorState value{a.pose->snapshot(),*a.blocks};
            if(!validActor(value))return fail("Invalid registered Sim state"),std::nullopt;
            s.actors.emplace(id,std::move(value));
        }
        return s;
    }
    Actor create(const DetActorState& value) { return storage->create(value); }
    bool validate(const DetSessionCheckpoint& s) const {
        if(s.manifest!=state.manifest)return fail("Session manifest/profile/schema mismatch");
        if(s.actors.size()>maxActors || s.randomStreams.size()!=seeds.size() || s.globals.size()!=state.globals.size()
            || !validBlocks(s.globals,false))return fail("Checkpoint state shape mismatch");
        for(const auto& [id,v]:state.globals)if(!s.globals.contains(id))return fail("Global state set mismatch");
        if(s.retiredIds.size()>65536 || !std::is_sorted(s.retiredIds.begin(),s.retiredIds.end())
            || std::adjacent_find(s.retiredIds.begin(),s.retiredIds.end())!=s.retiredIds.end())return fail("Invalid retired IDs");
        for(auto id:s.retiredIds)if(!id || s.actors.contains(id))return fail("Reused/zero actor identity");
        for(const auto& [id,a]:s.actors)if(!id || !validActor(a))return fail("Invalid actor state/schema");
        for(const auto& [id,seed]:seeds){auto it=s.randomStreams.find(id);
            if(it==s.randomStreams.end() || it->second.inc!=math::make_pcg32_state(seed).inc)return fail("RNG stream identity mismatch");}
        DetTickInput events{s.nextTick,config.inputVersion,s.pendingEvents};std::string e;
        if(!canonicalizeDetInput(events,e) || events.commands!=s.pendingEvents)return fail("Invalid pending event order/size");
        for(const auto& c:s.pendingEvents)if(std::none_of(systems.begin(),systems.end(),[&](const auto& sys){return sys.id==c.source;}))return fail("Unknown event producer");
        try { (void)encodeDetCheckpoint(s); }catch(const std::exception& e){return fail(e.what());}
        if(validating)return fail("Validator session reentry");
        validating=true;validationRejected=false;
        for(const auto& [id,v]:validators){
            std::string diagnostic;
            try {if(v.callback(s,diagnostic) && !validationRejected)continue;
                if(validationRejected)diagnostic=error;}
            catch(const std::exception& e){diagnostic=e.what();}
            catch(...){diagnostic="unknown exception";}
            validating=false;return fail("State validator "+std::to_string(id)+": "+diagnostic);
        }
        validating=false;return true;
    }
};
namespace {
class DetMemoryStorage final : public DetSessionStorage {
    DeterministicSession* _owner=nullptr;
public:
    void claim(DeterministicSession* owner) override { _owner=owner; }
    bool owns(const DeterministicSession* owner) const noexcept override { return _owner==owner; }
    void release(const DeterministicSession* owner) noexcept override { if(_owner==owner)_owner=nullptr; }
    DetActorSlot create(const DetActorState& value) override {
        auto memory=std::make_shared<DetActorMemory>();
        if(!memory->pose.restore(value.pose))throw std::runtime_error("Invalid actor pose");
        memory->blocks=value.blocks;return {&memory->pose,&memory->blocks,nullptr,0,std::move(memory)};
    }
    bool valid(const DetActorSlot& a) const noexcept override { return a.memory && a.pose==&a.memory->pose && a.blocks==&a.memory->blocks; }
    void destroy(const DetActorSlot&) noexcept override {}
    Entity* presentation(const DetActorSlot&) const noexcept override { return nullptr; }
};
}
DeterministicSession::DeterministicSession(DetSessionConfig config)
    :DeterministicSession(std::make_unique<DetMemoryStorage>(),config){}
DeterministicSession::DeterministicSession(std::unique_ptr<DetSessionStorage> storage,DetSessionConfig config)
    :_impl(std::make_unique<Impl>(std::move(storage),config)) {
    if(!config.applicationVersion || !config.inputVersion || !config.stepNumerator || !config.stepDenominator || !_impl->dt.isFinite() || !(_impl->dt>math::DetFloat32{}))
        throw std::invalid_argument("Invalid session version/fixed step");
    _impl->storage->claim(this);_impl->owner=this;
}
DeterministicSession::~DeterministicSession() {
    auto& p=*_impl;
    if(p.storage->owns(this))for(const auto& [id,a]:p.actors)p.storage->destroy(a);
    p.storage->release(this);
}
bool DeterministicSession::registerLogicProfile(std::uint32_t id,std::uint32_t version,std::uint64_t hash) {
    auto& p=*_impl;if(!p.configuring())return false;
    if(!id || !version || p.logicProfiles.size()>=maxSchemas || p.logicProfiles.contains(id))return p.fail("Invalid/duplicate logic profile");
    p.logicProfiles.emplace(id,std::pair{version,hash});return true;
}
bool DeterministicSession::registerSchema(DetStateSchema schema,std::vector<std::uint64_t> defaults) {
    auto& p=*_impl;if(!p.configuring())return false;
    if(schema.id<2 || schema.id==UINT32_MAX || !schema.version || schema.fields.empty() || schema.fields.size()>maxFields
        || p.schemas.size()>=maxSchemas || p.schemas.contains(schema.id) || !p.actors.empty())return p.fail("Invalid/late state schema");
    if(!std::is_sorted(schema.fields.begin(),schema.fields.end()) || schema.fields.front()==0
        || std::adjacent_find(schema.fields.begin(),schema.fields.end())!=schema.fields.end())return p.fail("Field IDs must be unique increasing nonzero values");
    if(defaults.empty())defaults.resize(schema.fields.size());
    if(defaults.size()!=schema.fields.size())return p.fail("Default field count mismatch");
    p.schemas.emplace(schema.id,Impl::Schema{std::move(schema),std::move(defaults),std::nullopt,std::nullopt});return true;
}
bool DeterministicSession::registerTypedSchema(DetTypedStateSchema schema) {
    auto& p=*_impl;if(!p.configuring())return false;
    if(p.schemas.size()>=maxSchemas || p.schemas.contains(schema.id) || !p.actors.empty())return p.fail("Duplicate/late typed state schema");
    try {
        DetStateLayout layout(schema);auto defaults=layout.defaults();DetStateSchema descriptor{schema.id,schema.version,{}};
        for(const auto& field:schema.fields)descriptor.fields.push_back(field.id);
        p.schemas.emplace(schema.id,Impl::Schema{std::move(descriptor),std::move(defaults),std::move(schema),std::move(layout)});return true;
    }catch(const std::exception& e){return p.fail(e.what());}
}
bool DeterministicSession::registerSystem(std::uint32_t id,std::int32_t priority,System callback) {
    auto& p=*_impl;if(!p.configuring())return false;
    if(!id || !callback || p.systems.size()>=maxSchemas || std::any_of(p.systems.begin(),p.systems.end(),[&](const auto& s){return s.id==id;}))return p.fail("Invalid/duplicate system ID");
    p.systems.push_back({id,priority,std::move(callback)});return true;
}
bool DeterministicSession::registerValidator(std::uint32_t id,std::uint32_t version,Validator callback) {
    auto& p=*_impl;if(!p.configuring())return false;
    if(!id || !version || !callback || p.validators.size()>=maxSchemas || p.validators.contains(id))return p.fail("Invalid/duplicate validator ID/version");
    p.validators.emplace(id,Impl::ValidatorEntry{version,std::move(callback)});return true;
}
bool DeterministicSession::registerRandomStream(std::uint32_t id,std::uint64_t seed) {
    auto& p=*_impl;if(!p.configuring())return false;
    if(!id || p.seeds.size()>=maxSchemas || p.seeds.contains(id))return p.fail("Invalid/duplicate RNG stream ID");
    p.seeds[id]=seed;p.state.randomStreams[id]=math::make_pcg32_state(seed);return true;
}
bool DeterministicSession::registerGlobalState(std::uint32_t schema,std::vector<std::uint64_t> words) {
    auto& p=*_impl;if(!p.configuring())return false;
    auto it=p.schemas.find(schema);if(it==p.schemas.end() || p.state.globals.contains(schema))return p.fail("Unknown/duplicate global schema");
    if(words.empty())words=it->second.defaults;
    if(!p.validBlocks({{schema,words}},false))return p.fail("Invalid global state shape/value");
    p.state.globals[schema]=std::move(words);return true;
}
bool DeterministicSession::addEntity(SimEntityId id,DetActorState initial) {
    auto& p=*_impl;if(!p.configuring())return false;
    if(!id || p.actors.contains(id) || p.actors.size()>=maxActors)return p.fail("Invalid/duplicate actor ID");
    p.defaults(initial);if(!p.validActor(initial))return p.fail("Invalid initial actor state");
    auto slot=p.actors.emplace(id,Impl::Actor{}).first;
    try { slot->second=p.create(initial);return true; }
    catch(const std::exception& e){p.actors.erase(slot);return p.fail(e.what());}
}
bool DeterministicSession::seal() {
    auto& p=*_impl;
    if(p.validating || !p.storage->owns(this))return p.fail("Session reentry or lost World ownership");
    if(p.sealed)return true;
    std::sort(p.systems.begin(),p.systems.end(),[](const auto& a,const auto& b){return std::pair{a.priority,a.id}<std::pair{b.priority,b.id};});
    try {p.state.manifest=p.buildManifest();}catch(const std::exception& e){return p.fail(e.what());}
    if(p.state.manifest.size()>maxInputBytes)return p.fail("Session manifest size limit");
    p.sealed=true;
    auto s=p.capture();if(!s || !p.validate(*s)){p.sealed=false;return false;}p.last=std::move(*s);p.error.clear();return true;
}
bool DeterministicSession::advance(DetTickInput input) {
    auto& p=*_impl;if(p.validating || !p.sealed || p.faulted || p.inTick || !p.storage->owns(this))return p.fail("Session not ready or lost tick ownership");
    if(input.tick!=p.state.nextTick || input.version!=p.config.inputVersion || input.tick==UINT64_MAX)return p.fail("Unexpected input tick/version");
    if(!canonicalizeDetInput(input,p.error))return false;
    auto before=p.capture();if(!before)return false;
    if(!sameState(p.last,*before))return p.fail("Registered Sim state changed outside the session tick");
    p.input=std::move(input);p.inTick=true;p.accessFailed=false;p.mutations.clear();p.outgoing.clear();
    try {
        for(const auto& [id,a]:p.actors)a.pose->beginSimulationStep();
        for(auto& s:p.systems){DetTickContext context(*this,s.id);if(!s.callback(context))throw std::runtime_error("Session system rejected tick: "+std::to_string(s.id));
            if(p.accessFailed)throw std::runtime_error(p.error);}
        std::set<SimEntityId> live,retired(p.state.retiredIds.begin(),p.state.retiredIds.end());for(const auto& [id,a]:p.actors)live.insert(id);
        for(const auto& m:p.mutations){
            if(m.spawn){if(!m.id || live.contains(m.id) || retired.contains(m.id) || !p.validActor(m.initial))throw std::runtime_error("Invalid spawn command");live.insert(m.id);}
            else {if(!live.erase(m.id))throw std::runtime_error("Unknown despawn ID");retired.insert(m.id);}
            if(live.size()>maxActors || retired.size()>65536)throw std::runtime_error("Session entity count limit");
        }
        DetTickInput events{p.state.nextTick+1,p.config.inputVersion,p.outgoing};
        if(!canonicalizeDetInput(events,p.error))throw std::runtime_error(p.error);
        // Validate the complete prospective boundary before creating/destroying actors.
        auto prospective=p.capture();if(!prospective)throw std::runtime_error(p.error);
        for(const auto& m:p.mutations){if(m.spawn)prospective->actors.emplace(m.id,m.initial);else prospective->actors.erase(m.id);}
        prospective->retiredIds.assign(retired.begin(),retired.end());prospective->pendingEvents=events.commands;++prospective->nextTick;
        if(!p.validate(*prospective))throw std::runtime_error(p.error);
        for(const auto& m:p.mutations){if(m.spawn){auto slot=p.actors.emplace(m.id,Impl::Actor{}).first;slot->second=p.create(m.initial);}
            else{auto a=p.actors.at(m.id);p.storage->destroy(a);p.actors.erase(m.id);}}
        p.state.retiredIds.assign(retired.begin(),retired.end());p.state.pendingEvents=std::move(events.commands);++p.state.nextTick;
        p.last=std::move(*prospective);p.inTick=false;p.error.clear();return true;
    }catch(const std::exception& e){p.faulted=true;p.inTick=false;return p.fail(e.what());}
    catch(...){p.faulted=true;p.inTick=false;return p.fail("Unknown system failure");}
}
std::optional<DetSessionCheckpoint> DeterministicSession::checkpoint() const {
    auto& p=*_impl;if(p.validating || p.inTick || p.faulted || !p.storage->owns(this)){p.fail("Checkpoint requires a quiescent valid session");return std::nullopt;}
    auto s=p.capture();if(s && !p.validate(*s))return std::nullopt;
    if(s && !sameState(p.last,*s)){p.fail("Registered Sim state changed outside the session tick");return std::nullopt;}
    return s;
}
bool DeterministicSession::restore(const DetSessionCheckpoint& s) {
    auto& p=*_impl;if(p.validating || !p.sealed || p.inTick || !p.storage->owns(this))return p.fail("Restore requires a sealed quiescent session");
    if(!p.validate(s))return false;
    // Stage all allocations before mutating retained poses or discarding actors.
    DetSessionCheckpoint staged,newState,newLast;std::map<SimEntityId,Impl::Actor> replacement,newActors;
    try {
        staged=s;newState=s;newState.actors.clear();newLast=s;
        for(const auto& [id,a]:s.actors){auto old=p.actors.find(id);
            if(old!=p.actors.end() && p.actorValid(old->second))replacement[id]=old->second;
            else {auto owned=newActors.emplace(id,Impl::Actor{}).first;
                auto slot=replacement.emplace(id,Impl::Actor{}).first;
                owned->second=p.create(a);slot->second=owned->second;}}
    }catch(const std::exception& e){for(const auto& [id,a]:newActors)if(a.pose)p.storage->destroy(a);return p.fail(e.what());}
    for(const auto& [id,a]:replacement){
        (void)a.pose->restore(staged.actors.at(id).pose);
        a.blocks->swap(staged.actors.at(id).blocks);
    }
    for(const auto& [id,a]:p.actors)if(!replacement.contains(id) || replacement.at(id).pose!=a.pose)
        p.storage->destroy(a);
    p.actors.swap(replacement);p.state=std::move(newState);p.last=std::move(newLast);
    p.outgoing.clear();p.mutations.clear();p.faulted=false;p.accessFailed=false;p.error.clear();return true;
}
std::uint64_t DeterministicSession::nextTick() const {return _impl->state.nextTick;}
std::uint32_t DeterministicSession::inputVersion() const {return _impl->config.inputVersion;}
math::DetFloat32 DeterministicSession::fixedStep() const {return _impl->dt;}
bool DeterministicSession::sealed() const {return _impl->sealed;}
bool DeterministicSession::faulted() const {return _impl->faulted;}
const std::string& DeterministicSession::error() const {return _impl->error;}
const std::vector<std::uint8_t>& DeterministicSession::manifest() const {return _impl->state.manifest;}
Entity* DeterministicSession::presentationEntity(SimEntityId id) const {
    if (_impl->validating || !_impl->storage->owns(this)) return nullptr;
    auto it=_impl->actors.find(id);return it!=_impl->actors.end()?_impl->storage->presentation(it->second):nullptr;
}
DetTickContext::DetTickContext(DeterministicSession& session,std::uint32_t system):_session(session),_system(system) {}
std::uint64_t DetTickContext::tick() const {return _session._impl->state.nextTick;}
math::DetFloat32 DetTickContext::dt() const {return _session._impl->dt;}
const DetTickInput& DetTickContext::input() const {return _session._impl->input;}
std::span<const DetTickCommand> DetTickContext::events() const {return _session._impl->state.pendingEvents;}
std::vector<SimEntityId> DetTickContext::entities() const {std::vector<SimEntityId> v;for(const auto& [id,a]:_session._impl->actors)v.push_back(id);return v;}
DetSimTransformComponent& DetTickContext::pose(SimEntityId id) {return *_session._impl->actors.at(id).pose;}
std::span<std::uint64_t> DetTickContext::words(SimEntityId id,std::uint32_t schema) {
    auto& p=*_session._impl;if(p.schemas.at(schema).typed){p.accessFailed=true;p.fail("Typed schema requires typed access");throw std::logic_error(p.error);}
    return (*p.actors.at(id).blocks).at(schema);
}
std::span<std::uint64_t> DetTickContext::globals(std::uint32_t schema) {
    auto& p=*_session._impl;if(p.schemas.at(schema).typed){p.accessFailed=true;p.fail("Typed schema requires typed access");throw std::logic_error(p.error);}
    return p.state.globals.at(schema);
}
DetStateValue DetTickContext::readValue(SimEntityId id,std::uint32_t schema,std::uint32_t field,DetStateType type) {
    auto& p=*_session._impl;
    try {
        const auto& descriptor=p.schemas.at(schema).typed;if(!descriptor)throw std::invalid_argument("Not a typed state schema");
        const auto& words=id?(*p.actors.at(id).blocks).at(schema):p.state.globals.at(schema);
        auto value=p.schemas.at(schema).layout->read(words,field);
        if(detStateType(value)!=type)throw std::invalid_argument("Typed state field type mismatch");return value;
    }catch(const std::exception& e){p.accessFailed=true;p.fail("Typed state read [entity="+std::to_string(id)+", schema="+std::to_string(schema)+", field="+std::to_string(field)+"]: "+e.what());throw std::runtime_error(p.error);}
}
void DetTickContext::writeValue(SimEntityId id,std::uint32_t schema,std::uint32_t field,const DetStateValue& value) {
    auto& p=*_session._impl;
    try {
        const auto& descriptor=p.schemas.at(schema).typed;if(!descriptor)throw std::invalid_argument("Not a typed state schema");
        auto& words=id?(*p.actors.at(id).blocks).at(schema):p.state.globals.at(schema);
        p.schemas.at(schema).layout->write(words,field,value);
    }catch(const std::exception& e){p.accessFailed=true;p.fail("Typed state write [entity="+std::to_string(id)+", schema="+std::to_string(schema)+", field="+std::to_string(field)+"]: "+e.what());throw std::runtime_error(p.error);}
}
math::pcg32_state& DetTickContext::random(std::uint32_t stream) {return _session._impl->state.randomStreams.at(stream);}
void DetTickContext::spawn(SimEntityId id,DetActorState a) {
    auto& p=*_session._impl;if(p.mutations.size()>=maxCommands)throw std::length_error("Structural command limit");p.defaults(a);p.mutations.push_back({id,true,std::move(a)});
}
void DetTickContext::despawn(SimEntityId id) {
    auto& p=*_session._impl;if(p.mutations.size()>=maxCommands)throw std::length_error("Structural command limit");p.mutations.push_back({id,false,{}});
}
void DetTickContext::emit(std::uint32_t type,std::span<const std::uint8_t> payload) {
    auto& p=*_session._impl;if(p.outgoing.size()>=maxCommands || payload.size()>maxInputBytes || _sequence==UINT32_MAX)throw std::length_error("Event limit");
    p.outgoing.push_back({_system,_sequence++,type,{payload.begin(),payload.end()}});
}
} // namespace ayt::entity
