#pragma once

#include <AYEntity/ComponentRegistry.h>

#include <memory>

namespace ayt::game
{
class ISubSystem;
}

namespace ayt::entity
{

// Headless/server hosts: EntitySubSystem + core component types only. The
// implementation lives in AYEntityCore and has no renderer/animation/physics
// dependency.
void bootstrapEntityCore();

// Full ECS render-pipeline hosts (Editor, integration demos): animation +
// skinned/render systems + bootstrapEntityCore(). Does NOT register
// RendererSubSystem — render-capable executables must call
// ayt::render::RendererSubSystem::registerSubSystem() separately.
void bootstrapModule();

// Module-first assembly primitives. The factory leaves ownership with the
// caller until it is published through GameLoop. System registration is split
// from subsystem creation so EntityRuntimeModule can keep type registration,
// install, and presentation policy in distinct startup phases.
[[nodiscard]] std::unique_ptr<ayt::game::ISubSystem>
createEntitySubSystem();
void registerEntityCoreSystems();
void registerEntityPresentationSystems();

void registerEntitySubSystem();
void registerRenderSystem();
void registerSimToPresentBridgeSystem();
[[nodiscard]] ComponentRegistryResult registerEntityCoreComponents(
    ComponentRegistry& registry);
// Legacy full-facade registration. New module graphs should add explicit
// Entity*IntegrationModule nodes instead.
[[nodiscard]] ComponentRegistryResult registerEntityComponents(
    ComponentRegistry& registry);
void registerEntityComponents();

// Phase 1 AN-03 + E-04: animation tick + skinned draw submission.
void registerAnimationSystem();
void registerSkinnedMeshRenderSystem();

// P3.1 (2026-08-06): state machine driver. Priority 460, runs AFTER
// AnimationSystem (450) so a transition this frame becomes a play()
// call on AnimationPlayer next frame.
void registerStateMachineSystem();

// CM-3 (2026-08-11): 2D lane — ortho camera driver (405), streaming
// shell (430), animation-tick shell (460), tilemap render (510),
// sprite render (510). register2DSystems() registers all five (the
// bootstrapModule() path); individual registers exist for hosts that
// need explicit control over registration order.
void registerOrthoCameraUpdateSystem();
void registerTilemapStreamingSystem();
void registerTilemapAnimationTickSystem();
void registerTilemapRenderSystem();
void registerSpriteRenderSystem();
void register2DSystems();

} // namespace ayt::entity
