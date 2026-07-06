#include "AYEntityModule.h"
#include "AYAnimationSystem.h"
#include "AYSkinnedMeshRenderSystem.h"



#include <cstdio>



namespace ayt::entity

{



void bootstrapModule()

{

    static bool bootstrapped = false;

    if (bootstrapped) {

        return;

    }

    bootstrapped = true;



    registerEntityComponents();

    // Phase 1 AN-03: AnimationSystem (priority 450) ticks before any
    // render system so per-bone skin matrices are fresh when the
    // renderer reads them. Both render systems share priority 500;
    // scene-builder chain order = registration order, so registering
    // SkinnedMeshRenderSystem before RenderSystem makes it run
    // first (the order is semantically irrelevant — both consume
    // different entities — but logging the order helps debugging).
    registerAnimationSystem();
    registerSkinnedMeshRenderSystem();
    registerRenderSystem();
    registerEntitySubSystem();



    std::fprintf(stderr, "[AYEntity] module bootstrap complete\n");

}



} // namespace ayt::entity

