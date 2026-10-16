#include <AYEntity.h>
#include <AYEntity/CameraRuntime.h>
#include <AYEntity/components/CameraControllerComponent.h>
#include <cmath>

namespace ayt::entity {
namespace {
float damping(float rate, float dt) {
    return std::isfinite(rate) && rate > 0 ? 1 - std::exp(-rate * dt) : 1;
}
math::FQuaternion lookRotation(const math::FVector3& forward) {
    const float horizontal = std::sqrt(forward.x * forward.x + forward.z * forward.z);
    return math::FQuaternion::fromEulerAngles({-std::atan2(forward.y, horizontal),
        std::atan2(forward.x, forward.z), 0});
}
uint64_t viewKey(int32_t viewport, bool twoD) {
    return (uint64_t(uint32_t(viewport)) << 1) | uint64_t(twoD);
}
void updateShake(CameraControllerComponent& controller, float dt) {
    controller.shakePosition = {}; controller.shakeRotation = {0,0,0,1};
    if (controller.shakeDuration <= 0) return;
    controller.shakeElapsed = std::min(controller.shakeElapsed + dt, controller.shakeDuration);
    const float weight = 1 - controller.shakeElapsed / controller.shakeDuration;
    const float phase = float(controller.shakeSeed % 8191u) * .017f;
    const float t = controller.shakeElapsed * controller.shakeFrequency * 6.283185307f;
    const math::FVector3 wave{std::sin(t + phase), std::sin(t * 1.217f + phase * 2),
        controller.twoD ? 0.f : std::sin(t * .913f + phase * 3)};
    controller.shakePosition = wave * (controller.shakePositionAmplitude * weight);
    const auto angles = controller.twoD
        ? math::FVector3{0,0,wave.x * controller.shakeRotationAmplitude * weight}
        : wave * (controller.shakeRotationAmplitude * weight);
    controller.shakeRotation = math::FQuaternion::fromEulerAngles(angles);
    if (weight <= 0) controller.shakeDuration = 0;
}
}

void CameraRuntimeSystem::observeCollision(DebugCollision value) const noexcept {
    if(!_debugCapture)return;try{if(_debugCollisions.size()==256)_debugCollisions.erase(_debugCollisions.begin());_debugCollisions.push_back(value);}catch(...){}
}
CameraRuntimeSystem::DebugSnapshot CameraRuntimeSystem::debugSnapshot() const {
    DebugSnapshot out;out.collisions=_debugCollisions;
    for(const auto& [key,state]:_transitions){CameraFrame frame;if(state.transition.snapshot(frame))out.views.push_back({int32_t(key>>1),bool(key&1),frame,state.transition.debugProgress(),state.transition.debugDuration()});}
    return out;
}
bool CameraControllerComponent::setTarget(Entity* target) {
    if (!_entity || !_entity->getWorld()) return false;
    if (target && (target == _entity || !target->isSpatial() || target->getWorld() != _entity->getWorld())) return false;
    targetPersistentId = target ? target->persistentId() : std::string{};
    targetName = target ? target->getName() : std::string{};
    targetHandle = target ? _entity->getWorld()->getEntityHandle(target->getId()) : EntityHandle{};
    resetHistory(); return true;
}
Entity* CameraControllerComponent::resolveTarget() const {
    auto* world = _entity ? _entity->getWorld() : nullptr;
    if (!world) return nullptr;
    Entity* target = nullptr;
    if (!targetPersistentId.empty()) {
        target = world->getEntityByHandle(targetHandle);
        if (!target || target->persistentId() != targetPersistentId)
            target = world->findEntityByPersistentId(targetPersistentId);
    } else if (!targetName.empty()) target = world->findEntity(targetName.c_str());
    if (!target || target == _entity || !target->isSpatial()) { targetHandle = {}; return nullptr; }
    targetHandle = world->getEntityHandle(target->getId()); return target;
}
void CameraRuntimeSystem::onUpdate(float dt) {
    _debugCollisions.clear();
    _dt = std::isfinite(dt) && dt > 0 ? dt : 0;
    ++_tick;
    auto& world = _owner ? *_owner : World::instance();
    const auto& collision = _collision ? _collision : _defaultCollision;
    const float alpha = world.presentationInterpolationAlpha();
    for (auto* entity : world.query<Transform, CameraControllerComponent>()) {
        auto* transform = entity->getComponent<Transform>();
        auto* c = entity->getComponent<CameraControllerComponent>();
        if (!transform || !c) continue;
        updateShake(*c, _dt);
        c->hasCollisionAnchor = false;
        if (_dt == 0 || c->mode == 0 || c->mode < 0 || c->mode > 3) continue;
        if (!camera_detail::finite(transform->position) || !std::isfinite(transform->rotation.dot(transform->rotation))
            || transform->rotation.dot(transform->rotation) < 1e-12f) continue;
        math::FVector3 currentPosition; math::FQuaternion currentRotation;
        if (!entity->worldPose(currentPosition,currentRotation,alpha)) continue;
        auto desiredPosition = currentPosition;
        auto desiredRotation = currentRotation;
        if (c->mode == 3) {
            if (!camera_detail::finite(c->movementInput) || !std::isfinite(c->moveSpeed)
                || !std::isfinite(c->yawRadians) || !std::isfinite(c->pitchRadians)) continue;
            desiredRotation = c->twoD ? math::FQuaternion{0,0,0,1}
                : math::FQuaternion::fromEulerAngles({std::clamp(c->pitchRadians,-1.553f,1.553f),c->yawRadians,0});
            auto movement = c->movementInput;
            if (movement.lengthSq() > 1) movement = movement.normalize();
            if (c->twoD) movement.z = 0;
            desiredPosition += desiredRotation * movement * (std::max(0.f,c->moveSpeed) * _dt);
        } else {
            auto* target = c->resolveTarget();
            math::FVector3 targetPosition; math::FQuaternion targetRotation;
            if (!target || !target->worldPose(targetPosition,targetRotation,alpha)
                || !camera_detail::finite(c->offset)) { c->hasPreviousTarget = false; continue; }
            if (!camera_detail::finite(targetPosition)) continue;
            const auto targetHandle = world.getEntityHandle(target->getId());
            if (c->previousTargetHandle != targetHandle) c->hasPreviousTarget = false;
            auto anchor = targetPosition;
            if (c->hasPreviousTarget && std::isfinite(c->lookAheadSeconds))
                anchor += (targetPosition - c->previousTarget) * (std::clamp(c->lookAheadSeconds,0.f,10.f) / _dt);
            c->previousTarget = targetPosition; c->previousTargetName = c->targetName;
            c->previousTargetHandle = targetHandle; c->hasPreviousTarget = true;
            if (c->mode == 2) {
                if (!std::isfinite(c->yawRadians) || !std::isfinite(c->pitchRadians) || !std::isfinite(c->distance)) continue;
                desiredRotation = c->twoD
                    ? math::FQuaternion::fromAxisAngle({0,0,1},c->yawRadians)
                    : math::FQuaternion::fromEulerAngles({std::clamp(c->pitchRadians,-1.553f,1.553f),c->yawRadians,0});
                anchor += c->offset;
                desiredPosition = anchor - desiredRotation * (c->twoD
                    ? math::FVector3{0,std::max(.01f,c->distance),0}
                    : math::FVector3{0,0,std::max(.01f,c->distance)});
            } else desiredPosition = anchor + c->offset;
            c->collisionAnchor = anchor; c->hasCollisionAnchor = camera_detail::finite(anchor);
            if (c->twoD) {
                desiredPosition.z = currentPosition.z;
                const auto delta = desiredPosition - currentPosition;
                const float zoneX = std::isfinite(c->deadZone.x) ? std::max(0.f,c->deadZone.x) : 0;
                const float zoneY = std::isfinite(c->deadZone.y) ? std::max(0.f,c->deadZone.y) : 0;
                desiredPosition.x = currentPosition.x + delta.x - std::clamp(delta.x,-zoneX,zoneX);
                desiredPosition.y = currentPosition.y + delta.y - std::clamp(delta.y,-zoneY,zoneY);
            }
            // Resolve after damping too: a camera must not interpolate back through an obstruction.
            desiredPosition = currentPosition + (desiredPosition - currentPosition) * damping(c->positionDamping,_dt);
            if (!c->twoD && c->collisionEnabled && collision) {
                const auto arm = desiredPosition - anchor;
                const float length = arm.length();
                if (std::isfinite(length) && length > 1e-6f) {
                    const float radius = std::isfinite(c->collisionRadius) ? std::max(0.f,c->collisionRadius) : .2f;
                    const auto requestedEye=desiredPosition;
                    const float allowed = collision({anchor,desiredPosition,radius,entity,target,c->collisionLayerMask,c->ignoreTargetCollision});
                    const float observedDistance=std::isfinite(allowed)&&allowed<length?std::clamp(allowed-std::max(0.f,c->collisionPadding),0.f,length):length;
                    observeCollision({entity->getId(),anchor,requestedEye,anchor+arm*(observedDistance/length),radius,allowed,std::isfinite(allowed)&&allowed>=0});
                    if (!std::isfinite(allowed) || allowed < 0) continue;
                    const float padding = std::isfinite(c->collisionPadding) ? std::max(0.f,c->collisionPadding) : 0;
                    if (allowed < length) desiredPosition = anchor + arm * (std::max(0.f,allowed-padding) / length);
                }
            }
        }
        if (c->boundsEnabled && camera_detail::finite(c->boundsMin) && camera_detail::finite(c->boundsMax)
            && c->boundsMin.x <= c->boundsMax.x && c->boundsMin.y <= c->boundsMax.y && c->boundsMin.z <= c->boundsMax.z) {
            desiredPosition.x = std::clamp(desiredPosition.x,c->boundsMin.x,c->boundsMax.x);
            desiredPosition.y = std::clamp(desiredPosition.y,c->boundsMin.y,c->boundsMax.y);
            desiredPosition.z = std::clamp(desiredPosition.z,c->boundsMin.z,c->boundsMax.z);
        }
        if (!camera_detail::finite(desiredPosition)) continue;
        if (!c->twoD && c->lookAtTarget && c->hasCollisionAnchor
            && (c->collisionAnchor-desiredPosition).lengthSq()>1e-10f)
            desiredRotation=lookRotation(c->collisionAnchor-desiredPosition);
        (void)entity->setWorldPose(desiredPosition,
            currentRotation.slerp(desiredRotation,damping(c->rotationDamping,_dt)).normalize(),alpha);
    }
}

CameraFrame CameraRuntimeSystem::present(const CameraFrame& frame, int32_t viewport, bool twoD,
                                        float duration, bool smooth) {
    auto& state = _transitions[viewKey(viewport,twoD)];
    const float dt = state.tick == _tick ? 0.f : _dt;
    state.tick = _tick;
    auto presented = state.transition.update(frame,dt,duration,smooth);
    if (_owner && !twoD && frame.sourceEntityId) {
        if (auto* entity = _owner->getEntityByHandle({frame.sourceEntityId,frame.sourceEntityVersion}))
            constrainFrame(*entity,presented);
        state.transition.constrainPresentation(presented);
    }
    return presented;
}
void CameraRuntimeSystem::resetView(int32_t viewport, bool twoD) { _transitions.erase(viewKey(viewport,twoD)); }
bool CameraRuntimeSystem::snapshot(int32_t viewport, bool twoD, CameraFrame& output) const noexcept {
    const auto found = _transitions.find(viewKey(viewport,twoD));
    return found != _transitions.end() && found->second.transition.snapshot(output);
}
void CameraRuntimeSystem::constrainFrame(const Entity& entity, CameraFrame& frame) const {
    const auto* c = const_cast<Entity&>(entity).getComponent<CameraControllerComponent>();
    const auto& collision = _collision ? _collision : _defaultCollision;
    if (!collision || !c || !c->collisionEnabled || c->twoD || !c->hasCollisionAnchor || (c->mode != 1 && c->mode != 2)) return;
    const auto arm = frame.position - c->collisionAnchor;
    const float length = arm.length();
    if (!std::isfinite(length) || length <= 1e-6f) return;
    const float radius = std::isfinite(c->collisionRadius) ? std::max(0.f,c->collisionRadius) : .2f;
    const float allowed = collision({c->collisionAnchor,frame.position,radius,&entity,c->resolveTarget(),c->collisionLayerMask,c->ignoreTargetCollision});
    const float observedDistance=std::isfinite(allowed)&&allowed<length?std::clamp(allowed-std::max(0.f,c->collisionPadding),0.f,length):length;
    observeCollision({entity.getId(),c->collisionAnchor,frame.position,c->collisionAnchor+arm*(observedDistance/length),radius,allowed,std::isfinite(allowed)&&allowed>=0});
    if (!std::isfinite(allowed) || allowed < 0 || allowed >= length) return;
    auto candidate = frame;
    const float padding = std::isfinite(c->collisionPadding) ? std::max(0.f,c->collisionPadding) : 0;
    candidate.position = c->collisionAnchor + arm * (std::max(0.f,allowed-padding)/length);
    if (rebuildCameraFrame(candidate)) frame = candidate;
}
CameraRuntimeSystem& cameraRuntime(World& world) {
    if (auto* system = world.findSystemByName("CameraRuntimeSystem")) {
        auto& runtime = *static_cast<CameraRuntimeSystem*>(system); runtime._owner = &world; return runtime;
    }
    world.registerSystem<CameraRuntimeSystem>(CameraRuntimeSystem::kPriority);
    auto& runtime = *static_cast<CameraRuntimeSystem*>(world.findSystemByName("CameraRuntimeSystem"));
    runtime._owner = &world; return runtime;
}
void registerCameraRuntimeSystem() { (void)cameraRuntime(World::instance()); }
void applyCameraEffects(World& world, const Entity& entity, CameraFrame& frame) {
    // Component queries borrow the Entity; effects never retain it.
    const auto* c = const_cast<Entity&>(entity).getComponent<CameraControllerComponent>();
    if (!c) return;
    auto candidate = frame;
    candidate.position += candidate.orientation * c->shakePosition;
    candidate.orientation = candidate.orientation * c->shakeRotation;
    if (rebuildCameraFrame(candidate)) frame = candidate;
    if (auto* runtime = world.findSystemByName("CameraRuntimeSystem"))
        static_cast<CameraRuntimeSystem*>(runtime)->constrainFrame(entity,frame);
}
}
