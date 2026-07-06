#pragma once
// AYResourceAnimationAdapter.h — Phase 1 AN-03 wiring layer.
// Converts AYResource's CPU-side `ISkeleton` / `IAnimation` into the
// runtime types AYAnimation's `Skeleton` / `AnimationPlayer` consume.
// Lives in AYEntity (not AYAnimation) because AYAnimation does not link
// AYResource — the adapter is only used by AnimationSystem and the
// unit tests under AYEntity/unittest/.

#include <ayanimation/Animation.h>
#include <ayanimation/Skeleton.h>

#include <string>

namespace ayt::entity::adapter
{

// Read `path` via ResourceManager::instance().load<resource::ISkeleton>
// and populate `out` with bone hierarchy + parallel local TRS arrays.
// Returns false on load failure or empty skeleton. Honors the
// parent-before-child invariant the loaders guarantee (AnimationPlayer
// requires this for correct world-matrix accumulation).
bool loadSkeleton(const std::string& path, ayt::anim::Skeleton& out);

// Read `path` via ResourceManager::instance().load<resource::IAnimation>
// and populate `out`. **Time-unit conversion**: AYResource stores
// `times` in raw ticks; this adapter normalizes to seconds by dividing
// each value by `getTicksPerSecond()`. AnimationPlayer + KeySampler
// operate in seconds.
bool loadAnimation(const std::string& path, ayt::anim::Animation& out);

} // namespace ayt::entity::adapter