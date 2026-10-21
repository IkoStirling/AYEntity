#pragma once
#include <AYEntity.h>
#ifdef AY_ENTITY_RENDER_BENCHMARK_BASELINE
#include <AYEntityBaseline/RenderSystem.h>
#include <AYEntityBaseline/SkinnedMeshRenderSystem.h>
#else
#include <AYEntity/RenderSystem.h>
#include <AYEntity/SkinnedMeshRenderSystem.h>
#endif
#include <AYEntity/components/MeshComponent.h>
#include <AYEntity/components/SkeletonComponent.h>
#include <AYRenderer/RendererSubSystem.h>
#include <AYRenderer/PbrShaderSources.h>
#include <AYGameLoop/SubSystemRegistry.h>
#include <AYResource/ResourceManager.h>
#include <AYResource/AssetPath.h>
#include <AYResource/assetsImpl/Mesh.h>
#include <AYResource/assetsImpl/Material.h>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace {
using namespace ayt;
#ifdef AY_ENTITY_RENDER_BENCHMARK_BASELINE
using FixtureRenderSystem=entity::baseline::RenderSystem;
using FixtureSkinnedRenderSystem=entity::baseline::SkinnedMeshRenderSystem;
#else
using FixtureRenderSystem=entity::RenderSystem;
using FixtureSkinnedRenderSystem=entity::SkinnedMeshRenderSystem;
#endif

struct SubmissionFixture {
    std::filesystem::path directory = std::filesystem::temp_directory_path()
        / ("ay-render-submission-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    entity::World& world = entity::World::processWorld();
    entity::World* previous = entity::World::activeWorld();
    std::vector<std::string> previousRoots=resource::assetRoots();
    render::RendererSubSystem* subsystem = nullptr;
    FixtureRenderSystem system;
    FixtureSkinnedRenderSystem skinnedSystem;
    bool isSkinned = false;
    bool planarGeometry = false;
    entity::MeshComponent* component = nullptr;
    entity::SkeletonComponent* skeleton = nullptr;
    bool ready = false;

    static bool write(const std::filesystem::path& path, const void* data, size_t bytes) {
        std::ofstream stream(path, std::ios::binary);
        stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes));
        return stream.good();
    }
    template<class T> bool save(const char* name, const T& asset) {
        std::vector<resource::UInt8> bytes;
        return asset.saveToBinary(bytes) && write(directory / name, bytes.data(), bytes.size());
    }
    std::string meshPath() const { return (directory / "sections.msh").generic_string(); }
    bool writeMesh(uint32_t indicesPerSection,bool newPalette=false,bool flippedSlots=false) {
        resource::Mesh mesh;
        if(planarGeometry) {
            const float vertices[]={
                -1.2f,-.8f,0,0,0,-1,0,1, -1.2f,.8f,0,0,0,-1,0,0,
                -.1f,.8f,0,0,0,-1,1,0, -.1f,-.8f,0,0,0,-1,1,1,
                .1f,-.8f,0,0,0,-1,0,1, .1f,.8f,0,0,0,-1,0,0,
                1.2f,.8f,0,0,0,-1,1,0, 1.2f,-.8f,0,0,0,-1,1,1};
            mesh._setForTestVertexLayout(7,8,32);mesh._setForTestVertexData(vertices,sizeof(vertices));
        } else {
            resource::Mesh cube;cube.createCube();
            mesh._setForTestVertexLayout(cube.getAttributeMask(),cube.getVertexCount(),cube.getVertexStride());
            mesh._setForTestVertexData(cube.getVertexData(),size_t(cube.getVertexCount())*cube.getVertexStride());
        }
        const uint32_t full[]={0,1,2,0,2,3,4,5,6,4,6,7};
        const uint32_t halves[]={0,1,2,4,5,6};
        mesh._setForTestIndices(planarGeometry&&indicesPerSection==3?halves:full,indicesPerSection*2);
        const resource::IMesh::Submesh sections[] = {
            {0,indicesPerSection,flippedSlots?1u:0u,0},
            {indicesPerSection,indicesPerSection,flippedSlots?0u:1u,0}};
        mesh._setForTestSubmeshes(sections,2);
        mesh._addForTestMaterialSlot("surface.mat");
        mesh._addForTestMaterialSlot(planarGeometry?"surface-b.mat":"surface.mat");
        if(isSkinned) {
            resource::VertexSkinWeight weight{};weight.boneWeight[0]=1;
            mesh.debugSetSkinWeights(std::vector<resource::VertexSkinWeight>(mesh.getVertexCount(),weight));
            mesh._setForTestSkinPalettes({{0,1},{1,1}},newPalette?std::vector<uint32_t>{9,12}:std::vector<uint32_t>{2,5});
        }
        return save("sections.msh",mesh);
    }
    explicit SubmissionFixture(bool skinned=false,render::Backend backend=render::Backend::Noop,
                               void* window=nullptr,bool planar=false) {
        isSkinned=skinned;planarGeometry=planar;
        std::filesystem::create_directory(directory);
        auto& manager=resource::ResourceManager::instance();
        manager.unloadAll();manager.setAutoWatchLoadedResources(false);
        world.shutdown();if(!world.initialize())return;
        entity::World::setActiveWorld(&world);
        constexpr char shader[] = R"(
material SubmissionTest {
    vertex {
        in pos : position
        return modelViewProjection * vec4(pos, 1.0)
    }
    fragment { return vec4(0.3, 0.7, 0.2, 1.0) }
}
)";
        const std::string shaderText=planarGeometry?render::kPbrPhoskiaSource:shader;
        if(!write(directory/"surface.phoskia",shaderText.data(),shaderText.size()))return;
        resource::Material material;material.setShader("surface.phoskia");
        if(planarGeometry){
            material.setColor("baseColor",{.9f,.08f,.04f,1});
            material.setFloat("metallic",0);material.setFloat("roughness",.8f);
            material.setSurfaceProperties(resource::MaterialAlphaMode::Opaque,.5f,true);
        }
        if(!save("surface.mat",material)||!writeMesh(3))return;
        if(planarGeometry){material.setColor("baseColor",{.04f,.1f,.9f,1});if(!save("surface-b.mat",material))return;}
        render::RendererSubSystem::setWindowProvider({});
        render::RendererSubSystem::setBootstrapBackend(backend);
        static int sentinel=0;
        render::RendererSubSystem::setBootstrapWindow(window?window:&sentinel,window?320:64,window?240:64);
        render::RendererSubSystem::setBootstrapViewport(0,0,window?320:64,window?240:64);
        subsystem=new render::RendererSubSystem();
        game::SubSystemRegistry::instance().registerSubSystem(subsystem);
        if(!subsystem->initialize())return;
        if(isSkinned)skinnedSystem.onStart();else system.onStart();
        auto* object=world.createSpatialEntity();
        component=object->createComponent<entity::MeshComponent>();
        if(!component)return;
        component->meshPath=meshPath();
        component->skinned=isSkinned;
        if(isSkinned) {
            skeleton=object->createComponent<entity::SkeletonComponent>();
            if(!skeleton)return;
            skeleton->skinMatrices=new math::Float4x4[32];
            for(unsigned i=0;i<32;++i)skeleton->skinMatrices[i]=math::Float4x4::identity();
            skeleton->jointCount=32;skeleton->loaded=true;
        }
        // Empty override selects the asset's two material slots.
        subsystem->update(0);
        ready=subsystem->renderScene().items().size()==2;
    }
    ~SubmissionFixture() {
        if(subsystem){
            subsystem->clearSceneBuildersForOwner(&world);
            subsystem->shutdown();
            game::SubSystemRegistry::instance().unregisterSubSystem("Renderer");
        }
        world.shutdown();entity::World::setActiveWorld(previous);
        resource::ResourceManager::instance().unloadAll();
        resource::setAssetRoots(previousRoots);
        for(const char* name:{"sections.msh","surface.mat","surface-b.mat","surface.phoskia","override.mat","decoy.msh"}) {
            std::error_code ignored;std::filesystem::remove(directory/name,ignored);
        }
        std::error_code ignored;std::filesystem::remove(directory,ignored);
    }
};
}
