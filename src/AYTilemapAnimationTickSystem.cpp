// AYTilemapAnimationTickSystem.cpp — CM-3 (2026-08-11): empty shell.
//
// Reserved registration slot (priority 460) for the per-frame tilemap
// animation table tick. See the header for the fill-in contract; the
// empty body mirrors the GBufferPass shell precedent.

#include "AYTilemapAnimationTickSystem.h"

#include "AYWorld.h"

namespace ayt::entity
{

void registerTilemapAnimationTickSystem()
{
    World::instance().registerSystem<TilemapAnimationTickSystem>(
        TilemapAnimationTickSystem::kPriority);
}

} // namespace ayt::entity
