// TestMain.cpp - AYEntity Test Entry Point

#include <AYEntity.h>
#include <AYEntityModule.h>
#include <AYTest.h>

int main(int argc, char** argv) {
    ayt::entity::bootstrapModule();
    if (argc > 1) {
        return ayt::test::runSuite(argv[1]);
    }
    return ayt::test::runAllTests("AYEntity");
}