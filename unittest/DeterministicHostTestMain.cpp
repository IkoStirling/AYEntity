#include <AYEntity/EntityModule.h>
#include <AYEntity/ComponentRegistry.h>
#include <AYGameLoop.h>
#include <AYTest.h>
int main(int argc, char** argv) {
    if (!ayt::entity::registerEntityCoreComponents(ayt::entity::ComponentRegistry::instance())) return 1;
    const auto result=ayt::test::runTests("AYEntityDeterministicHost",argc,argv);
    ayt::game::GameLoop::instance().shutdown();
    return result;
}
