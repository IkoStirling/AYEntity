#include <AYEntity/WorldInteraction3D.h>
#include <AYEntity/EntityImpl.h>
#include <AYEntity/World.h>
#include <AYEntity/components/RigidBodyComponent.h>
#include <AYEntity/components/TransformComponent.h>
#include <AYTest.h>

using namespace ayt;

namespace {
struct Query final : physics::IPhysicsQuery {
    bool ready = true;
    bool unsupportedIgnore = false;
    physics::BodyHandle ignored = physics::InvalidBodyHandle;
    physics::RaycastHit next{};
    bool isReady() const override { return ready; }
    physics::PhysResult raycastSync(const math::Ray&,
        physics::RaycastHit& out, physics::PhysLayerMask) override {
        out = next;
        return physics::PhysResult::Ok;
    }
    physics::PhysResult raycastSyncIgnoringBody(const math::Ray&,
        physics::RaycastHit& out, physics::PhysLayerMask,
        physics::BodyHandle body) override {
        ignored = body;
        out = unsupportedIgnore ? physics::RaycastHit{} : next;
        return unsupportedIgnore ? physics::PhysResult::Unsupported
            : physics::PhysResult::Ok;
    }
    physics::PhysResult overlapSphereSync(const math::FVector3&, float,
        std::vector<physics::BodyHandle>& out,
        physics::PhysLayerMask) override {
        out.clear();
        return physics::PhysResult::Ok;
    }
};
}

TEST_SUITE(WorldInteraction3DTests)

TEST_CASE(requeries_focus_authority_and_returns_only_committed_receipt) {
    auto& world = entity::World::instance();
    world.initialize();
    auto* target = world.createEntity();
    CHECK(target->addComponent<entity::Transform>() != nullptr);
    auto* body = target->addComponent<entity::RigidBodyComponent>();
    CHECK(body != nullptr);
    body->setPhysicsDimension(
        entity::RigidBodyComponent::PhysicsDimension::ThreeD);
    body->setBodyHandle(41);
    CHECK(target->restorePersistentId("11111111111111111111111111111111"));
    Query query;
    query.next.hit = true;
    query.next.body = 41;
    query.next.collider = 7;
    query.next.distance = 2;
    bool allowed = true;
    bool committed = true;
    std::uint32_t offered = 5;
    int commits = 0;
    entity::WorldInteraction3D interaction(world, query,
        [](std::string_view) -> std::optional<entity::WorldInteractionView3D> {
            entity::WorldInteractionView3D view;
            view.reach = 3;
            view.ignoredBody = 11;
            return view;
        },
        [target](physics::BodyHandle handle) -> entity::Entity* {
            return handle == 41 ? target : nullptr;
        },
        [&offered](const entity::Entity&, std::string_view) {
            return std::vector<std::uint32_t>{2, offered};
        },
        [&allowed](std::string_view,
            const entity::WorldInteractionFocus3D&, std::uint32_t) {
            return allowed;
        },
        [&committed, &commits](std::uint64_t requestId, std::string_view,
            const entity::WorldInteractionFocus3D&, std::uint32_t actionId) {
            CHECK(requestId == 77 && actionId == 5);
            ++commits;
            return committed;
        });
    const auto observed = interaction.focus("player");
    CHECK(observed.status == entity::WorldInteractionStatus3D::Ok);
    CHECK(observed.focus.has_value());
    CHECK(query.ignored == 11);
    const std::vector<std::uint32_t> expectedActions{2, 5};
    CHECK(observed.focus->actions == expectedActions);
    auto activated = interaction.activate("player", *observed.focus, 5, 77);
    CHECK(activated.status == entity::WorldInteractionStatus3D::Ok);
    CHECK(activated.receipt.has_value());
    CHECK(activated.receipt->objectId == target->persistentId());
    CHECK(commits == 1);
    allowed = false;
    activated = interaction.activate("player", *observed.focus, 5, 77);
    CHECK(activated.status == entity::WorldInteractionStatus3D::Denied);
    CHECK(commits == 1);
    allowed = true;
    offered = 8;
    activated = interaction.activate("player", *observed.focus, 5, 77);
    CHECK(activated.status == entity::WorldInteractionStatus3D::Stale);
    CHECK(commits == 1);
    offered = 5;
    query.next.body = 99;
    activated = interaction.activate("player", *observed.focus, 5, 77);
    CHECK(activated.status == entity::WorldInteractionStatus3D::Stale);
    CHECK(commits == 1);
    query.next.body = 41;
    committed = false;
    activated = interaction.activate("player", *observed.focus, 5, 77);
    CHECK(activated.status == entity::WorldInteractionStatus3D::Failed);
    CHECK_FALSE(activated.receipt.has_value());
    query.unsupportedIgnore = true;
    CHECK(interaction.focus("player").status ==
        entity::WorldInteractionStatus3D::Unavailable);
    world.shutdown();
}

TEST_SUITE_END
