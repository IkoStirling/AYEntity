#pragma once
#include <AYMath/MathUtils.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ayt::entity {
/// Projection values are append-only; orthographic size is vertical world extent.
enum class CameraProjection : int32_t { Perspective = 0, Orthographic = 1 };
/// Unjittered camera snapshot. Pixel coordinates have a top-left origin; depth is LH [0,1].
struct CameraViewport { float x = 0, y = 0, width = 1, height = 1; };
struct CameraRay { math::FVector3 origin{}, direction{0, 0, 1}; };
struct CameraFrame {
    math::Float4x4 view = math::Float4x4::identity();
    math::Float4x4 projection = math::Float4x4::identity();
    math::FVector3 position{};
    math::FQuaternion orientation{0, 0, 0, 1};
    uint64_t identity = 0, cutGeneration = 0;
    uint32_t layerMask = 0xFFFFFFFFu;
    // Optional World-local source handle; no pointer is retained in a snapshot.
    uint32_t sourceEntityId = 0, sourceEntityVersion = 0;
    CameraProjection projectionMode = CameraProjection::Perspective;
    float fovYDegrees = 50, verticalSize = 10, nearZ = .1f, farZ = 100, aspect = 1;
};

namespace camera_detail {
inline bool finite(const math::FVector3& p) noexcept {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
inline bool finite(const math::Float4x4& m) noexcept {
    for (int i = 0; i < 16; ++i) if (!std::isfinite(m.ptr()[i])) return false;
    return true;
}
inline bool viewportValid(const CameraViewport& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.width)
        && std::isfinite(v.height) && v.width > 0 && v.height > 0;
}
// Solve clip -> world directly in double precision with partial pivoting. This avoids
// relying on the fast general inverse's perspective path and detects singular inputs.
inline bool solveClip(const math::Float4x4& matrix, const math::FVector4& clip,
                      math::FVector4& world) noexcept {
    if (!finite(matrix)) return false;
    double rows[4][5]{};
    const float values[4] = {clip.x,clip.y,clip.z,clip.w};
    for (int row = 0; row < 4; ++row) {
        double scale = 0;
        for (int col = 0; col < 4; ++col) scale = std::max(scale,std::fabs(double(matrix(row,col))));
        if (!std::isfinite(scale) || scale == 0 || !std::isfinite(values[row])) return false;
        for (int col = 0; col < 4; ++col) rows[row][col] = matrix(row,col) / scale;
        rows[row][4] = values[row] / scale;
    }
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 4; ++row)
            if (std::fabs(rows[row][col]) > std::fabs(rows[pivot][col])) pivot = row;
        if (std::fabs(rows[pivot][col]) <= 1e-14) return false;
        for (int entry = 0; entry < 5; ++entry) std::swap(rows[col][entry],rows[pivot][entry]);
        const double divisor = rows[col][col];
        for (int entry = col; entry < 5; ++entry) rows[col][entry] /= divisor;
        for (int row = 0; row < 4; ++row) if (row != col) {
            const double factor = rows[row][col];
            for (int entry = col; entry < 5; ++entry) rows[row][entry] -= factor * rows[col][entry];
        }
    }
    world = {float(rows[0][4]),float(rows[1][4]),float(rows[2][4]),float(rows[3][4])};
    return std::isfinite(world.x) && std::isfinite(world.y) && std::isfinite(world.z) && std::isfinite(world.w);
}
}

/// Rebuild a snapshot from finite pose/lens values; failure leaves it unchanged.
inline bool rebuildCameraFrame(CameraFrame& frame) noexcept {
    if (!camera_detail::finite(frame.position) || !std::isfinite(frame.orientation.dot(frame.orientation))
        || frame.orientation.dot(frame.orientation) < 1e-12f || !std::isfinite(frame.aspect)
        || frame.aspect <= 0 || !std::isfinite(frame.nearZ) || !std::isfinite(frame.farZ)
        || frame.farZ <= frame.nearZ) return false;
    auto candidate = frame;
    candidate.orientation = frame.orientation.normalize();
    const auto rotation = candidate.orientation.toMatrix();
    candidate.view = rotation.transpose() * math::translate(
        -candidate.position.x,-candidate.position.y,-candidate.position.z);
    if (frame.projectionMode == CameraProjection::Orthographic) {
        if (!std::isfinite(frame.verticalSize) || frame.verticalSize <= 0) return false;
        const float half = frame.verticalSize * .5f;
        candidate.projection = math::lh::ortho(-half * frame.aspect, half * frame.aspect,
            -half, half, frame.nearZ, frame.farZ);
    } else {
        if (frame.projectionMode != CameraProjection::Perspective || !std::isfinite(frame.fovYDegrees)
            || frame.fovYDegrees <= 0 || frame.fovYDegrees >= 180 || frame.nearZ <= 0) return false;
        candidate.projection = math::lh::perspective(math::radians(frame.fovYDegrees),
            frame.aspect, frame.nearZ, frame.farZ);
    }
    if (!camera_detail::finite(candidate.view) || !camera_detail::finite(candidate.projection)) return false;
    frame = candidate;
    return true;
}

/// Project to pixels and normalized depth. Behind-camera/nonfinite input fails without writes.
inline bool worldToScreen(const CameraFrame& frame, const CameraViewport& viewport,
                          const math::FVector3& world, math::FVector3& output) noexcept {
    if (!camera_detail::viewportValid(viewport) || !camera_detail::finite(world)
        || !camera_detail::finite(frame.view) || !camera_detail::finite(frame.projection)) return false;
    const auto clip = frame.projection * (frame.view * math::FVector4{world.x, world.y, world.z, 1});
    if (!std::isfinite(clip.w) || clip.w <= 1e-7f) return false;
    const math::FVector3 p{viewport.x + (clip.x / clip.w + 1) * viewport.width * .5f,
        viewport.y + (1 - clip.y / clip.w) * viewport.height * .5f, clip.z / clip.w};
    if (!camera_detail::finite(p)) return false;
    output = p; return true;
}

/// Unproject pixel XY and normalized depth [0,1]; singular transforms fail without writes.
inline bool screenToWorld(const CameraFrame& frame, const CameraViewport& viewport,
                          const math::FVector3& screen, math::FVector3& output) noexcept {
    if (!camera_detail::viewportValid(viewport) || !camera_detail::finite(screen)
        || !camera_detail::finite(frame.view) || !camera_detail::finite(frame.projection)
        || screen.z < 0 || screen.z > 1) return false;
    math::FVector4 viewPoint;
    if (!camera_detail::solveClip(frame.projection,{2 * (screen.x - viewport.x) / viewport.width - 1,
        1 - 2 * (screen.y - viewport.y) / viewport.height,screen.z,1},viewPoint)) return false;
    math::FVector4 p;
    if (!camera_detail::solveClip(frame.view,viewPoint,p)) return false;
    if (!std::isfinite(p.w) || std::fabs(p.w) < 1e-7f) return false;
    const math::FVector3 world{p.x / p.w, p.y / p.w, p.z / p.w};
    if (!camera_detail::finite(world)) return false;
    output = world; return true;
}

/// Perspective rays start at the eye; orthographic rays start on the near plane.
inline bool screenPointToRay(const CameraFrame& frame, const CameraViewport& viewport,
                            const math::FVector2& pixel, CameraRay& output) noexcept {
    math::FVector3 nearPoint, farPoint;
    // An interior perspective sample avoids far-plane float cancellation for very long lenses.
    const float depth = frame.projectionMode == CameraProjection::Perspective ? .5f : 1.f;
    if (!screenToWorld(frame, viewport, {pixel.x, pixel.y, 0}, nearPoint)
        || !screenToWorld(frame, viewport, {pixel.x, pixel.y, depth}, farPoint)) return false;
    const auto origin = frame.projectionMode == CameraProjection::Perspective ? frame.position : nearPoint;
    const auto direction = farPoint - origin;
    if (!camera_detail::finite(origin) || !camera_detail::finite(direction) || direction.lengthSq() < 1e-12f) return false;
    output = {origin, direction.normalize()}; return true;
}

/// Blend pose and lens, never matrices. Projection changes are hard cuts.
inline CameraFrame blendCameraFrames(const CameraFrame& from, const CameraFrame& to, float alpha) noexcept {
    if (from.projectionMode != to.projectionMode) return to;
    alpha = std::isfinite(alpha) ? std::clamp(alpha, 0.f, 1.f) : 1.f;
    CameraFrame result = to;
    result.position = from.position + (to.position - from.position) * alpha;
    result.orientation = from.orientation.slerp(to.orientation, alpha);
    result.fovYDegrees = from.fovYDegrees + (to.fovYDegrees - from.fovYDegrees) * alpha;
    result.verticalSize = from.verticalSize + (to.verticalSize - from.verticalSize) * alpha;
    result.nearZ = from.nearZ + (to.nearZ - from.nearZ) * alpha;
    result.farZ = from.farZ + (to.farZ - from.farZ) * alpha;
    if (!rebuildCameraFrame(result)) return to;
    return result;
}

/// One transition state per viewport/domain; caller advances it exactly once per presentation frame.
class CameraTransition {
public:
    CameraFrame update(const CameraFrame& target, float dt, float duration, bool smooth = true) noexcept {
        if (!_initialized) { _last = target; _initialized = true; }
        if (_last.identity != target.identity) {
            _from = _last; _elapsed = 0;
            _duration = std::isfinite(duration) ? std::clamp(duration, 0.f, 60.f) : 0;
        }
        if ((_last.identity == target.identity && _last.cutGeneration != target.cutGeneration)
            || _last.projectionMode != target.projectionMode) _duration = 0;
        _elapsed += std::isfinite(dt) ? std::max(dt, 0.f) : 0;
        float alpha = _duration > 0 ? std::min(_elapsed / _duration, 1.f) : 1.f;
        if (smooth) alpha = alpha * alpha * (3 - 2 * alpha);
        _last = alpha < 1 ? blendCameraFrames(_from, target, alpha) : target;
        return _last;
    }
    void reset() noexcept { _initialized = false; _duration = 0; }
    /// Raw time fraction before optional smooth easing; observation does not tick.
    float debugProgress() const noexcept { return _duration>0?std::clamp(_elapsed/_duration,0.f,1.f):1.f; }
    float debugDuration() const noexcept { return _duration; }
    /// Copy the last presented frame; before the first sample/reset, fail without writes.
    bool snapshot(CameraFrame& output) const noexcept {
        if (!_initialized) return false;
        output = _last; return true;
    }
    /// Commit external presentation constraints without advancing transition time.
    /// Interrupted transitions then start from the pose that was actually rendered.
    void constrainPresentation(const CameraFrame& frame) noexcept { if (_initialized) _last = frame; }
private:
    CameraFrame _last{}, _from{};
    float _elapsed = 0, _duration = 0;
    bool _initialized = false;
};
}
