#include "RenderSubmissionFixture.h"
#include <AYEntity/EntityModule.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>

#ifndef AY_ENTITY_RENDER_BENCHMARK_LABEL
#define AY_ENTITY_RENDER_BENCHMARK_LABEL "current"
#endif

namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
unsigned bounded(const char* text,unsigned maximum){
    size_t used=0;const auto value=std::stoul(text,&used);
    require(used==std::strlen(text)&&value>0&&value<=maximum,"positive bounded benchmark argument");
    return static_cast<unsigned>(value);
}
}

int main(int argc,char** argv){
    try {
        require(argc==6,"usage: benchmark <new-json> static|skinned <entities<=8192> <paths<=128> <frames<=2000>");
        const auto output=std::filesystem::u8path(argv[1]);require(!std::filesystem::exists(output),"benchmark report must be new");
        const std::string mode=argv[2];require(mode=="static"||mode=="skinned","explicit submission mode");
        const auto entities=bounded(argv[3],8192),paths=bounded(argv[4],128),frames=bounded(argv[5],2000);
        require(paths<=entities,"distinct mesh paths must not exceed instances");
        entity::bootstrapModule();SubmissionFixture fixture(mode=="skinned");require(fixture.ready,"initialize benchmark extraction");
        std::vector<std::string> meshPaths{fixture.meshPath()};
        for(unsigned i=1;i<paths;++i){
            const auto path=fixture.directory/("instance-"+std::to_string(i)+".msh");
            require(std::filesystem::copy_file(fixture.meshPath(),path),"copy deterministic path-identity fixture");
            meshPaths.push_back(path.generic_string());
        }
        for(unsigned i=1;i<entities;++i){
            auto* object=fixture.world.createSpatialEntity();
            object->transform()->setPosition(float(i%64)*.01f,0,float(i/64)*.01f);
            auto* mesh=object->createComponent<entity::MeshComponent>();require(mesh!=nullptr,"create benchmark mesh component");
            mesh->meshPath=meshPaths[i%paths];mesh->skinned=fixture.isSkinned;
            if(fixture.isSkinned){
                auto* skeleton=object->createComponent<entity::SkeletonComponent>();require(skeleton!=nullptr,"create benchmark pose");
                skeleton->skinMatrices=new math::Float4x4[32];
                for(unsigned joint=0;joint<32;++joint)skeleton->skinMatrices[joint]=math::Float4x4::identity();
                skeleton->jointCount=32;skeleton->loaded=true;
            }
        }
        // All cold decode/upload/compiler work and the Noop command flush are
        // outside the measured extraction interval.
        for(unsigned i=0;i<32;++i)fixture.subsystem->update(0);
        fixture.subsystem->renderer().beginFrame();fixture.subsystem->renderer().render(render::RenderScene{});
        fixture.subsystem->renderer().endFrame();
        const auto expectedItems=size_t(entities)*2;
        require(fixture.subsystem->renderScene().items().size()==expectedItems,"benchmark emits all sections");
        std::set<uint64_t> uniqueMeshes;
        for(const auto& item:fixture.subsystem->renderScene().items())uniqueMeshes.insert(item.mesh.id);
        require(uniqueMeshes.size()==paths,"benchmark exercises requested distinct GPU path caches");
        std::vector<double> samples;samples.reserve(frames);
        for(unsigned i=0;i<frames;++i){
            const auto start=std::chrono::steady_clock::now();fixture.subsystem->update(0);
            const auto end=std::chrono::steady_clock::now();
            require(fixture.subsystem->renderScene().items().size()==expectedItems,"steady extraction remains complete");
            samples.push_back(std::chrono::duration<double,std::milli>(end-start).count());
        }
        std::sort(samples.begin(),samples.end());
        const auto median=samples[samples.size()/2],p95=samples[(samples.size()-1)*95/100];
        std::ofstream report(output);
        report<<"{\"label\":\""<<AY_ENTITY_RENDER_BENCHMARK_LABEL<<"\",\"mode\":\""<<mode
            <<"\",\"entities\":"<<entities<<",\"meshPaths\":"<<paths<<",\"drawItems\":"<<expectedItems
            <<",\"frames\":"<<frames<<",\"medianMs\":"<<median<<",\"p95Ms\":"<<p95
            <<",\"minimumMs\":"<<samples.front()<<",\"maximumMs\":"<<samples.back()
            <<",\"scope\":\"Debug Noop production extraction only; excludes cold upload and rendering\"}\n";
        require(report.good(),"write benchmark report");
        std::cout<<AY_ENTITY_RENDER_BENCHMARK_LABEL<<" "<<mode<<" "<<entities<<" entities / "<<paths
            <<" paths: median "<<median<<" ms, P95 "<<p95<<" ms\n";
        for(size_t i=1;i<meshPaths.size();++i){std::error_code ignored;std::filesystem::remove(std::filesystem::u8path(meshPaths[i]),ignored);}
        return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL benchmark: "<<failure.what()<<"\n";return 1;}
}
