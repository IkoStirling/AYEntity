#include <AYTest.h>
#include "RenderSubmissionFixture.h"

TEST_SUITE(EntityRenderSubmissionTests)
TEST_CASE(spatial_region_reaches_static_and_skinned_draw_packets)
{
    for(bool skinned:{false,true}){
        SubmissionFixture fixture(skinned);CHECK(fixture.ready);if(!fixture.ready)return;
        auto entities=fixture.world.getAllEntities();CHECK(entities.size()==1);
        CHECK(fixture.world.connectedSpaces().addRegion(2));
        CHECK(entities.front()->setSpaceRegion(2));
        fixture.subsystem->update(0);
        const auto& items=fixture.subsystem->renderScene().items();
        CHECK(items.size()==2);
        for(const auto& item:items) CHECK(item.spaceRegionId==2);
    }
}
TEST_CASE(replaced_renderer_does_not_reuse_a_resident_numeric_handle_from_the_old_context)
{
    for(bool skinned:{false,true}){
        SubmissionFixture fixture(skinned);CHECK(fixture.ready);if(!fixture.ready)return;
        auto& renderer=fixture.subsystem->renderer();const auto original=fixture.subsystem->renderScene();
        const auto context=renderer.resourceContextId();renderer.shutdown();renderer=render::Renderer{};
        render::InitDesc init;init.backend=render::Backend::Noop;CHECK(renderer.initialize(init));
        CHECK(renderer.resourceContextId()!=context);
        resource::Mesh decoy;decoy.createSphere();CHECK(fixture.save("decoy.msh",decoy));
        const auto other=renderer.loadMesh((fixture.directory/"decoy.msh").generic_string());
        CHECK(other.id==original.items()[0].mesh.id);CHECK(renderer.meshSubmissionInfo(other));
        const auto otherMaterial=renderer.createMaterialFromFile((fixture.directory/"surface.phoskia").generic_string());
        CHECK(otherMaterial.isValid());CHECK(renderer.isMaterialResident(otherMaterial));
        const auto expected=renderer.loadMesh(fixture.meshPath());CHECK(expected.id!=other.id);
        fixture.subsystem->update(0);const auto& current=fixture.subsystem->renderScene().items();
        CHECK(current.size()==2);if(current.size()!=2)return;
        CHECK(current[0].mesh.id==expected.id);CHECK(current[1].mesh.id==expected.id);
        CHECK(current[0].indexCount==3);CHECK(current[1].firstIndex==3);
    }
}
TEST_CASE(failed_path_deduplication_retries_on_the_next_extraction)
{
    for(bool skinned:{false,true}){
    SubmissionFixture fixture(skinned);CHECK(fixture.ready);if(!fixture.ready)return;
    auto* second=fixture.world.createSpatialEntity();
    auto* mesh=second->createComponent<entity::MeshComponent>();CHECK(mesh);if(!mesh)return;
    mesh->meshPath=fixture.meshPath();mesh->skinned=skinned;
    CHECK(second->createComponent<entity::SkeletonComponent>());
    fixture.subsystem->update(0);CHECK(fixture.subsystem->renderScene().items().size()==4);
    auto handle=fixture.subsystem->renderer().loadMesh(fixture.meshPath());
    fixture.subsystem->renderer().destroyMesh(handle);
    resource::ResourceManager::instance().unloadResource(fixture.meshPath());
    CHECK(std::filesystem::remove(std::filesystem::u8path(fixture.meshPath())));
    fixture.subsystem->update(0);CHECK(fixture.subsystem->renderScene().items().empty());
    CHECK(fixture.writeMesh(3));fixture.subsystem->update(0);
    CHECK(fixture.subsystem->renderScene().items().size()==4);
    }
}
TEST_CASE(relative_asset_paths_follow_root_changes_without_reusing_another_root_submission)
{
    for(bool skinned:{false,true}){
        SubmissionFixture fixture(skinned);CHECK(fixture.ready);if(!fixture.ready)return;
        auto& renderer=fixture.subsystem->renderer();
        resource::setAssetRoot(fixture.directory.generic_string());
        fixture.component->meshPath="sections.msh";fixture.subsystem->update(0);
        const auto original=fixture.subsystem->renderScene();CHECK(original.items().size()==2);
        if(original.items().size()!=2)return;
        const auto alternate=fixture.directory/"alternate";CHECK(std::filesystem::create_directory(alternate));
        for(const char* name:{"surface.mat","surface.phoskia"})
            CHECK(std::filesystem::copy_file(fixture.directory/name,alternate/name));
        resource::Mesh replacement;CHECK(replacement.load(fixture.meshPath()));
        const uint32_t indices[]={0,1,2,0,2,3,4,5,6,4,6,7};replacement._setForTestIndices(indices,12);
        const resource::IMesh::Submesh sections[]={{0,6,0,0},{6,6,1,0}};replacement._setForTestSubmeshes(sections,2);
        std::vector<uint8_t> bytes;CHECK(replacement.saveToBinary(bytes));
        CHECK(SubmissionFixture::write(alternate/"sections.msh",bytes.data(),bytes.size()));
        resource::setAssetRoot(alternate.generic_string());fixture.subsystem->update(0);
        const auto changed=fixture.subsystem->renderScene();CHECK(changed.items().size()==2);
        if(changed.items().size()==2){
            CHECK(changed.items()[0].mesh.id!=original.items()[0].mesh.id);
            CHECK(changed.items()[0].indexCount==6);CHECK(changed.items()[1].firstIndex==6);
            CHECK(changed.items()[0].material.id==renderer.loadMaterial((alternate/"surface.mat").generic_string()).id);
        }
        resource::setAssetRoot(fixture.directory.generic_string());fixture.subsystem->update(0);
        const auto& restored=fixture.subsystem->renderScene().items();CHECK(restored.size()==2);
        if(restored.size()==2){CHECK(restored[0].mesh.id==original.items()[0].mesh.id);CHECK(restored[0].indexCount==3);}
        CHECK(original.items()[1].firstIndex==3);
        for(const char* name:{"sections.msh","surface.mat","surface.phoskia"})CHECK(std::filesystem::remove(alternate/name));
        CHECK(std::filesystem::remove(alternate));
    }
}
TEST_CASE(reordered_slots_keep_asset_defaults_and_explicit_static_override_remains_whole_mesh)
{
    SubmissionFixture fixture(false,render::Backend::Noop,nullptr,true);CHECK(fixture.ready);if(!fixture.ready)return;
    auto& renderer=fixture.subsystem->renderer();const auto original=fixture.subsystem->renderScene();
    CHECK(original.items()[0].material.id!=original.items()[1].material.id);
    CHECK(fixture.writeMesh(3,false,true));std::string error;
    CHECK(resource::ResourceManager::instance().refreshPublishedResources({fixture.meshPath()},&error));
    fixture.component->materialPath=(fixture.directory/"surface.mat").generic_string();fixture.subsystem->update(0);
    const auto reordered=fixture.subsystem->renderScene();CHECK(reordered.items().size()==2);
    if(reordered.items().size()!=2)return;
    CHECK(reordered.items()[0].material.id==original.items()[1].material.id);
    CHECK(reordered.items()[1].material.id==original.items()[0].material.id);
    resource::Material overrideMaterial;CHECK(overrideMaterial.load(fixture.component->materialPath));
    CHECK(fixture.save("override.mat",overrideMaterial));
    fixture.component->materialPath=(fixture.directory/"override.mat").generic_string();fixture.subsystem->update(0);
    const auto& overridden=fixture.subsystem->renderScene().items();CHECK(overridden.size()==1);
    if(overridden.size()==1){CHECK(overridden[0].indexCount==0);
        CHECK(overridden[0].material.id==renderer.loadMaterial(fixture.component->materialPath).id);}
    fixture.component->materialPath.clear();fixture.subsystem->update(0);
    CHECK(fixture.subsystem->renderScene().items().size()==2);
    CHECK(original.items()[0].material.id!=original.items()[1].material.id);
}
TEST_CASE(reloaded_submesh_ranges_follow_current_gpu_content_and_old_packets_stay_owned)
{
    SubmissionFixture fixture;CHECK(fixture.ready);if(!fixture.ready)return;
    const auto original=fixture.subsystem->renderScene();
    CHECK(original.items()[0].indexCount==3);
    CHECK(original.items()[1].firstIndex==3);
    CHECK(fixture.writeMesh(6));
    std::string error;
    CHECK(resource::ResourceManager::instance().refreshPublishedResources({fixture.meshPath()},&error));
    fixture.subsystem->update(0);
    const auto& current=fixture.subsystem->renderScene().items();
    CHECK(current.size()==2);if(current.size()!=2)return;
    CHECK(current[0].mesh.id==original.items()[0].mesh.id);
    CHECK(current[0].indexCount==6);
    CHECK(current[1].firstIndex==6);
    CHECK(current[1].indexCount==6);
    CHECK(original.items()[0].indexCount==3);
    CHECK(original.items()[1].firstIndex==3);
}
TEST_CASE(explicitly_destroyed_mesh_is_resolved_again_on_the_next_extraction)
{
    SubmissionFixture fixture;CHECK(fixture.ready);if(!fixture.ready)return;
    auto& renderer=fixture.subsystem->renderer();
    const auto original=fixture.subsystem->renderScene();
    auto oldMesh=original.items()[0].mesh;
    renderer.destroyMesh(oldMesh);CHECK_FALSE(oldMesh.isValid());
    const auto replacement=renderer.loadMesh(fixture.meshPath());CHECK(replacement.isValid());
    CHECK(replacement.id!=original.items()[0].mesh.id);
    fixture.subsystem->update(0);
    const auto& current=fixture.subsystem->renderScene().items();
    CHECK(current.size()==2);if(current.size()!=2)return;
    CHECK(current[0].mesh.id==replacement.id);
    CHECK(current[1].mesh.id==replacement.id);
}
TEST_CASE(renderer_reinitialization_resolves_mesh_and_material_in_the_current_context)
{
    SubmissionFixture fixture;CHECK(fixture.ready);if(!fixture.ready)return;
    auto& renderer=fixture.subsystem->renderer();
    renderer.shutdown();
    render::InitDesc init;init.backend=render::Backend::Noop;init.width=64;init.height=64;
    const bool initialized=renderer.initialize(init);CHECK(initialized);if(!initialized)return;
    const auto mesh=renderer.loadMesh(fixture.meshPath());
    const auto material=renderer.loadMaterial((fixture.directory/"surface.mat").generic_string());
    CHECK(mesh.isValid());CHECK(material.isValid());
    fixture.subsystem->update(0);
    const auto& current=fixture.subsystem->renderScene().items();
    CHECK(current.size()==2);if(current.size()!=2)return;
    for(const auto& item:current){CHECK(item.mesh.id==mesh.id);CHECK(item.material.id==material.id);}
}
TEST_CASE(skinned_ranges_and_palette_follow_uploaded_content_while_old_packet_keeps_its_remap)
{
    SubmissionFixture fixture(true);CHECK(fixture.ready);if(!fixture.ready)return;
    const auto original=fixture.subsystem->renderScene();
    CHECK(original.items()[0].jointCount==1);CHECK(original.items()[1].jointCount==1);
    CHECK(original.items()[0].boneRemap&&original.items()[0].boneRemap[0]==2);
    CHECK(original.items()[1].boneRemap&&original.items()[1].boneRemap[0]==5);
    CHECK(fixture.writeMesh(6,true));std::string error;
    CHECK(resource::ResourceManager::instance().refreshPublishedResources({fixture.meshPath()},&error));
    fixture.subsystem->update(0);
    const auto& current=fixture.subsystem->renderScene().items();
    CHECK(current.size()==2);if(current.size()!=2)return;
    CHECK(current[0].indexCount==6);CHECK(current[1].firstIndex==6);CHECK(current[1].indexCount==6);
    CHECK(current[0].boneRemap&&current[0].boneRemap[0]==9);
    CHECK(current[1].boneRemap&&current[1].boneRemap[0]==12);
    CHECK(original.items()[0].indexCount==3);
    CHECK(original.items()[0].boneRemap&&original.items()[0].boneRemap[0]==2);
    CHECK(original.items()[1].boneRemap&&original.items()[1].boneRemap[0]==5);
}
TEST_CASE(skinned_renderer_restart_resolves_current_mesh_slot_materials_and_fallback_context)
{
    SubmissionFixture fixture(true);CHECK(fixture.ready);if(!fixture.ready)return;
    auto& renderer=fixture.subsystem->renderer();renderer.shutdown();
    render::InitDesc init;init.backend=render::Backend::Noop;init.width=64;init.height=64;
    const bool initialized=renderer.initialize(init);CHECK(initialized);if(!initialized)return;
    const auto mesh=renderer.loadMesh(fixture.meshPath());
    const auto material=renderer.loadMaterial((fixture.directory/"surface.mat").generic_string());
    CHECK(mesh.isValid());CHECK(material.isValid());fixture.subsystem->update(0);
    const auto& current=fixture.subsystem->renderScene().items();
    CHECK(current.size()==2);if(current.size()!=2)return;
    for(const auto& item:current){CHECK(item.mesh.id==mesh.id);CHECK(item.material.id==material.id);}
}
TEST_SUITE_END
