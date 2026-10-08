// design reference: AYEntity/design.md Stage19; example application-owned factory.
#include "../DetTypedSessionScenario.h"
#include <AYEntity/DeterministicReplayRegression.h>
#include <thread>
#include <chrono>
int main(int argc,char** argv) {
#ifdef AY_REPLAY_TEST_NO_REPORT
    return 0; // A successful process exit without the runner protocol is not verification.
#else
    return ayt::entity::DetReplayRegression::runnerMain(argc,argv,[]{
#ifdef AY_REPLAY_TEST_SLEEP
        std::this_thread::sleep_for(std::chrono::seconds(2));
#endif
        auto session=std::make_unique<ayt::entity::DeterministicSession>(ayt::entity::DetSessionConfig{1,1,1,64,0});
#ifdef AY_REPLAY_TEST_FAULT
        constexpr bool fault=true; // Deliberately undeclared logic change, same manifest.
#else
        constexpr bool fault=false;
#endif
        if(!dettyped_scenario::configure(*session,true,fault,true,96))throw std::runtime_error(session->error());
        return session;
    });
#endif
}
