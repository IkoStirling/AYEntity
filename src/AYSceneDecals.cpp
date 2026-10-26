#include <AYEntity/SceneDecals.h>

#include <AYEntity.h>
#include <AYEntity/World.h>
#include <AYEntity/EntityImpl.h>
#include <AYEntity/components/SceneDecalComponent.h>
#include <AYIO/PathSafety.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace ayt::entity {

SceneDecalBuildResult collectSceneDecals(World& world,
    std::vector<SceneDecalCandidate>& output, float alpha, SpaceRegionId region) {
    SceneDecalBuildResult report;
    struct Candidate {
        SceneDecalCandidate value;
        int32_t priority = 0;
        const std::string* identity = nullptr;
    };
    std::array<Candidate, render::kMaxDeferredDecals> selected{};
    std::size_t count = 0;
    std::uint32_t eligible = 0;
    const auto earlier = [](const Candidate& a, const Candidate& b) {
        return a.priority > b.priority
            || (a.priority == b.priority && *a.identity < *b.identity);
    };
    for (auto* object : world.query<SceneDecalComponent>()) {
        const auto* component = object->getComponent<SceneDecalComponent>();
        ++report.authored;
        if (region && object->effectiveSpaceRegion() != region) continue;
        if (!component->enabled || component->opacity == 0) {
            ++report.disabled;
            continue;
        }
        const auto portable = [](const std::string& path) {
            return path.empty() || ayt::io::path::isPortableRelative(path);
        };
        if (!component->valid() || !std::isfinite(alpha)
            || !portable(component->albedoPath)
            || !portable(component->normalPath)) {
            ++report.invalid;
            continue;
        }
        auto transform = object->worldMatrix(std::clamp(alpha, 0.0f, 1.0f));
        if (!render::validDeferredDecalTransform(transform)) {
            ++report.invalid;
            continue;
        }
        Candidate candidate;
        candidate.priority = component->priority;
        candidate.identity = &object->persistentId();
        candidate.value.albedoPath = component->albedoPath;
        candidate.value.normalPath = component->normalPath;
        auto& decal = candidate.value.decal;
        decal.localToWorld = transform;
        decal.color = component->color;
        decal.opacity = component->opacity;
        decal.normalStrength = component->normalStrength;
        decal.roughness = component->roughness;
        decal.metallic = component->metallic;
        decal.materialStrength = component->materialStrength;
        decal.minFacing = component->minFacing;
        ++eligible;
        std::size_t at = 0;
        while (at < count && !earlier(candidate, selected[at])) ++at;
        if (at < selected.size()) {
            const auto limit = std::min(count, selected.size()-1);
            for (auto i = limit; i > at; --i) selected[i] = std::move(selected[i-1]);
            selected[at] = std::move(candidate);
            count = std::min(count+1, selected.size());
        }
    }
    std::vector<SceneDecalCandidate> next;
    next.reserve(count);
    for (std::size_t i=0; i<count; ++i)
        next.push_back(std::move(selected[i].value));
    output.swap(next);
    report.emitted = static_cast<std::uint32_t>(count);
    report.overflow = eligible - report.emitted;
    return report;
}

} // namespace ayt::entity
