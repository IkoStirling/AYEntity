#pragma once

// Presentation-lane grid animation for SpriteComponent. The system consumes
// SpriteAnimationComponent timing and writes only SpriteComponent::sourceRect.
// It runs before SpriteRenderSystem, so the current frame is visible in the
// same presentation update.

#include <AYEntity/IEntity.h>

namespace ayt::entity
{

class SpriteAnimationSystem : public ISystem {
public:
    const char* getName() const override { return "SpriteAnimationSystem"; }
    void onStart() override {}
    void onUpdate(float dt) override;

    static constexpr int kPriority = 470;
};

void registerSpriteAnimationSystem();

} // namespace ayt::entity
