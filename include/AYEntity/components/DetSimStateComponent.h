#pragma once
#include <AYEntity/IEntity.h>
#include <map>

namespace ayt::entity {
/** @brief Runtime-only canonical state lanes owned by a deterministic session.
 * @note Stable schemas describe typed fields or legacy uint64 words. Gameplay
 * uses DetTickContext typed read/write; binary32 bits are encoded explicitly,
 * never native arithmetic or object memory. Present must not write these blocks.
 * Session snapshots include them; .ayscene does not.
 */
struct DetSimStateComponent final : IComponent {
    const char* getName() const override { return "DetSimStateComponent"; }
    std::map<std::uint32_t,std::vector<std::uint64_t>> blocks;
};
} // namespace ayt::entity
