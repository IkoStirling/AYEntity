#include "AYEntity/TilemapVisibilityRuntime.h"

namespace ayt::entity
{

TilemapVisibilityRuntime& TilemapVisibilityRuntime::instance()
{
    static TilemapVisibilityRuntime runtime;
    return runtime;
}

void TilemapVisibilityRuntime::publish(
    const TilemapCameraVisibility& state) noexcept
{
    const uint64_t nextRevision = _state.revision + 1u;
    _state = state;
    _state.revision = nextRevision;
}

void TilemapVisibilityRuntime::clear() noexcept
{
    TilemapCameraVisibility state{};
    state.revision = _state.revision + 1u;
    _state = state;
}

const TilemapCameraVisibility& TilemapVisibilityRuntime::current() const noexcept
{
    return _state;
}

} // namespace ayt::entity
