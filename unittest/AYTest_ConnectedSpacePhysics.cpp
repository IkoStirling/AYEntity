#include <AYEntity/ConnectedSpacePhysicsQuery.h>
#include <AYEntity/ConnectedSpacePhysicsTransfer.h>
#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/EntityPhysicsIntegrationModule.h>
#include <AYEntity/EntityPhysicsBridge.h>
#include <AYEntity/World.h>
#include <AYEntity/components/RigidBodyComponent.h>
#include <AYEntity/components/ColliderComponent.h>
#include <AYPhysics/PhysicsManager.h>
#include <AYPhysics/PhysicsQueryAdapter.h>
#include <AYPhysics/PhysicsBackendTestAccess.h>
#include <AYPhysics/PhysicsWorld3D.h>
#include <AYTest.h>

using namespace ayt::entity;
namespace math = ayt::math;
namespace physics = ayt::physics;
namespace {
struct MockSpaceQuery : physics::IPhysicsQuery {
    float distance = 0;
    uint32_t calls = 0;
    bool ready = true;
    physics::PhysLayerMask lastMask = 0;
    bool isReady() const override { return ready; }
    physics::PhysResult raycastSync(const math::Ray&,physics::RaycastHit& hit,
                                   physics::PhysLayerMask mask) override {
        ++calls; lastMask=mask; hit={}; hit.hit=true; hit.distance=distance;
        return physics::PhysResult::Ok;
    }
    physics::PhysResult overlapSphereSync(const math::FVector3&,float,
        std::vector<physics::BodyHandle>&,physics::PhysLayerMask) override {
        return physics::PhysResult::Unsupported;
    }
};
}
TEST_SUITE(ConnectedSpacePhysicsTests)
TEST_CASE(shared_region_routes_drive_body_creation_rebinding_and_queries) {
    CHECK(registerEntityCoreComponents(ComponentRegistry::instance()));
    CHECK(registerEntityPhysicsComponents(ComponentRegistry::instance()));
    World& world=World::instance();CHECK(world.initialize());
    CHECK(world.connectedSpaces().addRegion(1));
    CHECK(world.connectedSpaces().addRegion(2));
    CHECK(world.connectedSpaces().addRegion(3));
    SpacePhysicsRoutes routes;
    CHECK(routes.set(1,16));CHECK(routes.set(2,17));
    CHECK(!routes.set(3,17));CHECK(!routes.set(0,18));
    CHECK(!routes.set(3,2));
    CHECK(!routes.set(3,32));
    auto* first=world.createSpatialEntity();
    auto* second=world.createSpatialEntity();
    auto* missing=world.createSpatialEntity();
    CHECK(first && second && missing);
    CHECK(first->setSpaceRegion(1));
    CHECK(second->setSpaceRegion(2));
    CHECK(missing->setSpaceRegion(3));
    auto* firstBody=first->addComponent<RigidBodyComponent>();
    auto* secondBody=second->addComponent<RigidBodyComponent>();
    auto* missingBody=missing->addComponent<RigidBodyComponent>();
    firstBody->setSyncMode(RigidBodyComponent::SyncMode::EntityToPhysics);
    secondBody->setSyncMode(RigidBodyComponent::SyncMode::EntityToPhysics);
    missingBody->setSyncMode(RigidBodyComponent::SyncMode::EntityToPhysics);
    firstBody->setStatic(true); secondBody->setStatic(true);
    physics::PhysicsBackendDescriptor backend;backend.kind3D=physics::BackendKind::Mock;
    auto manager=physics::PhysicsManager::create(backend);CHECK(manager!=nullptr);
    EntityPhysicsBridge bridge;bridge.setPhysicsManager(manager.get());
    bridge.setSpacePhysicsRoutes(&routes);
    bridge.syncEntityToPhysics();
    CHECK(firstBody->getBodyHandle()!=physics::InvalidBodyHandle);
    CHECK(secondBody->getBodyHandle()!=physics::InvalidBodyHandle);
    CHECK(missingBody->getBodyHandle()==physics::InvalidBodyHandle);
    physics::CharacterHandle missingCharacter=physics::InvalidCharacterHandle;
    CHECK(bridge.createCharacterForEntity(*missing,{},missingCharacter)
          ==physics::PhysResult::InvalidState);
    CHECK(missingCharacter==physics::InvalidCharacterHandle);
    CHECK(manager->stepAndWait(1.f/60.f)==physics::PhysResult::Ok);
    bool sawFirst=false,sawSecond=false;
    for(const auto& payload:physics::testaccess::mockBackendCreatePayloads()) {
        if(payload.kind!=physics::PhysicsCreatePayload::Kind::Rigidbody) continue;
        sawFirst |= payload.rigidDesc.layer==16 && payload.rigidDesc.collideMask==(1u<<16);
        sawSecond |= payload.rigidDesc.layer==17 && payload.rigidDesc.collideMask==(1u<<17);
    }
    CHECK(sawFirst && sawSecond);
    MockSpaceQuery query;query.distance=.5f;
    const auto resolver=makeSpacePhysicsResolver(query,routes);
    CHECK(raycastConnectedSpaces(world,1,{{0,0,1},{0,0,-1}},2,0,resolver)
          .path.status==SpaceTraceStatus::Hit);
    CHECK(query.lastMask==(1u<<16));
    CHECK(raycastConnectedSpaces(world,2,{{0,0,1},{0,0,-1}},2,0,resolver)
          .path.status==SpaceTraceStatus::Hit);
    CHECK(query.lastMask==(1u<<17));
    CHECK(raycastConnectedSpaces(world,3,{{0,0,1},{0,0,-1}},2,0,resolver)
          .path.status==SpaceTraceStatus::QueryFailed);
    physics::testaccess::reset();
    CHECK(first->setSpaceRegion(2));
    bridge.syncEntityToPhysics();
    CHECK(manager->stepAndWait(1.f/60.f)==physics::PhysResult::Ok);
    bool rerouted=false;
    for(const auto& command:physics::testaccess::mockBackendCommands())
        if(command.type==physics::PhysicsCommandType::SetRigidbodyRoute3D
            && command.body==firstBody->getBodyHandle()) {
            rerouted=true;
            CHECK(command.queryId==17);
            CHECK(command.layerMask==(1u<<17));
        }
    CHECK(rerouted);
    bridge.shutdown();manager->shutdown();world.shutdown();
}
TEST_CASE(overlapping_rooms_keep_jolt_rays_and_character_travel_in_one_region) {
    CHECK(registerEntityCoreComponents(ComponentRegistry::instance()));
    CHECK(registerEntityPhysicsComponents(ComponentRegistry::instance()));
    World& world=World::instance();CHECK(world.initialize());
    CHECK(world.connectedSpaces().addRegion(1));
    CHECK(world.connectedSpaces().addRegion(2));
    SpacePortal portal;portal.id=1;portal.source=1;portal.destination=2;
    portal.exit.position={10,0,0};CHECK(world.connectedSpaces().setPortal(portal));
    SpacePortal reverse;reverse.id=2;reverse.source=2;reverse.destination=1;
    reverse.entrance.position={10,0,0};CHECK(world.connectedSpaces().setPortal(reverse));
    SpacePhysicsRoutes routes;CHECK(routes.set(1,16));CHECK(routes.set(2,17));
    physics::PhysicsBackendDescriptor backend;
    backend.kind3D=physics::BackendKind::DefaultJolt;
    backend.kind2D=physics::BackendKind::Null;
    backend.gravity3D={0,0,0};
    backend.maxBodies=256;
    backend.maxBodyPairs=1024;
    backend.maxContactConstraints=1024;
    backend.jobWorkerCount=1;
    auto manager=physics::PhysicsManager::create(backend);CHECK(manager!=nullptr);
    EntityPhysicsBridge bridge;bridge.setPhysicsManager(manager.get());
    bridge.setSpacePhysicsRoutes(&routes);
    auto createWall=[&](SpaceRegionId region, Entity** outEntity=nullptr) {
        auto* entity=world.createSpatialEntity();
        CHECK(entity!=nullptr);
        if (outEntity) *outEntity=entity;
        CHECK(entity->setSpaceRegion(region));
        CHECK(entity->setWorldPose({2,0,0},{0,0,0,1}));
        auto* body=entity->addComponent<RigidBodyComponent>();
        CHECK(body!=nullptr);body->setStatic(true);
        body->setSyncMode(RigidBodyComponent::SyncMode::EntityToPhysics);
        auto* collider=entity->addComponent<ColliderComponent>();
        CHECK(collider!=nullptr);collider->addShape();
        return body;
    };
    Entity* firstWallEntity=nullptr;
    auto* firstWall=createWall(1,&firstWallEntity);
    auto* secondWall=createWall(2);
    bridge.syncEntityToPhysics();
    if (firstWall->getBodyHandle()==physics::InvalidBodyHandle) {
        // The no-Jolt build has no native 3D collision backend.
        bridge.shutdown();manager->shutdown();world.shutdown();return;
    }
    CHECK(secondWall->getBodyHandle()!=physics::InvalidBodyHandle);
    CHECK(manager->stepAndWait(1.f/60.f)==physics::PhysResult::Ok);
    physics::PhysicsQueryAdapter query;query.bind(manager.get());
    const auto resolve=makeSpacePhysicsResolver(query,routes);
    const auto source=raycastConnectedSpaces(world,1,{{2,0,2},{0,0,-1}},4,0,resolve);
    const auto destination=raycastConnectedSpaces(world,2,{{2,0,2},{0,0,-1}},4,0,resolve);
    CHECK(source.path.status==SpaceTraceStatus::Hit);
    CHECK(destination.path.status==SpaceTraceStatus::Hit);
    CHECK(source.localHit.body==firstWall->getBodyHandle());
    CHECK(destination.localHit.body==secondWall->getBodyHandle());
    const auto afterQueriesStatus=manager->stepAndWait(1.f/60.f);
    CHECK_INT_EQ(static_cast<int>(afterQueriesStatus),static_cast<int>(physics::PhysResult::Ok));
    if (afterQueriesStatus!=physics::PhysResult::Ok) {
        bridge.shutdown();manager->shutdown();world.shutdown();return;
    }
    CHECK(firstWallEntity->setSpaceRegion(2));
    bridge.syncEntityToPhysics();
    CHECK(manager->stepAndWait(1.f/60.f)==physics::PhysResult::Ok);
    CHECK(raycastConnectedSpaces(world,1,{{2,0,2},{0,0,-1}},4,0,resolve)
          .path.status==SpaceTraceStatus::Miss);
    CHECK(raycastConnectedSpaces(world,2,{{2,0,2},{0,0,-1}},4,0,resolve)
          .path.status==SpaceTraceStatus::Hit);
    auto* movable=world.createSpatialEntity();CHECK(movable!=nullptr);
    CHECK(movable->setSpaceRegion(1));
    CHECK(movable->setWorldPose({.9f,0,1},{0,0,0,1}));
    auto* movingBody=movable->addComponent<RigidBodyComponent>();
    CHECK(movingBody!=nullptr);
    movingBody->setSyncMode(RigidBodyComponent::SyncMode::PhysicsToEntity);
    movingBody->setVelocity(0,0,-2);
    movingBody->setGravityScale(0);
    auto* movingCollider=movable->addComponent<ColliderComponent>();
    CHECK(movingCollider!=nullptr);
    movingCollider->addShape();
    bridge.syncEntityToPhysics();
    const auto afterCreateStatus=manager->stepAndWait(1.f/60.f);
    CHECK_INT_EQ(static_cast<int>(afterCreateStatus),static_cast<int>(physics::PhysResult::Ok));
    if (afterCreateStatus!=physics::PhysResult::Ok) {
        bridge.shutdown();manager->shutdown();world.shutdown();return;
    }
    bridge.syncPhysicsToEntity();
    const auto findBody=[&]() -> physics::BodyTransform {
        for(const auto& body:manager->fetchResults().transforms)
            if(body.body==movingBody->getBodyHandle()) return body;
        return {};
    };
    SpaceTraversal bodyCrossing;
    auto bodySnapshot=findBody();
    CHECK(bodySnapshot.body==movingBody->getBodyHandle());
    CHECK(transferSpatialRigidBodyAcrossSpaces(*movable,bodySnapshot,{0,0,-3},2,
        *manager->world3D(),makeSpaceBodyRouteResolver(routes),bodyCrossing)
        ==physics::PhysResult::Ok);
    CHECK(movable->spaceRegion()==2);
    CHECK(manager->stepAndWait(1.f/60.f)==physics::PhysResult::Ok);
    bridge.syncPhysicsToEntity();
    const auto movedHit=raycastConnectedSpaces(world,2,{{9.1f,0,5},{0,0,-1}},6,0,resolve);
    const auto wrongRoomHit=raycastConnectedSpaces(world,1,{{9.1f,0,5},{0,0,-1}},6,0,resolve);
    CHECK(movedHit.path.status==SpaceTraceStatus::Hit);
    CHECK(movedHit.localHit.body==movingBody->getBodyHandle());
    CHECK(wrongRoomHit.path.status==SpaceTraceStatus::Miss);
    bodySnapshot=findBody();
    CHECK(bodySnapshot.body==movingBody->getBodyHandle());
    CHECK(transferSpatialRigidBodyAcrossSpaces(*movable,bodySnapshot,{0,0,-3},2,
        *manager->world3D(),makeSpaceBodyRouteResolver(routes),bodyCrossing)
        ==physics::PhysResult::Ok);
    CHECK(movable->spaceRegion()==1);
    CHECK(manager->stepAndWait(1.f/60.f)==physics::PhysResult::Ok);
    bridge.syncPhysicsToEntity();
    auto* player=world.createSpatialEntity();CHECK(player!=nullptr);
    CHECK(player->setSpaceRegion(1));
    CHECK(player->setWorldPose({0,0,1},{0,0,0,1}));
    physics::CharacterController3DDesc desc;
    desc.position={0,0,1};
    physics::CharacterHandle character=physics::InvalidCharacterHandle;
    CHECK(bridge.createCharacterForEntity(*player,desc,character)==physics::PhysResult::Ok);
    CHECK(character!=physics::InvalidCharacterHandle);
    CHECK(manager->stepAndWait(1.f/60.f)==physics::PhysResult::Ok);
    physics::CharacterController3DState state;
    CHECK(manager->world3D()->getCharacterState(character,state)==physics::PhysResult::Ok);
    state.linearVelocity={0,0,-2};
    SpaceTraversal crossing;
    CHECK(transferSpatialCharacterAcrossSpaces(*player,state,{0,0,1},2,
        *manager->world3D(),makeSpaceBodyRouteResolver(routes),crossing)
        ==physics::PhysResult::InvalidParam);
    const auto invalidRoute=[](SpaceRegionId)->std::optional<SpaceBodyRoute> {
        return SpaceBodyRoute{32,0};
    };
    CHECK(transferSpatialCharacterAcrossSpaces(*player,state,{0,0,-3},2,
        *manager->world3D(),invalidRoute,crossing)==physics::PhysResult::InvalidParam);
    CHECK(player->spaceRegion()==1);
    CHECK(crossing.crossings==0);
    CHECK(transferSpatialCharacterAcrossSpaces(*player,state,{0,0,-3},2,
        *manager->world3D(),makeSpaceBodyRouteResolver(routes),crossing)
        ==physics::PhysResult::Ok);
    CHECK(player->spaceRegion()==2);
    CHECK(crossing.crossings==1);
    CHECK_FLOAT_EQ(player->transform()->position.x,10,.05f);
    physics::CharacterController3DInput input;input.useGravityOverride=true;
    CHECK(bridge.setCharacterInputForEntity(*player,character,input)==physics::PhysResult::Ok);
    CHECK(manager->stepAndWait(1.f/60.f)==physics::PhysResult::Ok);
    CHECK(manager->world3D()->getCharacterState(character,state)==physics::PhysResult::Ok);
    CHECK_FLOAT_EQ(state.position.x,10,.05f);
    CHECK(state.position.z>0);
    CHECK(transferSpatialCharacterAcrossSpaces(*player,state,{0,0,-3},2,
        *manager->world3D(),makeSpaceBodyRouteResolver(routes),crossing)
        ==physics::PhysResult::Ok);
    CHECK(player->spaceRegion()==1);
    CHECK(manager->stepAndWait(1.f/60.f)==physics::PhysResult::Ok);
    CHECK(manager->world3D()->getCharacterState(character,state)==physics::PhysResult::Ok);
    CHECK_FLOAT_EQ(state.position.x,0,.05f);
    CHECK(manager->world3D()->destroyCharacter(character)==physics::PhysResult::Ok);
    CHECK(manager->stepAndWait(1.f/60.f)==physics::PhysResult::Ok);
    bridge.shutdown();manager->shutdown();world.shutdown();
}
TEST_CASE(region_queries_stop_at_portal_and_continue_in_destination) {
    World& world=World::instance(); CHECK(world.initialize());
    auto& graph=world.connectedSpaces();
    CHECK(graph.addRegion(1)); CHECK(graph.addRegion(2));
    SpacePortal p; p.id=1;p.source=1;p.destination=2;p.exit.position={10,0,0};
    CHECK(graph.setPortal(p));
    MockSpaceQuery sourceQuery;sourceQuery.distance=3;
    MockSpaceQuery destinationQuery;destinationQuery.distance=1;
    const auto result=raycastConnectedSpaces(world,1,{{0,0,1},{0,0,-1}},5,2,
        [&](SpaceRegionId region)->std::optional<SpacePhysicsRegion>{
            return SpacePhysicsRegion{region==1?&sourceQuery:&destinationQuery,region==1?1u:2u};
        });
    CHECK(sourceQuery.calls==1);CHECK(destinationQuery.calls==1);
    CHECK(result.path.status==SpaceTraceStatus::Hit);
    CHECK(result.path.region==2);CHECK(result.path.crossings==1);
    CHECK_FLOAT_EQ(result.path.distance,2,2e-4f);
    CHECK(result.localHit.hit);CHECK_FLOAT_EQ(result.localHit.distance,1,1e-4f);
    destinationQuery.ready=false;
    const auto failed=raycastConnectedSpaces(world,1,{{0,0,1},{0,0,-1}},5,2,
        [&](SpaceRegionId region)->std::optional<SpacePhysicsRegion>{
            return SpacePhysicsRegion{region==1?&sourceQuery:&destinationQuery,0xFFFFFFFFu};
        });
    CHECK(failed.path.status==SpaceTraceStatus::QueryFailed);
    CHECK(failed.physicsResult==physics::PhysResult::InvalidState);
    world.shutdown();
}
TEST_CASE(dynamic_body_transfer_updates_region_only_after_one_command_is_admitted) {
    CHECK(registerEntityCoreComponents(ComponentRegistry::instance()));
    CHECK(registerEntityPhysicsComponents(ComponentRegistry::instance()));
    World& world=World::instance();CHECK(world.initialize());
    auto& graph=world.connectedSpaces();
    CHECK(graph.addRegion(1));CHECK(graph.addRegion(2));
    SpacePortal portal;portal.id=1;portal.source=1;portal.destination=2;
    portal.exit.position={10,0,0};CHECK(graph.setPortal(portal));
    auto* entity=world.createSpatialEntity();CHECK(entity!=nullptr);
    CHECK(entity->setSpaceRegion(1));
    auto* component=entity->addComponent<RigidBodyComponent>();
    CHECK(component!=nullptr);
    physics::PhysicsBackendDescriptor desc;desc.kind3D=physics::BackendKind::Mock;
    auto refusedManager=physics::PhysicsManager::create(desc);
    CHECK(refusedManager!=nullptr);
    auto* refusedWorld=refusedManager->world3D();
    refusedManager->shutdown();
    auto manager=physics::PhysicsManager::create(desc);CHECK(manager!=nullptr);
    auto* physicsWorld=manager->world3D();
    physics::BodyHandle handle=physics::InvalidBodyHandle;
    physics::RigidbodyDesc bodyDesc;
    CHECK(physicsWorld->createRigidbody(bodyDesc,handle)==physics::PhysResult::Ok);
    component->setBodyHandle(handle);
    physics::BodyTransform snapshot;snapshot.body=handle;snapshot.position={0,0,1};
    snapshot.rotation={0,0,0,1};snapshot.linearVelocity={0,0,-4};
    snapshot.angularVelocity={1,2,3};
    SpaceTraversal result;
    const auto route=[](SpaceRegionId region)->std::optional<SpaceBodyRoute> {
        if(region==2) return SpaceBodyRoute{2,1u<<2};
        return std::nullopt;
    };
    CHECK(transferSpatialRigidBodyAcrossSpaces(*entity,snapshot,{0,0,-3},2,
        *physicsWorld,{},result)==physics::PhysResult::InvalidParam);
    CHECK(entity->spaceRegion()==1);
    CHECK(transferSpatialRigidBodyAcrossSpaces(*entity,snapshot,{0,0,-3},2,
        *refusedWorld,route,result)==physics::PhysResult::InvalidState);
    CHECK(entity->spaceRegion()==1);
    CHECK_FLOAT_EQ(component->getVelocityZ(),0,1e-5f);
    CHECK(result.crossings==0);
    CHECK(transferSpatialRigidBodyAcrossSpaces(*entity,snapshot,{0,0,-3},2,
        *physicsWorld,route,result)==physics::PhysResult::Ok);
    CHECK(entity->spaceRegion()==2);CHECK(result.crossings==1);
    CHECK_FLOAT_EQ(component->getVelocityZ(),4,1e-4f);
    CHECK_FLOAT_EQ(entity->transform()->position.x,10,1e-4f);
    CHECK(manager->stepAndWait(1.f/60.f)==physics::PhysResult::Ok);
    bool captured=false;
    for(const auto& payload:physics::testaccess::mockBackendCreatePayloads()) {
        if(payload.kind!=physics::PhysicsCreatePayload::Kind::BodyTransfer3D) continue;
        captured=true;
        CHECK_FLOAT_EQ(payload.bodyTransfer3D.position.x,10,1e-4f);
        CHECK_FLOAT_EQ(payload.bodyTransfer3D.linearVelocity.z,4,1e-4f);
        CHECK_FLOAT_EQ(payload.bodyTransfer3D.angularVelocity.x,-1,1e-4f);
        CHECK_FLOAT_EQ(payload.bodyTransfer3D.angularVelocity.y,2,1e-4f);
        CHECK_FLOAT_EQ(payload.bodyTransfer3D.angularVelocity.z,-3,1e-4f);
        CHECK(payload.bodyTransfer3D.layer==2);
        CHECK(payload.bodyTransfer3D.collideMask==(1u<<2));
    }
    CHECK(captured);
    manager->shutdown();world.shutdown();
}
TEST_SUITE_END
int main(int argc,char** argv){return ayt::test::runTests("AYEntity_ConnectedSpacePhysicsTests",argc,argv);}
