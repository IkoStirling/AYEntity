#pragma once

namespace ayt::entity
{

// Headless / server / AYApplication hosts: EntitySubSystem + component
// types only. Does not register render ECS systems or RendererSubSystem.
// Safe when the executable links AYEntity but not AYRenderer.
void bootstrapEntityCore();

// Full ECS render-pipeline hosts (Editor, integration demos): animation +
// skinned/render systems + bootstrapEntityCore(). Does NOT register
// RendererSubSystem — render-capable executables must call
// ayt::render::RendererSubSystem::registerSubSystem() separately.
void bootstrapModule();

void registerEntitySubSystem();
void registerRenderSystem();
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
