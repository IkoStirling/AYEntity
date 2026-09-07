// TestMain.cpp - AYEntity Test Entry Point

#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include <AYTest.h>

#include <cstdio>

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    ayt::entity::bootstrapModule();
    if (argc > 1) {
        return ayt::test::runSuite(argv[1]);
    }
    return ayt::test::runAllTests("AYEntity");
}
