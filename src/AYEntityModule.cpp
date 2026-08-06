#include "AYEntityModule.h"
#include "AYAnimationSystem.h"
#include "AYSkinnedMeshRenderSystem.h"
#include "AYStateMachineSystem.h"
#include "AYWorld.h"

#include <cstdio>
#include <cstring>

namespace ayt::entity
{

namespace
{

// GL-01: idempotent re-registration helper. The original
// `static bool bootstrapped` guard could only fire once per process,
// but World::shutdown() clears _systems and a subsequent bootstrap
// would see an empty world and re-register cleanly. The complication
// is that test TUs (SystemTest.cpp) use the AY_SYSTEM() macro to
// register helper systems (CounterSystem, HealthProcessSystem) via
// file-scope static initializers — those are NOT cleared by
// World::shutdown. Looking at systemCount() alone is therefore
// wrong (it counts the test helpers too), and looking at a static
// flag is wrong (it never resets). The right check is "is the
// specific system I am about to register already present?" — if
// so skip, otherwise register. The register* functions themselves
// are also idempotent now (no internal static guards).
bool hasSystemNamed(const World& world, const char* name)
{
    for (size_t i = 0; i < world.systemCount(); ++i) {
        const char* existing = world.getSystemNameAt(i);
        if (existing != nullptr && std::strcmp(existing, name) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

void bootstrapEntityCore()
{
    registerEntitySubSystem();
    registerEntityComponents();
}

void bootstrapModule()
{
    World& world = World::instance();

    // Phase 1 AN-03: AnimationSystem (priority 450) ticks before any
    // render system so per-bone skin matrices are fresh when the
    // renderer reads them. Both render systems share priority 500;
    // scene-builder chain order = registration order, so registering
    // SkinnedMeshRenderSystem before RenderSystem makes it run
    // first (the order is semantically irrelevant — both consume
    // different entities — but logging the order helps debugging).
    if (!hasSystemNamed(world, "AnimationSystem")) {
        registerAnimationSystem();
    }
    // P3.1 (2026-08-06) — StateMachineSystem (priority 460) ticks AFTER
    // AnimationSystem (450); transitions decide which clip plays next frame.
    if (!hasSystemNamed(world, "StateMachineSystem")) {
        registerStateMachineSystem();
    }
    if (!hasSystemNamed(world, "SkinnedMeshRenderSystem")) {
        registerSkinnedMeshRenderSystem();
    }
    if (!hasSystemNamed(world, "RenderSystem")) {
        registerRenderSystem();
    }
    bootstrapEntityCore();

    static bool loggedOnce = false;
    if (!loggedOnce) {
        std::fprintf(stderr, "[AYEntity] module bootstrap complete\n");
        loggedOnce = true;
    }
}

} // namespace ayt::entity
