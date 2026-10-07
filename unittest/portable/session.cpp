#include "../DetExtendedCollisionScenario.h"
#include "../DetTypedSessionScenario.h"
#include <AYEntity/DeterministicLockstep.h>
#include <iostream>
#include <cfenv>
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif
using namespace ayt::entity;
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int main(){try {
    std::fesetround(FE_UPWARD);
#if defined(_M_X64) || defined(__x86_64__)
    _mm_setcsr(_mm_getcsr()|0x8040u);
#endif
    const DetSessionConfig config{1,1,1,64,0};
    for(bool extended:{false,true}) {
        DeterministicSession a(config),b(config);
        require(extended?detextendedcollision_scenario::configure(a):detcollision_scenario::configure(a),"configure a");
        require(extended?detextendedcollision_scenario::configure(b,true):detcollision_scenario::configure(b,true),"configure b");
        require(a.manifest()==b.manifest(),"assembly manifest");
        std::optional<DetSessionCheckpoint> restart;
        for(unsigned tick=0;tick<10000;++tick){
            require(a.advance(detcollision_scenario::input(tick)),a.error().c_str());
            require(b.advance(detcollision_scenario::input(tick,true)),b.error().c_str());
            auto cp=*a.checkpoint();require(encodeDetCheckpoint(cp)==encodeDetCheckpoint(*b.checkpoint()),"pair full bytes");
            require(extended?detextendedcollision_scenario::matchesGolden(cp):detcollision_scenario::matchesGolden(cp),"independent geometry/event oracle");
            if(tick==4999)restart=cp;
        }
        const auto expected=*a.checkpoint();require(a.restore(*restart),"restore 5000");
        for(unsigned tick=5000;tick<10000;++tick)require(a.advance(detcollision_scenario::input(tick,true)),"replay canonical inputs");
        require(encodeDetCheckpoint(expected)==encodeDetCheckpoint(*a.checkpoint()),"restore/replay bytes");
        const auto hash=detCheckpointHash(expected);
        require(hash==(extended?0x0e0940a868bb4d4dull:0x048c6e67f0d15cd8ull),"existing Host golden hash");
        auto bad=expected;bad.actors.begin()->second.pose.profileVersion=7;
        require(!a.restore(bad) && encodeDetCheckpoint(*a.checkpoint())==encodeDetCheckpoint(expected),"invalid restore atomicity");
        std::cout<<"PASS actual Session/collision profile "<<(extended?2:1)<<": 10000 ticks + restore/replay 5000, full bytes and independent golden "<<std::hex<<hash<<std::dec<<'\n';
    }
    DeterministicSession logic(config);require(logic.registerLogicProfile(7,1,9),"logic identity");require(logic.seal(),"manifest4");
    DetSessionCheckpoint decoded;std::string error;require(decodeDetCheckpoint(encodeDetCheckpoint(*logic.checkpoint()),decoded,error),"manifest4 codec");
    DeterministicSession mismatch(config);require(mismatch.registerLogicProfile(7,1,10) && mismatch.seal(),"second logic");
    require(!mismatch.restore(decoded),"logic mismatch rejection");
    std::cout<<"PASS logic identity manifest4 and incompatible restore rejection\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;} }
