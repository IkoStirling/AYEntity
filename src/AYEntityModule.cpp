#include "AYEntityModule.h"



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

    registerRenderSystem();

    registerEntitySubSystem();



    std::fprintf(stderr, "[AYEntity] module bootstrap complete\n");

}



} // namespace ayt::entity

