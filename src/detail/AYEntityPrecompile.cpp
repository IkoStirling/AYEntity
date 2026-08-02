// AYEntityPrecompile.cpp — AYEntity explicit-instantiation barrel (PR-follow-up to AYScene PR-1)
//
// 背景：AYEntity/include/AYWorld.h 与 src/AYComponentFactory.cpp 通过
// anonymous-namespace `addTyped<T>/getTyped<T>/hasTyped<T>` 三个 helper
// 调用 `Entity::addComponent<T>/getComponent<T>/hasComponent<T>` 这三个
// member template。AYEntity/lib 自身没有任何 TU 调过这些 template 实例，
// 所以 lib 的 .text 段**缺**对应符号。任何静态链接 AYEntity.lib 且不
// 直接 `addComponent<HealthComponent>` 等的下游 target（如同名自家
// `AYTest_SceneSerializer.cpp` 用了 `addComponent<>` 时没事，因为它本身
// 是 caller；但纯 import lib 不读 component 的下游 100% 撞 LNK2019
// unresolved）—— 参见 ay-scene.md "PR-1 实修 LNK2019" 段。
//
// 本 TU 是 AYEntity 自己把缺失的 12 个 template instance 显式生出来。
// 加进 AYEntity CMakeLists.txt SOURCES 之后，AYScene 的
// `Test_EntityLinkGlue.cpp`（PR-1 临时 cross-module glue）可删。
//
// 设计选择：把 precompile 集中在 src/detail/，与 AYRenderer/AY2D 的
// `src/detail/*` 同形态，避免污染 SOURCES 顶层公共接口。

#include <AYEntity.h>
#include <AYComponentFactory.h>

#include <components/AYHealthComponent.h>
#include <components/AYMeshComponent.h>
#include <components/AYSkeletonComponent.h>
#include <components/AYAnimationComponent.h>
#include <components/AYTransformComponent.h>

namespace ayt::entity
{

// -----------------------------------------------------------------------------
// Explicit template instantiation declarations for Entity::addComponent<T>,
// getComponent<T>, hasComponent<T> over the AY_ENTITY_PRECOMPILE_COMPONENTS
// set (see AYEntity CMakeLists.txt). These force the compiler to emit a
// definition for each <(T)> in THIS translation unit's object file, so
// downstream static-linkers see the symbols in AYEntity.lib and never
// raise LNK2019 for the corresponding component type.
// -----------------------------------------------------------------------------

// --- HealthComponent ---------------------------------------------------------
template HealthComponent* Entity::addComponent<HealthComponent>();
template HealthComponent* Entity::getComponent<HealthComponent>();
template bool Entity::hasComponent<HealthComponent>() const;

// --- MeshComponent -----------------------------------------------------------
template MeshComponent* Entity::addComponent<MeshComponent>();
template MeshComponent* Entity::getComponent<MeshComponent>();
template bool Entity::hasComponent<MeshComponent>() const;

// --- SkeletonComponent -------------------------------------------------------
template SkeletonComponent* Entity::addComponent<SkeletonComponent>();
template SkeletonComponent* Entity::getComponent<SkeletonComponent>();
template bool Entity::hasComponent<SkeletonComponent>() const;

// --- AnimationComponent ------------------------------------------------------
template AnimationComponent* Entity::addComponent<AnimationComponent>();
template AnimationComponent* Entity::getComponent<AnimationComponent>();
template bool Entity::hasComponent<AnimationComponent>() const;

// --- Transform (NOT TransformComponent) --------------------------------------
// Note: class `Transform` lives in <components/AYTransformComponent.h>; the
// C++ name does NOT carry a "Component" suffix. Pre-include guard above
// ensures the header is parsed; we instantiate the same three members.
template Transform* Entity::addComponent<Transform>();
template Transform* Entity::getComponent<Transform>();
template bool Entity::hasComponent<Transform>() const;

} // namespace ayt::entity
