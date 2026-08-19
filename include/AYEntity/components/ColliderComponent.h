#pragma once
// AYEntity/components/ColliderComponent.h - collider (shape) component.
//
// One ColliderComponent per entity = the full shape set of the entity's
// physics body (the body itself is owned by RigidBodyComponent). The shape
// list lives INSIDE a single component instead of one-component-per-shape
// because AYEntity storage is single-instance-per-type; a vector of specs
// gives compound bodies (capsule = sphere+cylinder, characters = several
// boxes) with no storage rework.
//
// The bridge (EntityPhysicsBridge) maps these specs to
// ayt::physics::ColliderDesc at FixedPrePhysics. No physics types leak
// into this component: shapes are described with plain math/primitive
// fields only.
//
// Revision semantics match Transform::revision: the mutators below bump it;
// direct writes to the `shapes` vector (push_back etc.) bypass it, so the
// bridge keeps a deep-compare fallback.

#include <AYEntity/IEntity.h>

#include <AYMath/MathTypes.h>

#include <cstdint>
#include <vector>

namespace ayt::entity
{

// Shape kinds available to an ECS entity. Values intentionally mirror
// ayt::physics::ColliderShape (Box/Sphere/Capsule) but the enum is
// independent so the component stays physics-free. The bridge maps via an
// explicit switch — do not rely on numeric coincidence across layers.
enum class ColliderShapeType : uint8_t {
    Box     = 0,
    Sphere  = 1,
    Capsule = 2,
};

// One collision shape spec. Editor/scene authored; the bridge consumes it.
//
// NOTE: no local offset field — AYPhysics ColliderDesc has no shape
// transform yet (Jolt backend offsets shapes by identity). Add offset here
// when the physics layer grows shape-local transforms.
#define AY_CURRENT_CLASS ColliderShapeSpec
struct ColliderShapeSpec {
    AY_PROPERTY(ColliderShapeType, shape,        kAttrSerialize)
    AY_PROPERTY(ayt::math::FVector3, halfExtents, kAttrSerialize)  // Box only
    AY_PROPERTY(float,             radius,       kAttrSerialize)  // Sphere / Capsule
    AY_PROPERTY(float,             height,       kAttrSerialize)  // Capsule
    AY_PROPERTY(bool,              isTrigger,    kAttrSerialize)
    AY_PROPERTY(float,             friction,     kAttrSerialize)
    AY_PROPERTY(float,             restitution,  kAttrSerialize)

    ColliderShapeSpec() {
        shape       = ColliderShapeType::Box;
        halfExtents = ayt::math::FVector3(0.5f, 0.5f, 0.5f);
        radius      = 0.5f;
        height      = 1.0f;
        isTrigger   = false;
        friction    = 0.5f;
        restitution = 0.3f;
    }
};
#undef AY_CURRENT_CLASS

// AY_FINALIZE_REGISTRATION_METADATA(ColliderShapeSpec) + ColliderComponent
// live in src/AYEntityReflection.cpp (single TU — see that file's header
// comment about duplicate static finalizers corrupting the CRT heap).

#define AY_CURRENT_CLASS ColliderComponent
struct ColliderComponent : public IComponent {
    const char* getName() const override { return "ColliderComponent"; }

    // Full shape set of the physics body. Direct vector edits (push_back,
    // operator[]) bypass `revision`; the bridge deep-compares as fallback.
    AY_PROPERTY(std::vector<ColliderShapeSpec>, shapes, kAttrSerialize)

    // Monotonic write counter bumped by the mutators below. Runtime-only;
    // not serialized. See the header comment for fallback semantics.
    uint32_t revision = 0;

    ColliderComponent() = default;

    // Append a default-shaped spec and return it for field tweaking.
    // Bumps revision.
    ColliderShapeSpec& addShape() {
        shapes.emplace_back();
        ++revision;
        return shapes.back();
    }

    // Remove the spec at index. Bumps revision. No-op on out-of-range.
    void removeShape(size_t index) {
        if (index < shapes.size()) {
            shapes.erase(shapes.begin() + static_cast<long>(index));
            ++revision;
        }
    }

    // Remove every shape. Bumps revision.
    void clearShapes() {
        if (!shapes.empty()) {
            shapes.clear();
            ++revision;
        }
    }

    // Empty shape list = no colliders; the bridge skips creation.
    bool isValid() const { return !shapes.empty(); }
};
#undef AY_CURRENT_CLASS

} // namespace ayt::entity
