// AYTilemapStreamingSystem.cpp — CM-3 (2026-08-11): empty shell.
//
// Reserved registration slot (priority 430) for the chunk-source /
// visibility streaming pipeline. See the header for the fill-in
// contract; the empty body mirrors the GBufferPass shell precedent.

#include "AYEntity/TilemapStreamingSystem.h"

#include "AYEntity/World.h"

namespace ayt::entity
{

void registerTilemapStreamingSystem()
{
    World::instance().registerSystem<TilemapStreamingSystem>(
        TilemapStreamingSystem::kPriority);
}

} // namespace ayt::entity
