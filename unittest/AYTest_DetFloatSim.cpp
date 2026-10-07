#include <AYEntity.h>
#include <AYEntity/SimToPresentBridgeSystem.h>
#include <AYTest.h>
#include <cfenv>
#include <stdexcept>
#include <vector>
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif
namespace {
using namespace ayt::entity;
using D = ayt::math::DetFloat32;
using V = ayt::math::DetVec3;
using Q = ayt::math::DetQuaternion;
struct Probe {
    unsigned starts = 0, updates = 0;
    D dt{};
    V velocity = V::fromInts(8,-4,2);
} probe;
class Movement final : public IDeterministicSystem {
public:
    const char* getName() const override { return "DetFloatMovement"; }
    void onStart() override { ++probe.starts; }
    void onDeterministicUpdate(D dt) override {
        ++probe.updates; probe.dt = dt;
        for (auto* e : World::instance().getAllEntities()) {
            if (auto* sim = e->getComponent<DetSimTransformComponent>()) {
                (void)sim->translate(probe.velocity*dt);
                if (sim->rotationEnabled) (void)sim->rotateLocal(*Q::fromAxisSinCos(V::fromInts(0,0,1),dt,D::fromInt(1)));
            }
        }
    }
};
struct Fixture {
    World& world = World::instance();
    Entity* entity;
    DetSimTransformComponent* sim;
    Transform* present;
    Fixture() {
        world.initialize(); probe = {};
        entity = world.createEntity();
        sim = entity->addComponent<DetSimTransformComponent>();
        present = entity->addComponent<Transform>();
    }
    ~Fixture() { world.shutdown(); }
};
bool same(const DetSimTransformComponent::Snapshot& a, const DetSimTransformComponent::Snapshot& b) {
    return a == b;
}
}
TEST_SUITE(DetFloatSim)
TEST_CASE(typed_tick_dispatches_and_snapshots_before_every_system) {
    Fixture f;
    CHECK_TRUE(f.sim->setPosition(V::fromInts(2,6,-2)));
    f.world.registerSystem<Movement>(0,SystemLane::Sim);
    f.world.registerSystem<Movement>(1,SystemLane::Sim);
    const auto dt = D::fromInt(1)/D::fromInt(8);
    f.world.fixedUpdate(dt);
    CHECK_TRUE(probe.dt.bits() == dt.bits() && probe.updates == 2 && probe.starts == 2);
    CHECK_TRUE(f.sim->previousPosition == V::fromInts(2,6,-2));
    CHECK_TRUE(f.sim->position == V::fromBits({0x40800000u,0x40a00000u,0xbfc00000u}));
    CHECK_TRUE(f.sim->revision == 3);
    f.world.fixedUpdate(dt);
    CHECK_TRUE(probe.starts == 2 && f.sim->previousPosition == V::fromBits({0x40800000u,0x40a00000u,0xbfc00000u}));
}
TEST_CASE(typed_invalid_dt_does_not_start_or_snapshot) {
    Fixture f; f.world.registerSystem<Movement>(0,SystemLane::Sim);
    const auto before = f.sim->snapshot();
    for (auto bits : {0u,0x80000000u,0xbf800000u,0x7f800000u,0x7fc00000u}) {
        bool threw = false;
        try { f.world.fixedUpdate(D::fromBits(bits)); } catch (const std::invalid_argument&) { threw = true; }
        CHECK_TRUE(threw && same(before,f.sim->snapshot()));
    }
    CHECK_TRUE(probe.starts == 0 && probe.updates == 0);
}
TEST_CASE(legacy_float_tick_has_an_explicit_adapter) {
    Fixture f; f.world.registerSystem<Movement>(0,SystemLane::Sim);
    f.world.fixedUpdate(0.125f);
    CHECK_TRUE(probe.dt.bits() == 0x3e000000u);
    CHECK_TRUE(f.sim->position == V::fromBits({0x3f800000u,0xbf000000u,0x3e800000u}));
}
TEST_CASE(component_rejects_nonfinite_and_overflow_atomically) {
    Fixture f; const auto initial = f.sim->snapshot();
    CHECK_FALSE(f.sim->setPosition(V::fromBits({0,0x7fc00000u,0})));
    CHECK_FALSE(f.sim->translate(V::fromBits({0x7f800000u,0,0})));
    CHECK_TRUE(same(initial,f.sim->snapshot()));
    CHECK_TRUE(f.sim->setPosition(V::fromBits({0x7f7fffffu,0,0})));
    const auto maximum = f.sim->snapshot();
    CHECK_FALSE(f.sim->translate(V::fromBits({0x7f7fffffu,0,0})));
    CHECK_TRUE(same(maximum,f.sim->snapshot()));
}
TEST_CASE(snapshot_restores_history_and_rejects_bad_profile_or_fields) {
    Fixture f; f.sim->setPosition(V::fromInts(1,2,3)); f.sim->beginSimulationStep();
    f.sim->translate(V::fromInts(4,5,6)); const auto saved = f.sim->snapshot();
    f.sim->setPosition(V::fromInts(9,9,9));
    CHECK_TRUE(f.sim->restore(saved) && same(saved,f.sim->snapshot()));
    auto bad = saved; ++bad.profileVersion;
    CHECK_FALSE(f.sim->restore(bad)); CHECK_TRUE(same(saved,f.sim->snapshot()));
    bad = saved; bad.previousPosition[1] = 0x7f800001u;
    CHECK_FALSE(f.sim->restore(bad)); CHECK_TRUE(same(saved,f.sim->snapshot()));
}
TEST_CASE(explicit_fixed_import_preserves_source_and_rounds_large_values) {
    Fixture f; auto* old = f.entity->addComponent<SimTransformComponent>();
    old->position = {ayt::math::Fixed32::fromRaw(0x7fffffff),ayt::math::Fixed32::fromRaw(-1),ayt::math::Fixed32::fromInt(2)};
    old->beginSimulationStep(); old->translate({{}, {}, ayt::math::Fixed32::fromInt(3)});
    f.sim->importFixed(*old);
    CHECK_TRUE(f.sim->position.x.bits() == 0x47000000u);
    CHECK_TRUE(f.sim->position.y.bits() == 0xb7800000u);
    CHECK_TRUE(f.sim->previousPosition.z == D::fromInt(2) && f.sim->position.z == D::fromInt(5));
    CHECK_TRUE(f.sim->revision == old->revision && f.sim->hasPreviousPosition);
    CHECK_TRUE(old->position.x.raw() == 0x7fffffff);
    f.entity->removeComponent<SimTransformComponent>();
    CHECK_TRUE(f.entity->hasComponent<DetSimTransformComponent>());
}
TEST_CASE(bridge_interpolates_without_writing_authority_or_rotation_scale) {
    Fixture f; f.sim->setPosition(V::fromInts(2,6,-2));
    f.world.registerSystem<Movement>(0,SystemLane::Sim);
    f.present->scale = {2,3,4}; f.present->rotation = {0,0,0.5f,0.5f};
    f.world.fixedUpdate(D::fromInt(1)); const auto saved = f.sim->snapshot();
    registerSimToPresentBridgeSystem(); f.world.updatePresentation(0.02f,0.25f);
    CHECK_TRUE(f.present->position.x == 4 && f.present->position.y == 5 && f.present->position.z == -1.5f);
    CHECK_TRUE(f.present->scale.y == 3 && f.present->rotation.z == 0.5f && !f.present->hasPreviousSimulationPose);
    CHECK_TRUE(same(saved,f.sim->snapshot()));
    f.world.updatePresentation(0.02f,1);
    CHECK_TRUE(f.present->position.x == 10 && same(saved,f.sim->snapshot()));
}
TEST_CASE(bridge_skips_conflicting_or_nonfinite_state) {
    Fixture f; f.present->position = {99,98,97}; f.entity->addComponent<SimTransformComponent>();
    SimToPresentBridgeSystem bridge; bridge.onUpdate(1);
    CHECK_TRUE(f.present->position.x == 99);
    f.entity->removeComponent<SimTransformComponent>();
    f.sim->position.x = D::fromBits(0x7f800000u); bridge.onUpdate(1);
    CHECK_TRUE(f.present->position.x == 99);
}
TEST_CASE(bridge_handles_initial_state_extremes_nan_alpha_and_removal) {
    Fixture f; f.sim->setPosition(V::fromBits({0x7f7fffffu,0x80000000u,1}));
    SimToPresentBridgeSystem bridge; bridge.onUpdate(0.5f);
    CHECK_TRUE(std::bit_cast<std::uint32_t>(f.present->position.x) == 0x7f7fffffu);
    f.sim->beginSimulationStep(); f.sim->setPosition(V::fromBits({0xff7fffffu,0,2}));
    bridge.onUpdate(0.5f); CHECK_TRUE(f.present->position.x == 0);
    bridge.onUpdate(D::fromBits(0x7fc00000u).toFloat());
    CHECK_TRUE(std::bit_cast<std::uint32_t>(f.present->position.x) == 0x7f7fffffu);
    f.entity->removeComponent<DetSimTransformComponent>(); bridge.onUpdate(1);
    CHECK_TRUE(std::bit_cast<std::uint32_t>(f.present->position.x) == 0x7f7fffffu);
}
TEST_CASE(per_tick_checkpoint_replay_ignores_host_environment) {
    Fixture f; f.world.registerSystem<Movement>(0,SystemLane::Sim);
    CHECK_TRUE(f.sim->setRotation(Q{}));
    const auto dt = D::fromInt(1)/D::fromInt(60);
    for (unsigned i=0;i<128;++i) f.world.fixedUpdate(dt);
    const auto saved = f.sim->snapshot();
    std::vector<DetSimTransformComponent::Snapshot> samples;
    for (unsigned i=0;i<256;++i) { f.world.fixedUpdate(dt); samples.push_back(f.sim->snapshot()); }
    CHECK_TRUE(f.sim->restore(saved));
    const auto rounding = std::fegetround();
#if defined(_M_X64) || defined(__x86_64__)
    const auto csr = _mm_getcsr();
#endif
    std::fesetround(FE_UPWARD);
#if defined(_M_X64) || defined(__x86_64__)
    _mm_setcsr(_mm_getcsr() | 0x8040u);
#endif
    bool equal = true;
    for (const auto& sample : samples) { f.world.fixedUpdate(dt); equal &= same(sample,f.sim->snapshot()); }
    std::fesetround(rounding);
#if defined(_M_X64) || defined(__x86_64__)
    _mm_setcsr(csr);
#endif
    CHECK_TRUE(equal);
}
TEST_CASE(rotation_activation_and_tick_history_are_explicit) {
    Fixture f;
    CHECK_FALSE(f.sim->rotationEnabled);
    CHECK_FALSE(f.sim->rotateLocal(Q{}));
    const auto turn=*Q::fromAxisSinCos(V::fromInts(1,0,0),D::fromInt(1),D::fromInt(1));
    CHECK_TRUE(f.sim->setRotation(turn));
    CHECK_TRUE(f.sim->rotationEnabled && !f.sim->hasPreviousRotation && f.sim->previousRotation==f.sim->rotation);
    const auto start=f.sim->rotation.bits();
    f.world.registerSystem<Movement>(0,SystemLane::Sim);
    f.world.fixedUpdate(D::fromInt(1)/D::fromInt(64));
    CHECK_TRUE(f.sim->hasPreviousRotation && f.sim->previousRotation.bits()==start);
    CHECK_TRUE(f.sim->rotation.bits()!=start);
    const auto second=f.sim->rotation.bits();
    f.world.fixedUpdate(D::fromInt(1)/D::fromInt(64));
    CHECK_TRUE(f.sim->previousRotation.bits()==second);
}
TEST_CASE(rotation_mutation_and_restore_reject_invalid_state_atomically) {
    Fixture f; CHECK_TRUE(f.sim->setRotation(Q{})); const auto saved=f.sim->snapshot();
    CHECK_FALSE(f.sim->setRotation(Q::fromBits({0,0,0,0})));
    CHECK_FALSE(f.sim->rotateLocal(Q::fromBits({0,0,0x7f800000u,0})));
    CHECK_TRUE(saved==f.sim->snapshot());
    auto bad=saved; ++bad.snapshotVersion;
    CHECK_FALSE(f.sim->restore(bad)); CHECK_TRUE(saved==f.sim->snapshot());
    bad=saved; ++bad.rotationProfileVersion;
    CHECK_FALSE(f.sim->restore(bad)); CHECK_TRUE(saved==f.sim->snapshot());
    bad=saved; bad.rotation={0,0,0,0};
    CHECK_FALSE(f.sim->restore(bad)); CHECK_TRUE(saved==f.sim->snapshot());
    bad=saved; bad.previousRotation[0]=0x7fc00000u;
    CHECK_FALSE(f.sim->restore(bad)); CHECK_TRUE(saved==f.sim->snapshot());
    bad=saved; bad.rotationEnabled=false; bad.hasPreviousRotation=true;
    CHECK_FALSE(f.sim->restore(bad)); CHECK_TRUE(saved==f.sim->snapshot());
}
TEST_CASE(rotation_restore_preserves_field_bits_and_disable_reseeds_history) {
    Fixture f; CHECK_TRUE(f.sim->setRotation(Q{D::fromInt(1),D::fromInt(2),D::fromInt(3),D::fromInt(4)}));
    f.sim->beginSimulationStep(); CHECK_TRUE(f.sim->rotateLocal(Q{})); const auto saved=f.sim->snapshot();
    f.sim->disableRotation(); CHECK_TRUE(!f.sim->rotationEnabled && !f.sim->hasPreviousRotation);
    CHECK_TRUE(f.sim->restore(saved) && saved==f.sim->snapshot());
    f.sim->disableRotation(); const auto revision=f.sim->revision;
    f.sim->disableRotation(); CHECK_TRUE(f.sim->revision==revision);
    CHECK_TRUE(f.sim->setRotation(Q{})); CHECK_TRUE(!f.sim->hasPreviousRotation && f.sim->previousRotation==Q{});
}
TEST_CASE(rotation_bridge_samples_shortest_hemisphere_without_touching_sim_or_scale) {
    Fixture f; CHECK_TRUE(f.sim->setRotation(Q{})); f.sim->beginSimulationStep();
    CHECK_TRUE(f.sim->setRotation(Q{{},{},D::fromInt(1),{}}));
    f.present->scale={2,3,4}; const auto saved=f.sim->snapshot();
    SimToPresentBridgeSystem bridge; bridge.onUpdate(0.5f);
    const auto expected=*Q::nlerp(f.sim->previousRotation,f.sim->rotation,D::fromBits(0x3f000000u));
    const auto fields=expected.toFloats();
    CHECK_TRUE(std::bit_cast<std::uint32_t>(f.present->rotation.z)==expected.z.bits());
    CHECK_TRUE(f.present->rotation.w==fields[3] && f.present->previousRotation.w==fields[3]);
    CHECK_TRUE(saved==f.sim->snapshot() && f.present->scale.y==3 && !f.present->hasPreviousSimulationPose);
    f.sim->rotation=-f.sim->previousRotation; bridge.onUpdate(0.5f);
    CHECK_TRUE(f.present->rotation.w==1 && f.present->rotation.z==0);
    f.sim->disableRotation(); f.present->rotation={0,1,0,0}; bridge.onUpdate(1);
    CHECK_TRUE(f.present->rotation.y==1);
}
TEST_CASE(rotation_bridge_rejects_invalid_pose_before_partial_present_writes) {
    Fixture f; CHECK_TRUE(f.sim->setRotation(Q{})); f.present->position={99,98,97};
    f.present->rotation={0,1,0,0}; f.sim->position=V::fromInts(1,2,3);
    f.sim->rotation=Q::fromBits({0,0,0,0});
    SimToPresentBridgeSystem bridge; bridge.onUpdate(1);
    CHECK_TRUE(f.present->position.x==99 && f.present->rotation.y==1);
    f.sim->rotation=Q{}; f.sim->hasPreviousRotation=true; f.sim->previousRotation.x=D::fromBits(0x7f800000u);
    bridge.onUpdate(0.5f); CHECK_TRUE(f.present->position.x==99);
}
TEST_CASE(fixed_import_discards_rotation_and_v1_translation_migration_is_explicit) {
    Fixture f; CHECK_TRUE(f.sim->setRotation(Q{})); f.sim->beginSimulationStep();
    SimTransformComponent source; source.position={ayt::math::Fixed32::fromInt(1),ayt::math::Fixed32::fromInt(2),ayt::math::Fixed32::fromInt(3)};
    f.sim->importFixed(source); CHECK_TRUE(!f.sim->rotationEnabled && !f.sim->hasPreviousRotation);
    CHECK_TRUE(f.sim->rotation==Q{} && f.sim->position==V::fromInts(1,2,3));
    // Decode the five legacy fields into a fresh v2 field record; no raw-layout cast.
    DetSimTransformComponent::Snapshot migrated;
    migrated.position=V::fromInts(3,4,5).bits(); migrated.previousPosition=V::fromInts(1,2,3).bits();
    migrated.hasPreviousPosition=true; migrated.revision=42;
    CHECK_TRUE(f.sim->restore(migrated) && migrated==f.sim->snapshot());
}
TEST_SUITE_END;
