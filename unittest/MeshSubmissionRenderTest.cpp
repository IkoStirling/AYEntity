#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
// AYCore's legacy Memory convenience macro must not rewrite bgfx::Memory.
#include <bgfx/bgfx.h>
#include "RenderSubmissionFixture.h"
#include <AYEntity/EntityModule.h>
#include <AYRenderer/MeshSubmissionInfo.h>
#include "detail/TextureImageLoader.h"
#include <array>
#include <iostream>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {
void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
struct HiddenWindow {
    HWND handle=CreateWindowExW(0,L"STATIC",L"Entity mesh submission acceptance",WS_OVERLAPPEDWINDOW,
        0,0,320,240,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    ~HiddenWindow(){if(handle)DestroyWindow(handle);}
};
using Image=render::detail::DecodedImage;

void configure(SubmissionFixture& fixture) {
    auto& renderer=fixture.subsystem->renderer();
    renderer.setMsaaSampleCount(0);renderer.setShadowsEnabled(false);
    renderer.setFxaaEnabled(false);renderer.setSmaaEnabled(false);renderer.setTaaEnabled(false);
    renderer.setAutoExposureEnabled(false);renderer.setPostProcessTonemapMode(render::Renderer::TonemapMode::None);
    renderer.setDirectionalLight({.35f,-.85f,-.4f},{1.35f,1.28f,1.15f});
    renderer.setSimulationTimeSeconds(1);
    fixture.component->castShadow=false;fixture.component->receiveShadow=false;
    fixture.subsystem->setCameraLookAt({0,0,-4},{0,0,0},{0,1,0},50);
}
void frame(render::Renderer& renderer,const render::RenderScene& scene,const std::string& capture={}) {
    MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
    renderer.beginFrame();renderer.render(scene);
    if(!capture.empty())require(renderer.captureScreenshot(capture),"queue native scene capture");
    renderer.endFrame();
}
Image capture(render::Renderer& renderer,const render::RenderScene& scene,
              const std::filesystem::path& output,const char* name) {
    for(int i=0;i<10;++i)frame(renderer,scene);
    const auto path=(output/name).generic_string();frame(renderer,scene,path);
    for(int i=0;i<8;++i)frame(renderer,scene);
    auto image=render::detail::decodeImageFile(path);
    require(image.isValid()&&image.width==320&&image.height==240,"decode complete native capture");
    return image;
}
size_t difference(const Image& a,const Image& b) {
    require(a.width==b.width&&a.height==b.height&&a.rgba8.size()==b.rgba8.size(),"compare equal-sized captures");
    size_t changed=0;
    for(size_t i=0;i<a.rgba8.size();i+=4){
        bool different=false;
        for(size_t channel=0;channel<3;++channel)
            different=different||std::abs(int(a.rgba8[i+channel])-int(b.rgba8[i+channel]))>1;
        changed+=different?1:0;
    }
    return changed;
}
size_t dominantPixels(const Image& image,bool red) {
    size_t count=0;
    for(size_t i=0;i<image.rgba8.size();i+=4){
        const int r=image.rgba8[i],g=image.rgba8[i+1],b=image.rgba8[i+2];
        // The semantic comparison is channel dominance under the fixed light,
        // independent of an arbitrary absolute brightness threshold.
        if(red?2*r>3*g+6&&2*r>3*b+6:2*b>3*r+6&&2*b>3*g+6)++count;
    }
    return count;
}
render::RenderScene reference(SubmissionFixture& fixture,uint32_t count,bool newPalette,bool flippedSlots=false) {
    render::RenderScene scene(render::RenderScene::DrawDataOwnership::OwnedCpu);
    auto& renderer=fixture.subsystem->renderer();
    const auto mesh=renderer.loadMesh(fixture.meshPath());
    const std::array<uint32_t,2> palette=newPalette?std::array<uint32_t,2>{9,12}:std::array<uint32_t,2>{2,5};
    for(uint32_t section=0;section<2;++section){
        render::DrawItem item;item.mesh=mesh;
        item.material=renderer.loadMaterial((fixture.directory/((section^(flippedSlots?1u:0u))?"surface-b.mat":"surface.mat")).generic_string());
        require(item.material.isValid(),"reference slot material");
        item.firstIndex=section*count;item.indexCount=count;item.shadowFlags=static_cast<render::ShadowFlags>(0);
        if(fixture.isSkinned){
            item.boneMatrices=fixture.skeleton->skinMatrices;item.skeletonJointCount=32;
            item.boneRemap=&palette[section];item.jointCount=1;
        }
        scene.add(item);
    }
    return scene;
}

void runCase(const std::filesystem::path& output,render::Backend backend,HWND window,bool skinned) {
    require(std::filesystem::create_directory(output),"use a fresh case output directory");
    SubmissionFixture fixture(skinned,backend,window,true);require(fixture.ready,"extract two production scene sections");
    require(bgfx::getRendererType()==(backend==render::Backend::Direct3D11?bgfx::RendererType::Direct3D11:bgfx::RendererType::Direct3D12),"selected real backend is active");
    auto& renderer=fixture.subsystem->renderer();configure(fixture);
    // New palette entries intentionally have different transforms. Comparing
    // to the old palette later proves the real shader consumes section remaps.
    if(skinned){fixture.skeleton->skinMatrices[9].row[1].w=.3f;fixture.skeleton->skinMatrices[12].row[1].w=-.2f;}
    fixture.subsystem->update(0);
    const auto empty=capture(renderer,render::RenderScene{},output,"empty.png");
    const auto opened=capture(renderer,fixture.subsystem->renderScene(),output,"opened.png");
    require(difference(opened,empty)>1000,"opened production scene has visible geometry");
    require(dominantPixels(opened,true)>300&&dominantPixels(opened,false)>300,"both material slots are visibly distinct");
    const auto initialReference=capture(renderer,reference(fixture,3,false),output,"opened-reference.png");
    require(difference(opened,initialReference)==0,"opened scene matches independent section/slot reference");
    const auto initialMetadata=renderer.meshSubmissionInfo(renderer.loadMesh(fixture.meshPath()));
    require(initialMetadata&&initialMetadata->sections.size()==2,"uploaded initial metadata");

    require(fixture.writeMesh(6,true),"write expanded sections and new palette");
    resource::ResourceManager::instance().unloadResource(fixture.meshPath());
    fixture.subsystem->update(0);
    const auto l2Only=capture(renderer,fixture.subsystem->renderScene(),output,"l2-only.png");
    require(difference(opened,l2Only)==0,"L2 eviction/new file cannot reinterpret resident GPU ranges");
    require(renderer.meshSubmissionInfo(renderer.loadMesh(fixture.meshPath()))==initialMetadata,"L2-only change preserves GPU snapshot");

    std::string error;
    require(resource::ResourceManager::instance().refreshPublishedResources({fixture.meshPath()},&error),"publish expanded mesh to runtime");
    fixture.subsystem->update(0);
    const auto full=capture(renderer,fixture.subsystem->renderScene(),output,"expanded.png");
    const auto fullReference=capture(renderer,reference(fixture,6,true),output,"expanded-reference.png");
    require(difference(full,fullReference)==0,"expanded scene matches independent six-index sections and new palette");
    const size_t expandedDifference=difference(opened,full);
    require(expandedDifference>1000,"expanded section geometry visibly changes");
    if(skinned){
        const auto wrongPalette=capture(renderer,reference(fixture,6,false),output,"old-palette-reference.png");
        require(difference(full,wrongPalette)>500,"real GPU applies updated bone remap");
    }

    require(fixture.writeMesh(6,true,true),"write reordered material slot indices");
    require(resource::ResourceManager::instance().refreshPublishedResources({fixture.meshPath()},&error),"publish reordered slots");
    fixture.component->materialPath=(fixture.directory/"surface.mat").generic_string();
    fixture.subsystem->update(0);
    const auto reordered=capture(renderer,fixture.subsystem->renderScene(),output,"slots-reordered.png");
    const auto reorderedReference=capture(renderer,reference(fixture,6,true,true),output,"slots-reordered-reference.png");
    require(difference(reordered,reorderedReference)==0,"asset slot indices survive first-slot default material selection");
    require(difference(full,reordered)>1000,"reordered slots visibly exchange section colors");
    require(fixture.writeMesh(6,true),"restore slot ordering");
    require(resource::ResourceManager::instance().refreshPublishedResources({fixture.meshPath()},&error),"publish restored slots");
    fixture.component->materialPath.clear();fixture.subsystem->update(0);
    require(difference(full,capture(renderer,fixture.subsystem->renderScene(),output,"slots-restored.png"))==0,"restore original slots");
    if(!skinned){
        resource::Material overrideMaterial;require(overrideMaterial.load((fixture.directory/"surface.mat").generic_string()),"load authored override seed");
        overrideMaterial.setColor("baseColor",{.04f,.9f,.1f,1});require(fixture.save("override.mat",overrideMaterial),"save explicit whole-mesh override");
        fixture.component->materialPath=(fixture.directory/"override.mat").generic_string();fixture.subsystem->update(0);
        require(fixture.subsystem->renderScene().items().size()==1,"explicit static override uses one whole-mesh draw");
        const auto overridden=capture(renderer,fixture.subsystem->renderScene(),output,"explicit-override.png");
        render::RenderScene overrideReference;
        overrideReference.add(renderer.loadMesh(fixture.meshPath()),renderer.loadMaterial(fixture.component->materialPath));
        require(difference(overridden,capture(renderer,overrideReference,output,"explicit-override-reference.png"))==0,"whole-mesh override matches independent draw");
        require(difference(full,overridden)>1000,"explicit override changes both section colors");
        fixture.component->materialPath.clear();fixture.subsystem->update(0);
        require(difference(full,capture(renderer,fixture.subsystem->renderScene(),output,"override-restored.png"))==0,"independent override does not pollute cached slot materials");
    }
    const auto goodHandle=renderer.loadMesh(fixture.meshPath());
    const auto goodMetadata=renderer.meshSubmissionInfo(goodHandle);
    require(goodMetadata&&goodMetadata!=initialMetadata,"GPU success publishes a new immutable metadata version");
    require(initialMetadata->sections[0].indexCount==3,"retained initial snapshot remains unchanged");

    resource::Mesh invalid;require(invalid.load(fixture.meshPath()),"prepare CPU-decodable rejected GPU replacement");
    std::vector<uint32_t> badIndices(invalid.getIndexData(),invalid.getIndexData()+invalid.getIndexCount());
    badIndices[0]=UINT32_MAX;invalid._setForTestIndices(badIndices.data(),uint32_t(badIndices.size()));
    require(fixture.save("sections.msh",invalid),"save invalid GPU index fixture");
    resource::Mesh decoded;require(decoded.load(fixture.meshPath())&&decoded.getIndexData()[0]==UINT32_MAX,"codec accepts the distinct bridge-rejection fixture");
    require(resource::ResourceManager::instance().refreshPublishedResources({fixture.meshPath()},&error),"L2 publication completes before GPU bridge rejection");
    const auto latestCpu=resource::ResourceManager::instance().load<resource::IMesh>(fixture.meshPath());
    require(latestCpu&&latestCpu->getIndexData()[0]==UINT32_MAX,"L2 is the rejected newer content");
    require(renderer.meshSubmissionInfo(goodHandle)==goodMetadata,"rejected GPU update preserves matching metadata");
    fixture.subsystem->update(0);
    const auto retained=capture(renderer,fixture.subsystem->renderScene(),output,"rejected-retained.png");
    require(difference(full,retained)==0,"rejected update retains complete visible old GPU geometry");

    require(fixture.writeMesh(6,true),"repair source");
    require(resource::ResourceManager::instance().refreshPublishedResources({fixture.meshPath()},&error),"retry repaired replacement");
    fixture.subsystem->update(0);
    const auto repaired=capture(renderer,fixture.subsystem->renderScene(),output,"repaired.png");
    require(difference(full,repaired)==0,"repaired retry preserves intended full scene");
    auto destroyedMesh=renderer.loadMesh(fixture.meshPath());renderer.destroyMesh(destroyedMesh);
    auto destroyedMaterial=renderer.loadMaterial((fixture.directory/"surface-b.mat").generic_string());
    renderer.destroyMaterial(destroyedMaterial);fixture.subsystem->update(0);
    const auto recreated=capture(renderer,fixture.subsystem->renderScene(),output,"destroy-recreated.png");
    require(difference(full,recreated)==0,"explicit destroy is repaired by current path extraction");

    const auto previousContext=renderer.resourceContextId();renderer.shutdown();renderer=render::Renderer{};
    render::InitDesc init;init.backend=backend;init.windowHandle=window;init.width=320;init.height=240;init.msaa=0;init.vsync=false;
    require(renderer.initialize(init),"reinitialize real renderer context");
    require(renderer.resourceContextId()!=previousContext,"renderer resource context identity changes");
    // Force reuse of low material ids for a different path before extraction.
    require(renderer.loadMaterial((fixture.directory/"surface-b.mat").generic_string()).isValid(),"preload blue into the new material context");
    configure(fixture);fixture.subsystem->update(0);
    const auto restarted=capture(renderer,fixture.subsystem->renderScene(),output,"restarted.png");
    require(difference(full,restarted)==0,"real renderer restart retains section geometry and material colors");
    const auto expected=capture(renderer,reference(fixture,6,true),output,"restarted-reference.png");
    require(difference(restarted,expected)==0,"restarted output matches current-context reference");

    const auto textures=bgfx::getStats()->numTextures,vertices=bgfx::getStats()->numVertexBuffers;
    const auto indices=bgfx::getStats()->numIndexBuffers,programs=bgfx::getStats()->numPrograms;
    for(int iteration=0;iteration<30;++iteration){fixture.subsystem->update(0);frame(renderer,fixture.subsystem->renderScene());}
    require(bgfx::getStats()->numTextures==textures&&bgfx::getStats()->numVertexBuffers==vertices
        &&bgfx::getStats()->numIndexBuffers==indices&&bgfx::getStats()->numPrograms==programs,"steady extraction does not repeatedly upload meshes or compile programs");
    std::ofstream report(output/"acceptance.json");
    const auto* caps=bgfx::getCaps();
    report<<"{\"passed\":true,\"skinned\":"<<(skinned?"true":"false")
        <<",\"expandedChangedPixels\":"<<expandedDifference<<",\"initialContext\":"<<previousContext
        <<",\"restartedContext\":"<<renderer.resourceContextId()
        <<",\"backend\":\""<<(backend==render::Backend::Direct3D11?"d3d11":"d3d12")
        <<"\",\"vendor\":"<<caps->vendorId<<",\"device\":"<<caps->deviceId<<"}\n";
    std::cout<<"PASS "<<(skinned?"skinned":"static")<<" mesh metadata/ranges/slots/reload/rejection/retry/destroy/restart; expanded pixels="<<expandedDifference<<"\n";
}
}

int main(int argc,char** argv) {
    try {
        require(argc==3,"usage: AYEntity_MeshSubmissionRenderTest <fresh-output> d3d11|d3d12");
        const std::string name=argv[2];require(name=="d3d11"||name=="d3d12","explicit D3D backend required");
        const auto output=std::filesystem::u8path(argv[1]);require(!std::filesystem::exists(output),"output must not exist");
        require(std::filesystem::create_directory(output),"create fresh native output");
        entity::bootstrapModule();HiddenWindow window;require(window.handle!=nullptr,"create hidden device window");
        const auto backend=name=="d3d11"?render::Backend::Direct3D11:render::Backend::Direct3D12;
        runCase(output/"static",backend,window.handle,false);
        runCase(output/"skinned",backend,window.handle,true);
        return 0;
    } catch(const std::exception& failure){std::cerr<<"FAIL: "<<failure.what()<<"\n";return 1;}
}
#else
int main(){std::cerr<<"Windows real-device fixture required\n";return 2;}
#endif
