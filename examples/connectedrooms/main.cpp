#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/EntityPhysicsIntegrationModule.h>
#include <AYEntity/EntityPhysicsBridge.h>
#include <AYEntity/ConnectedSpacePhysicsQuery.h>
#include <AYEntity/ConnectedSpacePhysicsTransfer.h>
#include <AYEntity/components/ColliderComponent.h>
#include <AYEntity/components/RigidBodyComponent.h>
#include <AYPhysics/PhysicsManager.h>
#include <AYPhysics/PhysicsQueryAdapter.h>
#include <AYPhysics/PhysicsWorld3D.h>
#include <iostream>
#include <memory>
#include <string>

using namespace ayt;
using namespace ayt::entity;
using namespace ayt::physics;
namespace math = ayt::math;

namespace {
constexpr float kFixedDt = 1.0f / 60.0f;

struct ConnectedRooms {
    World& world = World::instance();
    SpacePhysicsRoutes routes;
    std::unique_ptr<PhysicsManager> manager;
    EntityPhysicsBridge bridge;
    PhysicsQueryAdapter query;
    Entity* player = nullptr;
    Entity* crate = nullptr;
    RigidBodyComponent* crateBody = nullptr;
    CharacterHandle character = InvalidCharacterHandle;
    CharacterController3DState characterState{};
    RigidBodyComponent* wallBody[2]{};

    bool initialize() {
        if (!registerEntityCoreComponents(ComponentRegistry::instance())
            || !registerEntityPhysicsComponents(ComponentRegistry::instance())
            || !world.initialize()) return false;
        auto& graph = world.connectedSpaces();
        if (!graph.addRegion(1) || !graph.addRegion(2)
            || !routes.set(1, 16) || !routes.set(2, 17)) return false;
        SpacePortal forward;
        forward.id = 1; forward.source = 1; forward.destination = 2;
        forward.exit.position = {10, 0, 0};
        SpacePortal reverse;
        reverse.id = 2; reverse.source = 2; reverse.destination = 1;
        reverse.entrance.position = {10, 0, 0};
        if (!graph.setPortal(forward) || !graph.setPortal(reverse)) return false;

        PhysicsBackendDescriptor config;
        config.kind3D = BackendKind::DefaultJolt;
        config.kind2D = BackendKind::Null;
        config.gravity3D = {};
        config.maxBodies = 256;
        config.maxBodyPairs = 1024;
        config.maxContactConstraints = 1024;
        config.jobWorkerCount = 1;
        manager = PhysicsManager::create(config);
        if (!manager || !manager->isRunning()) return false;
        bridge.setPhysicsManager(manager.get());
        bridge.setSpacePhysicsRoutes(&routes);
        query.bind(manager.get());

        for (SpaceRegionId region = 1; region <= 2; ++region) {
            Entity* roomMarker = world.createSpatialEntity();
            if (!roomMarker || !roomMarker->setSpaceRegion(region)
                || !roomMarker->setWorldPose({2, 0, 0}, {0, 0, 0, 1})) return false;
            auto* body = roomMarker->addComponent<RigidBodyComponent>();
            auto* shape = roomMarker->addComponent<ColliderComponent>();
            if (!body || !shape) return false;
            body->setStatic(true);
            body->setSyncMode(RigidBodyComponent::SyncMode::EntityToPhysics);
            shape->addShape();
            wallBody[region - 1] = body;
        }
        crate = world.createSpatialEntity();
        if (!crate || !crate->setSpaceRegion(1)
            || !crate->setWorldPose({.9f, 0, 1}, {0, 0, 0, 1})) return false;
        crateBody = crate->addComponent<RigidBodyComponent>();
        auto* crateShape = crate->addComponent<ColliderComponent>();
        if (!crateBody || !crateShape) return false;
        crateBody->setSyncMode(RigidBodyComponent::SyncMode::PhysicsToEntity);
        crateBody->setGravityScale(0);
        crateShape->addShape();

        player = world.createSpatialEntity();
        if (!player || !player->setSpaceRegion(1)
            || !player->setWorldPose({0, 0, 1}, {0, 0, 0, 1})) return false;
        auto gravity = *player->spatialGravity();
        gravity.mode = SpatialGravityMode::None;
        if (!player->setSpatialGravity(gravity)) return false;
        CharacterController3DDesc characterDesc;
        characterDesc.position = {0, 0, 1};
        if (bridge.createCharacterForEntity(*player, characterDesc, character)
            != PhysResult::Ok) return false;
        return step();
    }

    bool step() {
        bridge.syncEntityToPhysics();
        if (manager->stepAndWait(kFixedDt) != PhysResult::Ok) return false;
        bridge.syncPhysicsToEntity();
        if (manager->world3D()->getCharacterState(character, characterState)
            != PhysResult::Ok) return false;
        math::FVector3 oldPosition;
        math::FQuaternion rotation;
        if (!player->worldPose(oldPosition, rotation)) return false;
        return player->setWorldPose(characterState.position, rotation);
    }

    bool walk(float direction, int ticks = 30) {
        for (int i = 0; i < ticks; ++i) {
            math::FVector3 oldPosition;
            math::FQuaternion rotation;
            if (!player->worldPose(oldPosition, rotation)) return false;
            const math::FVector3 velocity = rotation * math::FVector3{0, 0, direction * 3.0f};
            const math::FVector3 displacement = velocity * kFixedDt;
            SpaceTraversal preview;
            if (!world.connectedSpaces().travel({player->spaceRegion(),
                {characterState.position, rotation}, velocity}, displacement, 2, preview))
                return false;
            if (preview.crossings) {
                SpaceTraversal committed;
                if (transferSpatialCharacterAcrossSpaces(*player, characterState,
                    displacement, 2, *manager->world3D(),
                    makeSpaceBodyRouteResolver(routes), committed) != PhysResult::Ok)
                    return false;
            } else {
                CharacterController3DInput input;
                input.horizontalVelocity = velocity;
                if (bridge.setCharacterInputForEntity(*player, character, input)
                    != PhysResult::Ok) return false;
            }
            if (!step()) return false;
        }
        return true;
    }

    bool sendCrate() {
        if (crate->spaceRegion() != player->spaceRegion()) return false;
        // Wake a resting body and use the next completed live snapshot.
        if (manager->world3D()->setRigidbodyVelocity(crateBody->getBodyHandle(),
            {0, 0, -2}) != PhysResult::Ok || !step()) return false;
        BodyTransform snapshot;
        for (const auto& body : manager->fetchResults().transforms)
            if (body.body == crateBody->getBodyHandle()) snapshot = body;
        if (snapshot.body != crateBody->getBodyHandle()) return false;
        SpaceTraversal crossing;
        if (transferSpatialRigidBodyAcrossSpaces(*crate, snapshot,
            {0, 0, -(snapshot.position.z + 1.0f)}, 2,
            *manager->world3D(), makeSpaceBodyRouteResolver(routes), crossing)
            != PhysResult::Ok) return false;
        return step();
    }

    bool probe() {
        const auto trace = raycastConnectedSpaces(world, player->spaceRegion(),
            {{2, 0, 2}, {0, 0, -1}}, 4, 0,
            makeSpacePhysicsResolver(query, routes));
        if (trace.path.status != SpaceTraceStatus::Hit
            || trace.localHit.body != wallBody[player->spaceRegion() - 1]->getBodyHandle())
            return false;
        std::cout << "Probe in room " << player->spaceRegion()
                  << " hit body " << trace.localHit.body << '\n';
        return true;
    }

    void describe() const {
        std::cout << "Player: room " << player->spaceRegion()
                  << " feet (" << characterState.position.x << ", "
                  << characterState.position.z << ") | crate: room "
                  << crate->spaceRegion() << '\n';
    }

    void shutdown() {
        if (manager && manager->isRunning() && character)
            (void)manager->world3D()->destroyCharacter(character);
        bridge.shutdown();
        if (manager) manager->shutdown();
        if (world.isInitialized()) world.shutdown();
    }
};
} // namespace

int main(int argc, char** argv) {
    ConnectedRooms game;
    if (!game.initialize()) {
        std::cerr << "Could not start connected rooms (Jolt required).\n";
        game.shutdown();
        return 1;
    }
    const bool script = argc > 1 && std::string(argv[1]) == "--script";
    if (script) {
        const bool passed = game.sendCrate() && game.walk(-1, 30)
            && game.player->spaceRegion() == 2 && game.crate->spaceRegion() == 2
            && game.probe() && game.sendCrate() && game.walk(1, 30)
            && game.player->spaceRegion() == 1 && game.crate->spaceRegion() == 1;
        game.describe();
        game.shutdown();
        return passed ? 0 : 1;
    }
    std::cout << "Two overlapping rooms share coordinates but not collisions.\n"
              << "Commands: w forward, s backward, p send crate through, "
                 "q probe current room, x exit.\n";
    game.describe();
    for (std::string command; std::cin >> command;) {
        if (command == "x") break;
        const bool ok = command == "w" ? game.walk(-1)
            : command == "s" ? game.walk(1)
            : command == "p" ? game.sendCrate()
            : command == "q" ? game.probe() : false;
        std::cout << (ok ? "OK\n" : "Unavailable or failed\n");
        game.describe();
    }
    game.shutdown();
    return 0;
}
