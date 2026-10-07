#include "detail/DetSessionStorage.h"
#include <AYEntity.h>
#include <AYEntity/components/DetSimStateComponent.h>
#include <stdexcept>
namespace ayt::entity {
class DetWorldStorage final : public DetSessionStorage {
    World& _world;
public:
    explicit DetWorldStorage(World& world):_world(world){}
    void claim(DeterministicSession* owner) override {
        if(!_world.isInitialized() || _world._deterministicOwner)throw std::invalid_argument("Session needs an initialized unclaimed World");
        if(!World::isComponentTypeRegistered<DetSimStateComponent>() || !World::isComponentTypeRegistered<DetSimTransformComponent>() || !World::isComponentTypeRegistered<Transform>())
            throw std::invalid_argument("Register Core component types before session construction");
        for(const auto& s:_world._systems)if(s->getLane()==SystemLane::Sim)throw std::invalid_argument("Legacy World Sim systems conflict with session owner");
        for(auto* e:_world.getAllEntities())if(e->hasComponent<SimTransformComponent>() || e->hasComponent<DetSimTransformComponent>() || e->hasComponent<DetSimStateComponent>())
            throw std::invalid_argument("Existing Sim authority is not managed by this session");
        _world._deterministicOwner=owner;
    }
    bool owns(const DeterministicSession* owner) const noexcept override {return _world.isInitialized() && _world._deterministicOwner==owner;}
    void release(const DeterministicSession* owner) noexcept override {if(_world._deterministicOwner==owner)_world._deterministicOwner=nullptr;}
    DetActorSlot create(const DetActorState& value) override {
        if(_world._nextEntityId==UINT32_MAX)throw std::runtime_error("World runtime identity exhausted");
        auto* e=_world.createEntityInternal();
        try {
            auto* p=e->addComponent<DetSimTransformComponent>();auto* b=e->addComponent<DetSimStateComponent>();auto* t=e->addComponent<Transform>();
            if(!p || !b || !t)throw std::runtime_error("Core session component types not registered");
            if(!p->restore(value.pose))throw std::runtime_error("Invalid actor pose");
            b->blocks=value.blocks;return {p,&b->blocks,e,e->getId(),{}};
        }catch(...){_world.destroyEntityInternal(e);throw;}
    }
    bool valid(const DetActorSlot& a) const override {
        if(!presentation(a) || a.entity->getComponent<DetSimTransformComponent>()!=a.pose) return false;
        auto* b=a.entity->getComponent<DetSimStateComponent>();
        if(!b || &b->blocks!=a.blocks || !a.entity->getComponent<Transform>())return false;
        for(auto* c:a.entity->getComponents())if(dynamic_cast<DetSimTransformComponent*>(c)==nullptr
            && dynamic_cast<DetSimStateComponent*>(c)==nullptr && dynamic_cast<Transform*>(c)==nullptr)return false;
        return true;
    }
    void destroy(const DetActorSlot& a) noexcept override {if(presentation(a))_world.destroyEntityInternal(a.entity);}
    Entity* presentation(const DetActorSlot& a) const noexcept override {return a.entity && _world.findEntity(a.runtimeId)==a.entity?a.entity:nullptr;}
};
std::unique_ptr<DetSessionStorage> makeDetWorldStorage(World& world){return std::make_unique<DetWorldStorage>(world);}
DeterministicSession::DeterministicSession(World& world,DetSessionConfig config)
    :DeterministicSession(makeDetWorldStorage(world),config){}
}
