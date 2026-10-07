#include <AYEntity/EntityModule.h>
#include <AYTest.h>
int main(int argc,char** argv) {
    if(!ayt::entity::registerEntityCoreComponents(ayt::entity::ComponentRegistry::instance()))return 1;
    return ayt::test::runTests("AYEntityDeterministicCollision",argc,argv);
}
