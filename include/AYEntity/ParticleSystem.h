#pragma once
#include <AYEntity/IEntity.h>
#include <AYRenderer/RenderScene.h>
#include <deque>
#include <unordered_map>
#include <memory>
namespace ayt::entity {
class ParticleGpuState;
/// Presentation-only visual simulation. A world's default live budget is 100000 particles.
class ParticleSimulationSystem : public ISystem {
public:
    static constexpr int kPriority=470;
    uint32_t maxParticles=100000;
    uint32_t liveParticles=0;
    uint32_t gpuEmitters=0,gpuReservedParticles=0;
    const char* getName() const override { return "ParticleSimulationSystem"; }
    void onUpdate(float dt) override;
};
class ParticleRenderSystem : public ISystem {
public:
    static constexpr int kPriority=515;
    ~ParticleRenderSystem() override;
    const char* getName() const override { return "ParticleRenderSystem"; }
    void onStart() override;
    void onUpdate(float) override {}
    void buildRenderScene(render::RenderScene& scene);
    std::shared_ptr<ParticleGpuState> gpuState() const { return _gpuState.lock(); }
private:
    struct Batch { render::ParticleDrawData geometry; render::DrawPayload2D payload; };
    struct Material { render::TextureHandle texture; render::MaterialHandle material; };
    std::deque<Batch> _batches;
    std::unordered_map<std::string,Material> _materials;
    render::MeshHandle _quad;
    /// Renderer-owned scene callback retains GPU state and releases it before device shutdown.
    std::weak_ptr<ParticleGpuState> _gpuState;
};
}
