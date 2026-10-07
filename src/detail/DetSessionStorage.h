#pragma once
#include <AYEntity/DeterministicSession.h>
namespace ayt::entity {
struct DetActorMemory { DetSimTransformComponent pose; DetStateBlocks blocks; };
struct DetActorSlot {
    DetSimTransformComponent* pose=nullptr;
    DetStateBlocks* blocks=nullptr;
    Entity* entity=nullptr;
    std::uint32_t runtimeId=0;
    std::shared_ptr<DetActorMemory> memory;
};
// Private storage boundary. Both adapters execute the same session algorithms.
class DetSessionStorage {
public:
    virtual ~DetSessionStorage()=default;
    virtual void claim(DeterministicSession*)=0;
    virtual bool owns(const DeterministicSession*) const noexcept=0;
    virtual void release(const DeterministicSession*) noexcept=0;
    virtual DetActorSlot create(const DetActorState&)=0;
    virtual bool valid(const DetActorSlot&) const=0;
    virtual void destroy(const DetActorSlot&) noexcept=0;
    virtual Entity* presentation(const DetActorSlot&) const noexcept=0;
};
std::unique_ptr<DetSessionStorage> makeDetWorldStorage(World&);
}
