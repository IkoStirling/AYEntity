#pragma once
// AYHealth.h - health component

#include <AYCore.h>
#include <IAYEntity.h>

#include <algorithm>
#include <functional>

namespace ayt::entity
{

#define AY_CURRENT_CLASS HealthComponent
class HealthComponent : public IComponent {
public:
    const char* getName() const override { return "Health"; }

    HealthComponent()
    {
        currentHp = 100;
        maxHp     = 100;
    }

    AY_PROPERTY(Int32, currentHp, kAttrSerializeNetReplicate)
    AY_PROPERTY(Int32, maxHp, kAttrSerialize)

    void onStart() override
    {
        if (currentHp <= 0) {
            currentHp = maxHp;
        }
    }

    Int32 getHp() const { return currentHp; }
    Int32 getMaxHp() const { return maxHp; }
    float getHpPercent() const
    {
        return (maxHp > 0) ? static_cast<float>(currentHp) / static_cast<float>(maxHp) : 0.0f;
    }

    void setHp(Int32 hp) { currentHp = std::max(0, std::min(hp, maxHp)); }
    void setMaxHp(Int32 hpMax) { maxHp = std::max(1, hpMax); }

    bool isDead() const { return currentHp <= 0; }
    bool isFull() const { return currentHp >= maxHp; }

    void heal(Int32 amount)
    {
        if (amount > 0) {
            currentHp = std::min(currentHp + amount, maxHp);
        }
    }

    void damage(Int32 amount)
    {
        if (amount > 0) {
            currentHp = std::max(currentHp - amount, 0);
        }
    }

    void healPercent(float percent)
    {
        heal(static_cast<Int32>(maxHp * percent));
    }

    void damagePercent(float percent)
    {
        damage(static_cast<Int32>(maxHp * percent));
    }

    void restore() { currentHp = maxHp; }
    void kill() { currentHp = 0; }

    std::function<void(HealthComponent*, Int32 oldHp, Int32 newHp)> onHpChanged;
    std::function<void(HealthComponent*)> onDeath;
};
#undef AY_CURRENT_CLASS

AY_FINALIZE_REGISTRATION_METADATA(HealthComponent)

} // namespace ayt::entity
