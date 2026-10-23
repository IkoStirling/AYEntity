#pragma once
#include <AYEntity/IEntity.h>
#include <AYRenderer/RenderScene.h>
#include <AYParticle/Particle.h>
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
    struct ImpactVisual {
        particle::ParticleInstance instance;
        particle::Pose pose;
        int32_t layer=0,sortingKey=0;
    };
    const std::deque<ImpactVisual>& impactVisuals() const noexcept { return _impactVisuals; }
    const char* getName() const override { return "ParticleSimulationSystem"; }
    /// Optional world-space gravity for CPU 3D particles. Caller owns query;
    /// clear it before destroying the provider. GPU emitters fall back to CPU.
    void setGravityQuery(const particle::GravityQuery3D* query) noexcept { _gravityQuery=query; }
    const particle::GravityQuery3D* gravityQuery() const noexcept { return _gravityQuery; }
    void onUpdate(float dt) override;
private:
    const particle::GravityQuery3D* _gravityQuery=nullptr;
    std::deque<ImpactVisual> _impactVisuals;
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
