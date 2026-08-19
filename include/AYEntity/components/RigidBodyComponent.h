#pragma once
// AYRigidBody.h - 刚体物理组件

#include <AYEntity/IEntity.h>

namespace ayt::entity
{

// =============================================================================
// RigidBodyComponent - 刚体物理组件
// =============================================================================
class RigidBodyComponent : public IComponent {
public:
    enum class SyncMode : uint8_t {
        None,
        PhysicsToEntity,
        EntityToPhysics
    };

    enum class PhysicsDimension : uint8_t { ThreeD, TwoD };

    const char* getName() const override { return "RigidBody"; }

    // ===== 基础属性 =====
    float getMass() const { return _mass; }
    void setMass(float mass) { _mass = mass; }

    // ===== 速度 =====
    float getVelocityX() const { return _velocityX; }
    float getVelocityY() const { return _velocityY; }
    float getVelocityZ() const { return _velocityZ; }

    void setVelocity(float x, float y, float z) {
        _velocityX = x;
        _velocityY = y;
        _velocityZ = z;
    }

    void addVelocity(float x, float y, float z) {
        _velocityX += x;
        _velocityY += y;
        _velocityZ += z;
    }

    // ===== 力（累计，bridge 每帧 flush 为 ApplyForce 命令后清零） =====
    void applyForce(float x, float y, float z) {
        _forceX += x;
        _forceY += y;
        _forceZ += z;
    }

    float getForceX() const { return _forceX; }
    float getForceY() const { return _forceY; }
    float getForceZ() const { return _forceZ; }

    void clearForces() {
        _forceX = 0.0f;
        _forceY = 0.0f;
        _forceZ = 0.0f;
    }

    // ===== 力矩（累计，bridge 每帧 flush 为 ApplyTorque 命令后清零） =====
    void applyTorque(float x, float y, float z) {
        _torqueX += x;
        _torqueY += y;
        _torqueZ += z;
    }

    float getTorqueX() const { return _torqueX; }
    float getTorqueY() const { return _torqueY; }
    float getTorqueZ() const { return _torqueZ; }

    void clearTorques() {
        _torqueX = 0.0f;
        _torqueY = 0.0f;
        _torqueZ = 0.0f;
    }

    // ===== 物理状态 =====
    bool isStatic() const { return _isStatic; }
    void setStatic(bool isStatic) { _isStatic = isStatic; }

    bool isKinematic() const { return _isKinematic; }
    void setKinematic(bool isKinematic) { _isKinematic = isKinematic; }

    // Opaque AYPhysics BodyHandle binding. Kept as uint32_t here so the ECS
    // component remains serializable without exposing backend types.
    uint32_t getBodyHandle() const { return _bodyHandle; }
    void setBodyHandle(uint32_t handle) { _bodyHandle = handle; }

    SyncMode getSyncMode() const { return _syncMode; }
    void setSyncMode(SyncMode mode) { _syncMode = mode; }

    PhysicsDimension getPhysicsDimension() const { return _physicsDimension; }
    void setPhysicsDimension(PhysicsDimension dimension) { _physicsDimension = dimension; }

    // ===== 碰撞属性 =====
    float getRestitution() const { return _restitution; }
    void setRestitution(float restitution) { _restitution = restitution; }

    float getFriction() const { return _friction; }
    void setFriction(float friction) { _friction = friction; }

    // ===== 重力缩放 =====
    float getGravityScale() const { return _gravityScale; }
    void setGravityScale(float scale) { _gravityScale = scale; }

    // ===== 生命周期回调 =====
    void onStart() override {
        if (_mass <= 0.0f) {
            _mass = 1.0f;
        }
    }

private:
    // ===== 速度 =====
    float _velocityX = 0.0f;
    float _velocityY = 0.0f;
    float _velocityZ = 0.0f;

    // ===== 力（累计） =====
    float _forceX = 0.0f;
    float _forceY = 0.0f;
    float _forceZ = 0.0f;

    // ===== 力矩（累计） =====
    float _torqueX = 0.0f;
    float _torqueY = 0.0f;
    float _torqueZ = 0.0f;

    // ===== 质量 =====
    float _mass = 1.0f;

    // ===== 物理状态 =====
    bool _isStatic = false;
    bool _isKinematic = false;
    uint32_t _bodyHandle = 0;
    SyncMode _syncMode = SyncMode::PhysicsToEntity;
    PhysicsDimension _physicsDimension = PhysicsDimension::ThreeD;

    // ===== 碰撞属性 =====
    float _restitution = 0.3f;
    float _friction = 0.5f;

    // ===== 重力缩放 =====
    float _gravityScale = 1.0f;
};

} // namespace ayt::entity
