#pragma once
#include <AYEntity/DeterministicDebug2D.h>
#include <type_traits>
namespace ayt::entity {
struct DetDebugAccess {
    template<class T> static void retain(DeterministicSession& session,const T& config) noexcept {
        try {
            if(!session._debugPolicies2D)session._debugPolicies2D=std::make_shared<DetDebugPolicies2D>();
            auto& policies=*session._debugPolicies2D;
            auto& values=[&]() -> auto& {
                if constexpr(std::is_same_v<T,DetCollision2DConfig>)return policies.collisions;
                else if constexpr(std::is_same_v<T,DetCharacterController2DConfig>)return policies.characters;
                else if constexpr(std::is_same_v<T,DetPathMotion2DConfig>)return policies.paths;
                else if constexpr(std::is_same_v<T,DetInteraction2DConfig>)return policies.interactions;
                else if constexpr(std::is_same_v<T,DetCharacterRespawn2DConfig>)return policies.respawns;
                else return policies.respawnFlows;
            }();
            if(values.size()<64)values.push_back(std::make_shared<const T>(config));
        } catch(...) { /* Diagnostic allocation never changes successful registration. */ }
    }
};
}
