#pragma once

#include <AYGameLoop/SubSystemModule.h>

#include <string_view>

namespace ayt::entity
{

inline constexpr std::string_view kEntityRuntimeModuleId =
    "AYEntity.Runtime";

class EntityRuntimeModule final : public ayt::game::SubSystemModule
{
public:
    EntityRuntimeModule();
};

} // namespace ayt::entity
