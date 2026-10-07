#include <AYEntity/DeterministicSession.h>
#include <AYEntity.h>
#include <AYEntity/components/DetSimStateComponent.h>
#include "detail/DetSessionWire.h"
#include <set>

namespace ayt::entity {
using namespace detwire;
struct DeterministicSession::Impl {
    struct Schema { DetStateSchema descriptor;std::vector<std::uint64_t> defaults;std::optional<DetTypedStateSchema> typed; };
    struct SystemEntry { std::uint32_t id;std::int32_t priority;System callback; };
    struct Actor { Entity* entity;std::uint32_t runtimeId; };
    struct Mutation { SimEntityId id;bool spawn;DetActorState initial; };
    World& world;DetSessionConfig config;math::DetFloat32 dt;
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
    Impl(World& w,DetSessionConfig c):world(w),config(c),dt(math::DetFloat32::fromUInt(c.stepNumerator)/math::DetFloat32::fromUInt(c.stepDenominator)) {}
    bool fail(std::string e) const { error=std::move(e);return false; }
    bool configuring() {
        if (!world.isInitialized() || world._deterministicOwner != owner)
            return fail("Session lost World ownership");
        return !sealed || fail("Session configuration is sealed");
    }
    bool actorValid(const Actor& a) const {
        if(!a.entity)return false;
        if(world.findEntity(a.runtimeId)!=a.entity)return false;
        if(!a.entity->getComponent<DetSimTransformComponent>() || !a.entity->getComponent<DetSimStateComponent>() || !a.entity->getComponent<Transform>())return false;
        for(auto* c:a.entity->getComponents())
            if(dynamic_cast<DetSimTransformComponent*>(c)==nullptr && dynamic_cast<DetSimStateComponent*>(c)==nullptr && dynamic_cast<Transform*>(c)==nullptr)return false;
        return true;
    }
    bool validBlocks(const DetStateBlocks& b,bool all) const {
        if(all && b.size()!=schemas.size())return false;
        for(const auto& [id,words]:b){auto it=schemas.find(id);if(it==schemas.end() || words.size()!=it->second.defaults.size())return false;
            if(it->second.typed)try {
                std::size_t offset=0;for(const auto& field:it->second.typed->fields){const auto width=encodeDetStateValue(field.initial).size();
                    (void)decodeDetStateValue(detStateType(field.initial),std::span(words).subspan(offset,width));offset+=width;}
            }catch(...){return false;}}
        return true;
    }
    bool validActor(const DetActorState& a) const {
        DetSimTransformComponent test;return test.restore(a.pose) && validBlocks(a.blocks,true);
    }
    void defaults(DetActorState& a) const {
        for(const auto& [id,s]:schemas)if(!a.blocks.contains(id))a.blocks[id]=s.defaults;
    }
    std::vector<std::uint8_t> buildManifest() const {
        const bool typed=std::any_of(schemas.begin(),schemas.end(),[](const auto& entry){return entry.second.typed.has_value();});
        Writer w;w.u32(manifestMagic);w.u32(typed?2:1);
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
        return w.finish();
    }
    std::optional<DetSessionCheckpoint> capture() const {
        if(!sealed)return fail("Session is not sealed"),std::nullopt;
        DetSessionCheckpoint s=state;s.actors.clear();
        for(const auto& [id,a]:actors){
            if(!actorValid(a))return fail("Managed entity topology changed"),std::nullopt;
            DetActorState value{a.entity->getComponent<DetSimTransformComponent>()->snapshot(),a.entity->getComponent<DetSimStateComponent>()->blocks};
            if(!validActor(value))return fail("Invalid registered Sim state"),std::nullopt;
            s.actors.emplace(id,std::move(value));
        }
        return s;
    }
    Actor create(const DetActorState& value) {
        if(world._nextEntityId==UINT32_MAX)throw std::runtime_error("World runtime identity exhausted");
        auto* e=world.createEntityInternal();
        try {
            auto* p=e->addComponent<DetSimTransformComponent>();auto* b=e->addComponent<DetSimStateComponent>();auto* t=e->addComponent<Transform>();
            if(!p || !b || !t)throw std::runtime_error("Core session component types not registered");
            if(!p->restore(value.pose))throw std::runtime_error("Invalid actor pose");
            b->blocks=value.blocks;
            return {e,e->getId()};
        }catch(...){world.destroyEntityInternal(e);throw;}
    }
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
        try { (void)encodeDetCheckpoint(s); }catch(const std::exception& e){return fail(e.what());}return true;
    }
};
DeterministicSession::DeterministicSession(World& world,DetSessionConfig config):_impl(std::make_unique<Impl>(world,config)) {
    if(!world.isInitialized() || world._deterministicOwner)throw std::invalid_argument("Session needs an initialized unclaimed World");
    if(!config.applicationVersion || !config.inputVersion || !config.stepNumerator || !config.stepDenominator || !_impl->dt.isFinite() || !(_impl->dt>math::DetFloat32{}))
        throw std::invalid_argument("Invalid session version/fixed step");
    if(!World::isComponentTypeRegistered<DetSimStateComponent>() || !World::isComponentTypeRegistered<DetSimTransformComponent>() || !World::isComponentTypeRegistered<Transform>())
        throw std::invalid_argument("Register Core component types before session construction");
    for(const auto& s:world._systems)if(s->getLane()==SystemLane::Sim)throw std::invalid_argument("Legacy World Sim systems conflict with session owner");
    for(auto* e:world.getAllEntities())if(e->hasComponent<SimTransformComponent>() || e->hasComponent<DetSimTransformComponent>() || e->hasComponent<DetSimStateComponent>())
        throw std::invalid_argument("Existing Sim authority is not managed by this session");
    world._deterministicOwner=this;
    _impl->owner=this;
}
DeterministicSession::~DeterministicSession() {
    auto& p=*_impl;if(p.world._deterministicOwner!=this)return;
    for(const auto& [id,a]:p.actors)if(a.entity && p.world.findEntity(a.runtimeId)==a.entity)p.world.destroyEntityInternal(a.entity);
    p.world._deterministicOwner=nullptr;
}
bool DeterministicSession::registerSchema(DetStateSchema schema,std::vector<std::uint64_t> defaults) {
    auto& p=*_impl;if(!p.configuring())return false;
    if(schema.id<2 || schema.id==UINT32_MAX || !schema.version || schema.fields.empty() || schema.fields.size()>maxFields
        || p.schemas.size()>=maxSchemas || p.schemas.contains(schema.id) || !p.actors.empty())return p.fail("Invalid/late state schema");
    if(!std::is_sorted(schema.fields.begin(),schema.fields.end()) || schema.fields.front()==0
        || std::adjacent_find(schema.fields.begin(),schema.fields.end())!=schema.fields.end())return p.fail("Field IDs must be unique increasing nonzero values");
    if(defaults.empty())defaults.resize(schema.fields.size());
    if(defaults.size()!=schema.fields.size())return p.fail("Default field count mismatch");
    p.schemas.emplace(schema.id,Impl::Schema{std::move(schema),std::move(defaults),std::nullopt});return true;
}
bool DeterministicSession::registerTypedSchema(DetTypedStateSchema schema) {
    auto& p=*_impl;if(!p.configuring())return false;
    if(p.schemas.size()>=maxSchemas || p.schemas.contains(schema.id) || !p.actors.empty())return p.fail("Duplicate/late typed state schema");
    try {
        auto defaults=detStateDefaults(schema);DetStateSchema descriptor{schema.id,schema.version,{}};
        for(const auto& field:schema.fields)descriptor.fields.push_back(field.id);
        p.schemas.emplace(schema.id,Impl::Schema{std::move(descriptor),std::move(defaults),std::move(schema)});return true;
    }catch(const std::exception& e){return p.fail(e.what());}
}
bool DeterministicSession::registerSystem(std::uint32_t id,std::int32_t priority,System callback) {
    auto& p=*_impl;if(!p.configuring())return false;
    if(!id || !callback || p.systems.size()>=maxSchemas || std::any_of(p.systems.begin(),p.systems.end(),[&](const auto& s){return s.id==id;}))return p.fail("Invalid/duplicate system ID");
    p.systems.push_back({id,priority,std::move(callback)});return true;
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
    auto slot=p.actors.emplace(id,Impl::Actor{nullptr,0}).first;
    try { slot->second=p.create(initial);return true; }
    catch(const std::exception& e){p.actors.erase(slot);return p.fail(e.what());}
}
bool DeterministicSession::seal() {
    auto& p=*_impl;
    if(p.world._deterministicOwner!=this)return p.fail("Session lost World ownership");
    if(p.sealed)return true;
    std::sort(p.systems.begin(),p.systems.end(),[](const auto& a,const auto& b){return std::pair{a.priority,a.id}<std::pair{b.priority,b.id};});
    try {p.state.manifest=p.buildManifest();}catch(const std::exception& e){return p.fail(e.what());}
    if(p.state.manifest.size()>maxInputBytes)return p.fail("Session manifest size limit");
    p.sealed=true;
    auto s=p.capture();if(!s || !p.validate(*s)){p.sealed=false;return false;}p.last=std::move(*s);p.error.clear();return true;
}
bool DeterministicSession::advance(DetTickInput input) {
    auto& p=*_impl;if(!p.sealed || p.faulted || p.inTick || p.world._deterministicOwner!=this)return p.fail("Session not ready or lost tick ownership");
    if(input.tick!=p.state.nextTick || input.version!=p.config.inputVersion || input.tick==UINT64_MAX)return p.fail("Unexpected input tick/version");
    if(!canonicalizeDetInput(input,p.error))return false;
    auto before=p.capture();if(!before)return false;
    if(firstDetDifference(p.last,*before))return p.fail("Registered Sim state changed outside the session tick");
    p.input=std::move(input);p.inTick=true;p.accessFailed=false;p.mutations.clear();p.outgoing.clear();
    try {
        for(const auto& [id,a]:p.actors)a.entity->getComponent<DetSimTransformComponent>()->beginSimulationStep();
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
        for(const auto& m:p.mutations){if(m.spawn){auto slot=p.actors.emplace(m.id,Impl::Actor{nullptr,0}).first;slot->second=p.create(m.initial);}
            else{auto a=p.actors.at(m.id);p.world.destroyEntityInternal(a.entity);p.actors.erase(m.id);}}
        p.state.retiredIds.assign(retired.begin(),retired.end());p.state.pendingEvents=std::move(events.commands);++p.state.nextTick;
        auto after=p.capture();if(!after || !p.validate(*after))throw std::runtime_error(p.error);
        p.last=std::move(*after);p.inTick=false;p.error.clear();return true;
    }catch(const std::exception& e){p.faulted=true;p.inTick=false;return p.fail(e.what());}
    catch(...){p.faulted=true;p.inTick=false;return p.fail("Unknown system failure");}
}
std::optional<DetSessionCheckpoint> DeterministicSession::checkpoint() const {
    auto& p=*_impl;if(p.inTick || p.faulted || p.world._deterministicOwner!=this){p.fail("Checkpoint requires a quiescent valid session");return std::nullopt;}
    auto s=p.capture();if(s && !p.validate(*s))return std::nullopt;
    if(s && firstDetDifference(p.last,*s)){p.fail("Registered Sim state changed outside the session tick");return std::nullopt;}
    return s;
}
bool DeterministicSession::restore(const DetSessionCheckpoint& s) {
    auto& p=*_impl;if(!p.sealed || p.inTick || p.world._deterministicOwner!=this)return p.fail("Restore requires a sealed quiescent session");
    if(!p.validate(s))return false;
    // Stage all allocations before mutating retained poses or discarding actors.
    DetSessionCheckpoint staged,newState,newLast;std::map<SimEntityId,Impl::Actor> replacement,newActors;
    try {
        staged=s;newState=s;newState.actors.clear();newLast=s;
        for(const auto& [id,a]:s.actors){auto old=p.actors.find(id);
            if(old!=p.actors.end() && p.actorValid(old->second))replacement[id]=old->second;
            else {auto owned=newActors.emplace(id,Impl::Actor{nullptr,0}).first;
                auto slot=replacement.emplace(id,Impl::Actor{nullptr,0}).first;
                owned->second=p.create(a);slot->second=owned->second;}}
    }catch(const std::exception& e){for(const auto& [id,a]:newActors)if(a.entity)p.world.destroyEntityInternal(a.entity);return p.fail(e.what());}
    for(const auto& [id,a]:replacement){
        (void)a.entity->getComponent<DetSimTransformComponent>()->restore(staged.actors.at(id).pose);
        a.entity->getComponent<DetSimStateComponent>()->blocks.swap(staged.actors.at(id).blocks);
    }
    for(const auto& [id,a]:p.actors)if(!replacement.contains(id) || replacement.at(id).entity!=a.entity)
        if(a.entity && p.world.findEntity(a.runtimeId)==a.entity)p.world.destroyEntityInternal(a.entity);
    p.actors.swap(replacement);p.state=std::move(newState);p.last=std::move(newLast);
    p.outgoing.clear();p.mutations.clear();p.faulted=false;p.accessFailed=false;p.error.clear();return true;
}
std::uint64_t DeterministicSession::nextTick() const {return _impl->state.nextTick;}
math::DetFloat32 DeterministicSession::fixedStep() const {return _impl->dt;}
bool DeterministicSession::sealed() const {return _impl->sealed;}
bool DeterministicSession::faulted() const {return _impl->faulted;}
const std::string& DeterministicSession::error() const {return _impl->error;}
const std::vector<std::uint8_t>& DeterministicSession::manifest() const {return _impl->state.manifest;}
Entity* DeterministicSession::presentationEntity(SimEntityId id) const {
    if (_impl->world._deterministicOwner != this) return nullptr;
    auto it=_impl->actors.find(id);return it!=_impl->actors.end() && _impl->world.findEntity(it->second.runtimeId)==it->second.entity?it->second.entity:nullptr;
}
DetTickContext::DetTickContext(DeterministicSession& session,std::uint32_t system):_session(session),_system(system) {}
std::uint64_t DetTickContext::tick() const {return _session._impl->state.nextTick;}
math::DetFloat32 DetTickContext::dt() const {return _session._impl->dt;}
const DetTickInput& DetTickContext::input() const {return _session._impl->input;}
std::span<const DetTickCommand> DetTickContext::events() const {return _session._impl->state.pendingEvents;}
std::vector<SimEntityId> DetTickContext::entities() const {std::vector<SimEntityId> v;for(const auto& [id,a]:_session._impl->actors)v.push_back(id);return v;}
DetSimTransformComponent& DetTickContext::pose(SimEntityId id) {return *_session._impl->actors.at(id).entity->getComponent<DetSimTransformComponent>();}
std::span<std::uint64_t> DetTickContext::words(SimEntityId id,std::uint32_t schema) {
    auto& p=*_session._impl;if(p.schemas.at(schema).typed){p.accessFailed=true;p.fail("Typed schema requires typed access");throw std::logic_error(p.error);}
    return p.actors.at(id).entity->getComponent<DetSimStateComponent>()->blocks.at(schema);
}
std::span<std::uint64_t> DetTickContext::globals(std::uint32_t schema) {
    auto& p=*_session._impl;if(p.schemas.at(schema).typed){p.accessFailed=true;p.fail("Typed schema requires typed access");throw std::logic_error(p.error);}
    return p.state.globals.at(schema);
}
DetStateValue DetTickContext::readValue(SimEntityId id,std::uint32_t schema,std::uint32_t field,DetStateType type) {
    auto& p=*_session._impl;
    try {
        const auto& descriptor=p.schemas.at(schema).typed;if(!descriptor)throw std::invalid_argument("Not a typed state schema");
        const auto& words=id?p.actors.at(id).entity->getComponent<DetSimStateComponent>()->blocks.at(schema):p.state.globals.at(schema);
        auto value=readDetStateValue(*descriptor,words,field);
        if(detStateType(value)!=type)throw std::invalid_argument("Typed state field type mismatch");return value;
    }catch(const std::exception& e){p.accessFailed=true;p.fail("Typed state read [entity="+std::to_string(id)+", schema="+std::to_string(schema)+", field="+std::to_string(field)+"]: "+e.what());throw std::runtime_error(p.error);}
}
void DetTickContext::writeValue(SimEntityId id,std::uint32_t schema,std::uint32_t field,const DetStateValue& value) {
    auto& p=*_session._impl;
    try {
        const auto& descriptor=p.schemas.at(schema).typed;if(!descriptor)throw std::invalid_argument("Not a typed state schema");
        auto& words=id?p.actors.at(id).entity->getComponent<DetSimStateComponent>()->blocks.at(schema):p.state.globals.at(schema);
        writeDetStateValue(*descriptor,words,field,value);
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
