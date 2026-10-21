#include <AYEntity/EntityModule.h>
#include <AYTest.h>
#include <cstdio>

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    ayt::entity::bootstrapModule();
    return ayt::test::runTests("AYEntity_RenderSubmissionTests", argc, argv);
}
