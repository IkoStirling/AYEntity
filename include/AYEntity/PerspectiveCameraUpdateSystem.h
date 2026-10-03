#pragma once
#include <AYEntity/IEntity.h>
namespace ayt::entity {
// Registers a World-scoped provider; evaluated before geometry extraction,
// even in Edit preview (which does not tick gameplay simulation).
class PerspectiveCameraUpdateSystem : public ISystem {
public:
    const char* getName() const override { return "PerspectiveCameraUpdateSystem"; }
    void onStart() override;
    void onUpdate(float) override {}
    static constexpr int kPriority = 404;
};
void registerPerspectiveCameraUpdateSystem();
}
