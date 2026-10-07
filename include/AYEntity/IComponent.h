#pragma once
namespace ayt::entity {
class Entity;
/// Minimal component lifecycle, independent of reflection, World and Host headers.
class IComponent {
public:
    virtual ~IComponent() = default;
    virtual const char* getName() const = 0;
    virtual void onAttach(Entity*) {}
    virtual void onDetach() {}
    virtual void onUpdate(float) {}
    virtual void onStart() {}
};
}
