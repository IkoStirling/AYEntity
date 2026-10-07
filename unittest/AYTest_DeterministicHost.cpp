#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include <AYGameLoop.h>
#include <AYTest.h>
#include <cfenv>
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif
namespace {
using namespace ayt::entity;
using D=ayt::math::DetFloat32;
using V=ayt::math::DetVec3;
using Q=ayt::math::DetQuaternion;
struct Probe { D dt{}; unsigned count=0; } probe;
class Motion final : public IDeterministicSystem {
public:
    const char* getName() const override { return "HostTypedMotion"; }
    void onDeterministicUpdate(D dt) override {
        probe.dt=dt; ++probe.count;
        for (auto* e : World::instance().getAllEntities())
            if (auto* sim=e->getComponent<DetSimTransformComponent>()) {
                (void)sim->translate(V::fromInts(3,-2,1)*dt);
                if (sim->rotationEnabled) (void)sim->integrateAngularVelocityLocal(V::fromInts(0,2,0),dt);
            }
    }
};
struct Session {
    ayt::game::GameLoop& loop=ayt::game::GameLoop::instance();
    DetSimTransformComponent* sim;
    bool prepared;
    Session() {
        loop.endHostedSession(); ayt::game::SubSystemRegistry::instance().clearAll();
        loop.setRenderThreadEnabled(false); loop.setParallelEnabled(false);
        (void)loop.setFixedTimestepRatio(1,64);
        loop.registerSubSystem(createEntitySubSystem().release());
        prepared=loop.prepareHostedSession(); probe={};
        registerEntityCoreSystems(); // Same post-initialize install used by EntityRuntimeModule.
        World::instance().registerSystem<Motion>(0, SystemLane::Sim);
        auto* e=World::instance().createEntity();
        sim=e->addComponent<DetSimTransformComponent>(); e->addComponent<Transform>();
    }
    ~Session() {
        loop.endHostedSession(); ayt::game::SubSystemRegistry::instance().clearAll();
        (void)loop.setFixedTimestepRatio(1,60); loop.setTimeScale(1);
    }
};
}
TEST_SUITE(DeterministicHost)
TEST_CASE(standard_entity_subsystem_receives_host_ratio_and_fixed_history) {
    Session s; CHECK_TRUE(s.prepared);
    s.loop.tickHostedFrame({1.0f/32.0f,1,1});
    CHECK(probe.count==2 && probe.dt.bits()==0x3c800000u);
    CHECK(s.sim->position==(V::fromInts(3,-2,1)*D::fromInt(1)/D::fromInt(32)));
    CHECK(s.sim->previousPosition==(V::fromInts(3,-2,1)/D::fromInt(64)));
}
TEST_CASE(entity_prefers_typed_context_without_native_round_trip_and_retains_legacy_fallback) {
    Session s; CHECK_TRUE(s.prepared); auto adapter=createEntitySubSystem();
    ayt::game::FrameContext context;
    context.fixedDeltaTime=0.5f; context.fixedStep=ayt::game::FixedTimestep::fromRatio(1,60);
    adapter->tick(ayt::game::FramePhase::FixedPrePhysics,context);
    CHECK(probe.dt.bits()==0x3c888889u);
    context.fixedStep.reset(); context.fixedDeltaTime=0.25f;
    adapter->tick(ayt::game::FramePhase::FixedPrePhysics,context);
    CHECK(probe.dt.bits()==0x3e800000u);
}
TEST_CASE(host_checkpoint_replay_keeps_bits_with_hostile_rounding_and_same_tick_inputs) {
    Session s; CHECK_TRUE(s.prepared);
    CHECK_TRUE(s.sim->setRotation(Q{}));
    const auto saved=s.sim->snapshot();
    for (unsigned i=0;i<128;++i) s.loop.stepOnce();
    const auto expected=s.sim->snapshot();
    CHECK_TRUE(s.sim->restore(saved));
    const auto rounding=std::fegetround();
#if defined(_M_X64) || defined(__x86_64__)
    const auto csr=_mm_getcsr();
#endif
    std::fesetround(FE_UPWARD);
#if defined(_M_X64) || defined(__x86_64__)
    _mm_setcsr(_mm_getcsr() | 0x8040u);
#endif
    const bool configured=s.loop.setFixedTimestepRatio(1,64);
    for (unsigned i=0;i<128;++i) s.loop.stepOnce();
    const auto actual=s.sim->snapshot();
    std::fesetround(rounding);
#if defined(_M_X64) || defined(__x86_64__)
    _mm_setcsr(csr);
#endif
    CHECK_TRUE(configured);
    CHECK(actual==expected);
}
TEST_CASE(standard_host_propagates_rotation_history_and_presents_without_feedback) {
    Session s; CHECK_TRUE(s.prepared); CHECK_TRUE(s.sim->setRotation(Q{}));
    s.loop.stepOnce(); const auto first=s.sim->snapshot();
    CHECK(first.rotationEnabled && first.hasPreviousRotation && first.previousRotation==Q{}.bits());
    s.loop.stepOnce(); const auto second=s.sim->snapshot();
    CHECK(second.previousRotation==first.rotation && second.rotation!=first.rotation);
    World::instance().updatePresentation(0.02f,0.5f);
    CHECK(second==s.sim->snapshot());
    auto* present=World::instance().getAllEntities().front()->getComponent<Transform>();
    const auto expected=*Q::nlerp(Q::fromBits(second.previousRotation),Q::fromBits(second.rotation),D::fromBits(0x3f000000u));
    CHECK(std::bit_cast<std::uint32_t>(present->rotation.y)==expected.y.bits());
}
TEST_CASE(host_native_configuration_remains_an_explicit_compatibility_boundary) {
    Session s; CHECK_TRUE(s.prepared);
    s.loop.setFixedTimestep(D::fromBits(0x3c888888u).toFloat());
    s.loop.stepOnce();
    CHECK(probe.dt.bits()==0x3c888888u && !s.loop.getFixedStep()->isRational());
}
TEST_SUITE_END;
