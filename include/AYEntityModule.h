#pragma once



namespace ayt::entity

{



// Registers EntitySubSystem, RenderSystem, and component types.

// Call once before GameLoop::run() when using AYEntity as a static library.

void bootstrapModule();



void registerEntitySubSystem();

void registerRenderSystem();

void registerEntityComponents();



} // namespace ayt::entity

