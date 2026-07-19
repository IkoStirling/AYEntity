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

} // namespace ayt::entity
