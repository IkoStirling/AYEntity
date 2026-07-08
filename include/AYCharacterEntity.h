#pragma once
// AYCharacterEntity.h — Phase 1 ED-02 helper.
//
// Factored out of AYRenderer/demo/SuzanneSkinnedDemo::spawnSkinnedEntity
// so the AYEditor Play mode and other tools can construct the same
// four-component "skinned character" entity without copy-pasting the
// body. Mirrors the convention established in Phase 1 E-04: Mesh +
// Skeleton + Animation + Transform with `MeshComponent::skinned = true`.
//
// Pure data-construction helper — does NOT call into the renderer or
// the resource adapter. The caller (e.g. EditorPlayRuntime, a tool
// binary, or a test fixture) decides when to begin ticking. The
// SkeletonComponent owns its skeleton/player/skinMatrices state; the
// AnimationComponent pairs with it to drive playback. See
// `AYResourceAnimationAdapter.h` for how the .ayskel / .ayanm bytes
// flow into SkeletonComponent::skeleton / ::player.

#include <string>

namespace ayt::entity
{

class Entity;

// Construct an entity that draws a skinned mesh, evaluates a skeleton,
// and plays an animation clip. The four path strings are stored on the
// respective components verbatim — they are looked up at runtime by the
// resource adapter / animation system when the entity first ticks.
//
// Returns nullptr when World is uninitialized or entity creation fails
// (which only happens when the entity pool is exhausted — a hard
// resource error, not a content one). Empty strings are accepted; the
// components' own `isValid()` will report false and the render systems
// will skip the entity until paths are filled in.
Entity* spawnCharacterFromPaths(const std::string& meshPath,
                                const std::string& materialPath,
                                const std::string& skeletonPath,
                                const std::string& animationPath);

// Destroy an entity previously returned by `spawnCharacterFromPaths`.
// Equivalent to `World::instance().destroyEntity(entity)` but named for
// parallel structure with the spawn helper. Safe to call with nullptr
// (no-op). The SkeletonComponent destructor releases its heap-allocated
// `skinMatrices[]` array on the way out.
void destroyCharacter(Entity* entity);

} // namespace ayt::entity
