#pragma once
#include <AYEntity/IEntity.h>
#include <map>

namespace ayt::entity {
/** @brief Runtime-only, registered authoritative word blocks owned by a deterministic session.
 * @note Stable schema/field IDs describe these uint64 words. Store binary32 as
 * explicit bits, not native arithmetic or object memory. Present must not write
 * these blocks. Session snapshots include them; .ayscene does not.
 */
struct DetSimStateComponent final : IComponent {
    const char* getName() const override { return "DetSimStateComponent"; }
    std::map<std::uint32_t,std::vector<std::uint64_t>> blocks;
};
} // namespace ayt::entity
