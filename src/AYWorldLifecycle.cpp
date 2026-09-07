#include <AYEntity/WorldLifecycle.h>

#include <algorithm>
#include <mutex>
#include <vector>

namespace ayt::entity
{
namespace
{

struct CallbackEntry
{
    const void* owner = nullptr;
    WorldBeforeShutdownCallback callback = nullptr;
};

std::mutex g_callbackMutex;
std::vector<CallbackEntry> g_callbacks;

} // namespace

bool registerWorldBeforeShutdownCallback(
    const void* owner,
    WorldBeforeShutdownCallback callback)
{
    if (owner == nullptr || callback == nullptr) {
        return false;
    }

    std::lock_guard lock(g_callbackMutex);
    const auto existing = std::find_if(
        g_callbacks.begin(), g_callbacks.end(),
        [owner](const CallbackEntry& entry) {
            return entry.owner == owner;
        });
    if (existing != g_callbacks.end()) {
        existing->callback = callback;
        return true;
    }
    g_callbacks.push_back({owner, callback});
    return true;
}

void unregisterWorldBeforeShutdownCallback(const void* owner) noexcept
{
    if (owner == nullptr) {
        return;
    }
    try {
        std::lock_guard lock(g_callbackMutex);
        std::erase_if(g_callbacks, [owner](const CallbackEntry& entry) {
            return entry.owner == owner;
        });
    } catch (...) {
        // Teardown must remain noexcept. A failed lock can only leave an
        // already-registered callback alive until process shutdown.
    }
}

void notifyWorldBeforeShutdown(World& world) noexcept
{
    std::vector<CallbackEntry> callbacks;
    try {
        std::lock_guard lock(g_callbackMutex);
        callbacks = g_callbacks;
    } catch (...) {
        return;
    }

    for (const CallbackEntry& entry : callbacks) {
        if (entry.callback != nullptr) {
            entry.callback(world);
        }
    }
}

} // namespace ayt::entity
