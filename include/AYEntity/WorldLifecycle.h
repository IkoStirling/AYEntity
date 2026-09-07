#pragma once

namespace ayt::entity
{

class World;

// Cross-module teardown extension point. AYEntityCore owns only the callback
// list; integrations such as rendering may release World-owned state without
// introducing a reverse dependency from World to the integration library.
using WorldBeforeShutdownCallback = void (*)(World&) noexcept;

[[nodiscard]] bool registerWorldBeforeShutdownCallback(
    const void* owner,
    WorldBeforeShutdownCallback callback);
void unregisterWorldBeforeShutdownCallback(const void* owner) noexcept;

// Called by World immediately before entities and systems are destroyed.
// Public for lifecycle hosts, but ordinary integrations should only register
// and unregister callbacks through the functions above.
void notifyWorldBeforeShutdown(World& world) noexcept;

} // namespace ayt::entity
