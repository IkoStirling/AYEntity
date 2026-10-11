#include <AYEntity/DeterministicLevelResource2D.h>
#include <AYResource/ResourceBootstrap.h>
#include <AYResource/ResourceManager.h>
#include <AYResource/AssetPath.h>
#include <charconv>
#include <iostream>
#include <stdexcept>
#include <string_view>

using namespace ayt;
int main(int argc,char** argv) {
    try {
        std::string asset,root,database;unsigned ticks=8;
        bool seenTicks=false;
        for(int n=1;n<argc;++n) {
            const std::string_view flag(argv[n]);if(n+1==argc)throw std::invalid_argument("Missing option value");
            const std::string value(argv[++n]);
            if(flag=="--asset" && asset.empty())asset=value;
            else if(flag=="--root" && root.empty())root=value;
            else if(flag=="--database" && database.empty())database=value;
            else if(flag=="--ticks" && !seenTicks) {seenTicks=true;const auto parsed=std::from_chars(value.data(),value.data()+value.size(),ticks);
                if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || ticks>4096)throw std::invalid_argument("--ticks must be 0..4096");}
            else throw std::invalid_argument("Unknown/duplicate option");
        }
        if(asset.empty() || (root.empty() && database.empty()))throw std::invalid_argument(
            "Usage: AYEntity_DetLevelResourceExample --asset levels/room.dlc [--root LOOSE_ROOT] [--database resources.db] [--ticks 0..4096]");
        std::string error;
        if(!resource::initializeLoaders() || !entity::registerDetLevelResources2D(error))throw std::runtime_error(error.empty()?"Resource bootstrap failed":error);
        auto& resources=resource::ResourceManager::instance();resource::setAssetRoot(root);
        if(!database.empty() && !resources.openDatabase(database))throw std::runtime_error("Cannot mount resources.db");
        const auto path=resource::resolveAssetPath({},asset);
        const auto loaded=resources.load<entity::DetLevelResource2D>(path);
        if(!loaded || resources.hasLoadFailed(path) || !loaded->level())throw std::runtime_error("Validated level resource could not be loaded");
        const auto level=loaded->level(); // Application owns this immutable snapshot; Resource owns no Session.
        entity::DeterministicSession session(entity::detLevelSessionConfig(*level));
        const bool configured=entity::configureDetLevel2D(session,*level,error);
        if(!configured)throw std::runtime_error(error);if(!session.seal())throw std::runtime_error(session.error());
        // This tool-sized recipe submits canonical empty input. Gameplay input systems belong
        // in a standard Host recipe and use the same immutable Level getters/configure facade.
        for(unsigned n=0;n<ticks;++n)if(!session.advance({n,level->inputVersion,{}}))throw std::runtime_error(session.error());
        const auto cp=session.checkpoint();if(!cp)throw std::runtime_error(session.error());
        std::cout<<"{\"schema\":1,\"status\":\"passed\",\"cookedFormat\":1,\"levelFormat\":"<<level->formatVersion
            <<",\"nextTick\":"<<cp->nextTick<<",\"bodies\":"<<level->actors.size()<<",\"contentHash\":\""<<entity::detLevelSessionConfig(*level).contentHash
            <<"\",\"finalHash\":\""<<entity::detCheckpointHash(*cp)<<"\"}\n";
        resources.unloadAll();resources.setDatabase({});resource::setAssetRoot({});return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';auto& resources=resource::ResourceManager::instance();resources.unloadAll();resources.setDatabase({});resource::setAssetRoot({});return 1;
    }
}
