// TestMain.cpp - AYEntity Test Entry Point

#include <AYEntity.h>
#include <AYEntityModule.h>
#include <AYTest.h>

int main() {
    ayt::entity::bootstrapModule();
    return ayt::test::runAllTests("AYEntity");
}