#pragma once



namespace ayt::entity

{



// Registers EntitySubSystem, RenderSystem, and component types.

// Call once before GameLoop::run() when using AYEntity as a static library.

void bootstrapModule();



void registerEntitySubSystem();

void registerRenderSystem();

void registerEntityComponents();

// Phase 1 AN-03 + E-04: animation tick + skinned draw submission.
void registerAnimationSystem();
void registerSkinnedMeshRenderSystem();



} // namespace ayt::entity

