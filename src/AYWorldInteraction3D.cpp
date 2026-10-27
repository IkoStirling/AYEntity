#include <AYEntity/WorldInteraction3D.h>

#include <AYEntity/EntityImpl.h>
#include <AYEntity/World.h>
#include <AYEntity/components/RigidBodyComponent.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace ayt::entity {

WorldInteraction3D::WorldInteraction3D(World& world,
    physics::IPhysicsQuery& query, ViewProvider view, BodyResolver resolve,
    Offers offers, Permission permission, Commit commit)
    : world_(world), query_(query), view_(std::move(view)),
      resolve_(std::move(resolve)), offers_(std::move(offers)),
      permission_(std::move(permission)), commit_(std::move(commit)) {}

WorldInteractionFocusResult3D WorldInteraction3D::focus(
    std::string_view actorId) const {
    if (actorId.empty() || actorId.size() > 128 || !view_ || !resolve_ ||
        !offers_) return {WorldInteractionStatus3D::Invalid, {}};
    if (!world_.isInitialized() || !query_.isReady())
        return {WorldInteractionStatus3D::Unavailable, {}};
    try {
        const auto view = view_(actorId);
        if (!view || !std::isfinite(view->reach) || view->reach <= 0 ||
            view->reach > 10000)
            return {WorldInteractionStatus3D::Invalid, {}};
        physics::RaycastHit hit{};
        const auto queryResult = query_.raycastSyncIgnoringBody(
            view->ray, hit, view->layerMask, view->ignoredBody);
        if (queryResult != physics::PhysResult::Ok)
            return {WorldInteractionStatus3D::Unavailable, {}};
        if (!hit.hit || hit.body == physics::InvalidBodyHandle ||
            !std::isfinite(hit.distance) || hit.distance < 0 ||
            hit.distance > view->reach) return {};
        auto* entity = resolve_(hit.body);
        if (!entity || !entity->isValid() || !entity->isSpatial() ||
            entity->getWorld() != &world_ || entity->persistentId().empty())
            return {};
        auto* body = entity->getComponent<RigidBodyComponent>();
        if (!body || body->getPhysicsDimension() !=
                RigidBodyComponent::PhysicsDimension::ThreeD ||
            body->getBodyHandle() != hit.body) return {};
        auto actions = offers_(*entity, actorId);
        if (actions.empty()) return {};
        if (actions.size() > 16 ||
            std::any_of(actions.begin(), actions.end(),
                [](std::uint32_t id) { return id == 0; }))
            return {WorldInteractionStatus3D::Invalid, {}};
        std::sort(actions.begin(), actions.end());
        if (std::adjacent_find(actions.begin(), actions.end()) != actions.end())
            return {WorldInteractionStatus3D::Invalid, {}};
        WorldInteractionFocus3D candidate;
        candidate.entity = world_.getEntityHandle(entity->getId());
        candidate.objectId = entity->persistentId();
        candidate.body = hit.body;
        candidate.collider = hit.collider;
        candidate.point = hit.point;
        candidate.normal = hit.normal;
        candidate.distance = hit.distance;
        candidate.actions = std::move(actions);
        return {WorldInteractionStatus3D::Ok, std::move(candidate)};
    } catch (...) {
        return {WorldInteractionStatus3D::Failed, {}};
    }
}

WorldInteractionActivationResult3D WorldInteraction3D::activate(
    std::string_view actorId, const WorldInteractionFocus3D& selected,
    std::uint32_t actionId, std::uint64_t requestId) const {
    if (!requestId || !actionId || selected.objectId.empty() ||
        !permission_ || !commit_)
        return {WorldInteractionStatus3D::Invalid, {}};
    auto current = focus(actorId);
    if (current.status != WorldInteractionStatus3D::Ok)
        return {current.status, {}};
    if (!current.focus || current.focus->entity != selected.entity ||
        current.focus->objectId != selected.objectId ||
        current.focus->body != selected.body ||
        current.focus->collider != selected.collider ||
        !std::binary_search(current.focus->actions.begin(),
            current.focus->actions.end(), actionId))
        return {WorldInteractionStatus3D::Stale, {}};
    try {
        if (!permission_(actorId, *current.focus, actionId))
            return {WorldInteractionStatus3D::Denied, {}};
        if (!commit_(requestId, actorId, *current.focus, actionId))
            return {WorldInteractionStatus3D::Failed, {}};
        return {WorldInteractionStatus3D::Ok,
            WorldInteractionReceipt3D{requestId, current.focus->objectId,
                                      actionId}};
    } catch (...) {
        return {WorldInteractionStatus3D::Failed, {}};
    }
}

} // namespace ayt::entity
