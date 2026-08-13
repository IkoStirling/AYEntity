// AYTilemapAnimationTickSystem.cpp — CM-3 (2026-08-11) shell, filled
// CM-5 (2026-08-12): drives the per-path animation runtime with the
// wall clock (QPC). One ensure() per tilemap path (the runtime's per-path
// cache), then one tick() per entity — tick() is a no-op for the second
// entity on the same path in the same frame (deltaUs == 0), so shared
// tilemaps advance once regardless of entity count.

#include "AYTilemapAnimationTickSystem.h"

#include "AYEntity.h"
#include "AYTilemapAnimationRuntime.h"
#include "AYWorld.h"

#include "components/AYTilemapComponent.h"
#include "components/AYTransformComponent.h"

#include <assetsDefs/IAYTilemap.h>
#include <AYCore.h>
#include <AYResourceManager.h>

namespace ayt::entity
{

void TilemapAnimationTickSystem::onUpdate(float /*dt*/)
{
    const int64_t nowUs = static_cast<int64_t>(ayt::performanceNowUs());
    TilemapAnimationRuntime& runtime = TilemapAnimationRuntime::instance();
    World& world = World::instance();

    for (Entity* entity : world.query<Transform, TilemapComponent>()) {
        if (entity == nullptr) {
            continue;
        }
        TilemapComponent* tm = entity->getComponent<TilemapComponent>();
        if (tm == nullptr || !tm->isValid()) {
            continue;
        }

        // Warm path: the runtime already holds the table for this path —
        // no resource load at all. Cold path: lazy-load (L-16 marker —
        // failure => skip the entity, no exception; the render system
        // logs the failure on early frames).
        TilemapAnimationRuntimeEntry* entry = runtime.find(tm->tilemapPath);
        if (entry == nullptr) {
            auto tilemap = ayt::resource::ResourceManager::instance()
                               .load<ayt::resource::ITilemap>(tm->tilemapPath);
            if (!tilemap) {
                continue;
            }
            entry = runtime.ensure(tm->tilemapPath, *tilemap);
            if (entry == nullptr) {
                continue;
            }
        }

        runtime.tick(*entry, nowUs);
    }
}

void registerTilemapAnimationTickSystem()
{
    World::instance().registerSystem<TilemapAnimationTickSystem>(
        TilemapAnimationTickSystem::kPriority);
}

} // namespace ayt::entity
