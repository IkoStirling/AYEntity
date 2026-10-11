#include <AYEntity/DeterministicLevelResource2D.h>
#include <AYEntity/DeterministicLevelCooked2D.h>
#include <AYResource/Converter/DeterministicLevelConverter2D.h>
#include <AYResource/ProjectBuild.h>
#include <AYResource/ImportJob.h>
#include <AYResource/CookShip.h>
#include <AYResource/ResourceBootstrap.h>
#include <AYResource/ResourceRegistry.h>
#include <AYResource/ResourceManager.h>
#include <AYResource/AssetPath.h>
#include <AYAssetFormat/AssetFormat.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace fs=std::filesystem;
using namespace ayt;
using namespace ayt::entity;
namespace {
using D=math::DetFloat32;using Bytes=std::vector<std::uint8_t>;
unsigned fullComparisons=0,negativeChecks=0;
void require(bool value,const std::string& message) {if(!value)throw std::runtime_error(message);}
fs::path nativeFilesystemPath(const fs::path& path) {
#if defined(_WIN32)
    const auto native=fs::absolute(path).lexically_normal().make_preferred().native();
    if(native.rfind(L"\\\\?\\",0)==0)return fs::path(native);
    if(native.rfind(L"\\\\",0)==0)return fs::path(L"\\\\?\\UNC\\"+native.substr(2));
    return fs::path(L"\\\\?\\"+native);
#else
    return path;
#endif
}
void write(const fs::path& path,const Bytes& bytes) {
    const auto physical=nativeFilesystemPath(path);
    fs::create_directories(physical.parent_path());std::ofstream stream(physical,std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    stream.close();require(bool(stream),"Cannot write resource evidence");
}
void write(const fs::path& path,const std::string& text) {write(path,Bytes(text.begin(),text.end()));}
Bytes read(const fs::path& path) {
    const auto physical=nativeFilesystemPath(path);
    const auto size=fs::file_size(physical);require(size<=4*1024*1024,"Evidence file exceeds bounded test read");
    Bytes bytes(static_cast<std::size_t>(size));std::ifstream stream(physical,std::ios::binary);
    stream.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(size));
    require(stream.gcount()==static_cast<std::streamsize>(size),"Cannot read resource evidence");return bytes;
}
fs::path fresh(const fs::path& parent) {
    require(fs::is_directory(parent),"--output must be an existing parent");
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned n=0;n<1024;++n) {const auto path=parent/("level-resource2d-"+std::to_string(stamp)+"-"+std::to_string(n));
        if(fs::create_directory(path))return path;}
    throw std::runtime_error("Cannot create a fresh evidence directory");
}
DetLevel2D scene() {
    DetLevel2D level;level.stepDenominator=4;
    level.character.gravity=D::fromInt(4);level.character.acceleration=D::fromInt(32);level.character.deceleration=D::fromInt(32);
    const auto half=D::fromInt(1)/D::fromInt(2);
    DetLevelActor2D hero;hero.id=10;hero.position={{},half};hero.character=true;
    hero.body={DetBodyMode2D::Kinematic,{half,half},{},{},1,6,false};
    DetLevelActor2D floor;floor.id=20;floor.position={{},D::fromInt(-1)};
    floor.body={DetBodyMode2D::Static,{D::fromInt(8),D::fromInt(1)},{},{},2,1,false};
    DetLevelActor2D trigger;trigger.id=30;trigger.position={D::fromInt(1),half};
    trigger.body={DetBodyMode2D::Static,{half/D::fromInt(2),half},{},{},4,1,true};
    level.actors={hero,floor,trigger};return level;
}
DetSessionCheckpoint cp(DeterministicSession& session) {
    auto result=session.checkpoint();require(bool(result),session.error());return *result;
}
void configure(DeterministicSession& session,const DetLevel2D& level) {
    const bool registered=session.registerSystem(10,level.character.collision.priority,[c=level.character](DetTickContext& context){
        if(context.input().commands.size()!=1)return false;
        const auto& command=context.input().commands[0];
        if(command.source!=1 || command.sequence!=0 || command.type!=0x45001 || command.payload.size()!=4)return false;
        std::uint32_t bits=0;for(unsigned n=0;n<4;++n)bits|=std::uint32_t{command.payload[n]}<<(n*8);
        detCharacterInput2D(context,10,c,{D::fromBits(bits),false,false,false});return true;
    });require(registered,session.error());
    std::string error;const bool configured=configureDetLevel2D(session,level,error);require(configured,error);
    const bool sealed=session.seal();require(sealed,session.error());
}
DetTickInput input(std::uint64_t tick,const DetLevel2D& level) {
    const auto bits=D::fromInt(1).bits();Bytes payload;
    for(unsigned n=0;n<4;++n)payload.push_back(static_cast<std::uint8_t>(bits>>(8*n)));
    return {tick,level.inputVersion,{{1,0,0x45001,std::move(payload)}}};
}
std::vector<Bytes> simulate(const DetLevel2D& level,const std::vector<Bytes>* expected=nullptr,std::uint64_t* finalHash=nullptr) {
    DeterministicSession session(detLevelSessionConfig(level));configure(session,level);
    std::vector<Bytes> states{encodeDetCheckpoint(cp(session))};
    if(expected) {require(states.back()==expected->at(0),"Resource initial manifest/full CP differ");++fullComparisons;}
    for(unsigned n=1;n<=8;++n) {
        const bool advanced=session.advance(input(n-1,level));require(advanced,session.error());const auto checkpoint=cp(session);
        require(checkpoint.actors.at(10).pose.position[0]==(D::fromInt(n)/D::fromInt(4)).bits()
            && checkpoint.actors.at(10).pose.position[1]==(D::fromInt(1)/D::fromInt(2)).bits(),"Independent loaded dyadic motion");
        states.push_back(encodeDetCheckpoint(checkpoint));
        if(expected) {require(states.back()==expected->at(n),"Resource complete CP differs at row "+std::to_string(n));++fullComparisons;}
    }
    if(finalHash)*finalHash=detCheckpointHash(cp(session));return states;
}
class ForeignLoader final : public resource::IResourceLoader {
public:
    bool canLoad(const std::string&) const override {return false;}
    const char* getResourceType() const override {return DetLevelResource2D::TYPE;}
    std::shared_ptr<resource::IResource> load(const std::string&) override {return {};}
    std::shared_ptr<resource::IResource> loadFromBinary(const void*,std::size_t) override {return {};}
    std::shared_ptr<resource::IResource> loadAsync(const std::string&,
        std::function<void(std::shared_ptr<resource::IResource>)> callback={}) override {(void)callback;return {};}
};
void registrationChecks() {
    std::string error;require(resource::initializeLoaders(),"Initialize real built-in resource registry");
    require(registerDetLevelResources2D(error),error);require(registerDetLevelResources2D(error),error);
    resource::ResourceRegistry::registerExtension(".dlc","OtherLevelType");
    require(!registerDetLevelResources2D(error),"Foreign suffix ownership must be rejected");++negativeChecks;
    resource::ResourceRegistry::registerExtension(".dlc",DetLevelResource2D::TYPE);
    resource::ResourceRegistry::unregisterLoader(DetLevelResource2D::TYPE);
    require(resource::ResourceRegistry::registerLoader(DetLevelResource2D::TYPE,+[]()->std::unique_ptr<resource::IResourceLoader>{return std::make_unique<ForeignLoader>();}),"Install isolated foreign loader fixture");
    require(!registerDetLevelResources2D(error),"Foreign loader ownership must be rejected");++negativeChecks;
    resource::ResourceRegistry::unregisterLoader(DetLevelResource2D::TYPE);require(registerDetLevelResources2D(error),error);
    DetLevelLoader2D loader;require(!loader.canLoad("author.dlt"),"Authoring file is not a runtime resource");++negativeChecks;
}
void run(const fs::path& directory) {
    registrationChecks();const auto original=scene();const auto canonical=encodeDetLevel2D(original);
    const auto cooked=encodeDetLevelCooked2D(original);std::uint64_t referenceHash=0;const auto expected=simulate(original,nullptr,&referenceHash);
    write(directory/"reference-initial.state",expected.front());write(directory/"reference-final.state",expected.back());
    write(directory/"authoring-source.dlt","\r\n\t"+canonical+" \n");
    write(directory/"project/Assets/levels/sub/room.dlt","\r\n\t"+canonical+" \n");
    const auto project=directory/"project";
    resource::ImportOptions direct;direct.sourcePath=(project/"Assets/levels/sub/room.dlt").string();
    direct.sourceAssetRoot=(project/"Assets").string();direct.outputDir=(directory/"direct-import").string();direct.requireCharacterAssets=false;
    auto imported=resource::importAsset(direct);require(imported.ok && !imported.usedCache && imported.conversion.resources.size()==1,"Actual single-file import");
    require(imported.conversion.resources[0].path=="levels/sub/room.dlc"
        && read(directory/"direct-import/levels/sub/room.dlc")==cooked,"Import preserves hierarchy and canonical source identity");
    auto brokenImport=cooked;brokenImport.back()^=1;write(directory/"direct-import/levels/sub/room.dlc",brokenImport);
    imported=resource::importAsset(direct);require(imported.ok && !imported.usedCache
        && read(directory/"direct-import/levels/sub/room.dlc")==cooked,"Single-file import skips timestamp sidecars and revalidates the source");
    resource::ProjectBuildProfile profile;profile.schemaVersion=1;profile.id="deterministic-level-test";
    profile.platform="windows";profile.architecture="x64";profile.configuration="Development";
    profile.content.rules={{"levels/**/*.dlt",resource::ProjectAssetTransform::Cook,resource::ProjectAssetStorage::Pak,"levels",true}};
    profile.package.output="out/package";
#if defined(_WIN32)
    // Preserve the caller's complete deep output tree, and make this a real
    // long-cache-path regression even when the caller supplies a short root.
    const auto rootCharacters=(project/profile.cache.root).native().size();
    if(rootCharacters<180)profile.cache.root+="/"+std::string(199-rootCharacters,'p');
#endif
    std::string error;
    require(profile.save((project/"levels.bld").string(),&error),error);
    const auto plan=resource::ProjectBuildPlanner::create(profile,project.string());require(plan.valid(),"Valid actual level project build plan");
    const auto key=plan.assets.at(0).cacheKey;
    const auto cacheRoot=project/profile.cache.root/"objects"/key.substr(0,2)/key;
    const auto cacheFile=cacheRoot/"files/levels/sub/room.dlc";
#if defined(_WIN32)
    require(cacheFile.native().size()>260,"Windows fixture must retain a physical cache path beyond MAX_PATH");
#endif
    auto built=resource::ProjectBuildExecutor::execute(plan);require(built.ok,"Real ProjectBuild cook/Pak: "+built.error);
    require(built.cookedCount==1 && built.cacheHitCount==0,"First source cook reaches actual converter");
    require(read(cacheFile)==cooked,"Deep cache publication retains exact canonical cooked bytes");
    resource::ProjectBuildExecutionOptions forced;forced.forceCook=true;
    built=resource::ProjectBuildExecutor::execute(plan,forced);
    require(built.ok && built.cookedCount==1 && built.cacheHitCount==0,
        "Deep forced cook replaces the existing real cache object: "+built.error);
    require(read(cacheFile)==cooked,"Deep cache replacement preserves complete source identity");
    const auto temporary=project/profile.cache.root/"staging"/(key+".tmp");
    require(!fs::exists(nativeFilesystemPath(temporary)),"Published deep cache leaves no unrenamed staging object");
    built=resource::ProjectBuildExecutor::execute(plan);require(built.ok && built.cacheHitCount==1,"Content-addressed ProjectBuild cache hit");
    const auto content=project/"out/package/Content";
    const auto publishedDatabase=read(content/"resources.db");
    const auto cacheMetadata=read(cacheRoot/"conversion.json");
    auto otherNamespace=resource::ConversionResult::fromJson(std::string(cacheMetadata.begin(),cacheMetadata.end()));
    otherNamespace.resources.at(0).path="levels/sub/room.msh";otherNamespace.resources.at(0).type="Mesh";
    write(cacheRoot/"files/levels/sub/room.msh",cooked);write(cacheRoot/"conversion.json",otherNamespace.toJson());
    built=resource::ProjectBuildExecutor::execute(plan);
    require(!built.ok && built.error.find("cached conversion identity")!=std::string::npos
        && read(content/"resources.db")==publishedDatabase,"Cached conversion cannot redirect a level into another resource namespace");++negativeChecks;
    write(cacheRoot/"conversion.json",cacheMetadata);
    auto otherContent=original;otherContent.applicationVersion=2;write(cacheFile,encodeDetLevelCooked2D(otherContent));
    built=resource::ProjectBuildExecutor::execute(plan);
    require(!built.ok && built.error.find("cache/source identity mismatch")!=std::string::npos
        && read(content/"resources.db")==publishedDatabase,"A valid but wrong cooked cache cannot replace the exact source identity");++negativeChecks;
    write(cacheFile,cooked);
    write(project/"Assets/levels/sub/room.dlt",canonical+" \n\n");
    built=resource::ProjectBuildExecutor::execute(plan);
    require(!built.ok && built.error.find("changed after planning")!=std::string::npos,"Changed source cannot publish using a stale plan");++negativeChecks;
    require(read(content/"resources.db")==publishedDatabase,"Stale-plan rejection preserves previous database");
    auto raw=profile;raw.content.rules[0].transform=resource::ProjectAssetTransform::Raw;
    require(!resource::ProjectBuildPlanner::create(raw,project.string()).valid(),"Authoring Raw publication is rejected");++negativeChecks;
    resource::DeterministicLevelConverter2D outside((directory/"outside.dlt").string());
    write(directory/"outside.dlt",canonical);outside.setAssetRoot((project/"Assets").string());outside.setOutputDir((directory/"outside-output").string());
    bool refused=false;try {(void)outside.convert();}catch(const std::exception&) {refused=true;}
    require(refused && !fs::exists(directory/"outside-output"),"Converter rejects out-of-root source before output writes");++negativeChecks;

    write(directory/"loose/levels/sub/room.dlc",cooked);
    auto& manager=resource::ResourceManager::instance();manager.unloadAll();manager.setDatabase({});
    resource::setAssetRoot((directory/"loose").string());const auto loosePath=resource::resolveAssetPath({},"levels/sub/room.dlc");
    auto loose=manager.load<DetLevelResource2D>(loosePath);
    require(loose && !manager.hasLoadFailed(loosePath) && loose->level(),"Real ResourceManager loose cooked load");
    require(encodeDetLevel2D(*loose->level())==canonical && loose->sizeInBytes()==cooked.size(),"Loose canonical snapshot and exact cooked size");
    (void)simulate(*loose->level(),&expected);const auto retained=loose->level();
    auto damaged=cooked;damaged.back()^=1;write(directory/"bad.dlc",damaged);
    require(!loose->reload((directory/"bad.dlc").string()) && loose->level()==retained && loose->isLoaded(),"Failed reload retains the exact prior snapshot");++negativeChecks;
    require(!loose->loadFromBinary(nullptr,0) && loose->level()==retained,"Invalid memory preserves previous snapshot");++negativeChecks;
    auto wrongVersion=cooked;wrongVersion[8]=2;
    require(!loose->loadFromBinary(wrongVersion.data(),wrongVersion.size()) && loose->level()==retained,"Unknown cooked version is denied atomically");++negativeChecks;
    Bytes excessive(kDetLevelCooked2DMaxBytes+1);
    require(!loose->loadFromBinary(excessive.data(),excessive.size()) && loose->level()==retained,"Oversized cooked bytes are denied before decode");++negativeChecks;
    const auto bad=manager.load<DetLevelResource2D>((directory/"bad.dlc").string());
    require(!bad && manager.hasLoadFailed((directory/"bad.dlc").string()),"Real ResourceManager marks damaged cooked input Failed");++negativeChecks;
    require(loose->unload() && !loose->level() && encodeDetLevel2D(*retained)==canonical,"Retained immutable level survives resource unload");
    manager.unloadAll();manager.setDatabase({});resource::setAssetRoot({});

    // Remove every authoring/loose source available to these manager keys. These are
    // individual test-owned files; no recursive deletion or unrelated directory scan.
    require(fs::remove(project/"Assets/levels/sub/room.dlt"),"Remove authoring file before Pak-only proof");
    require(fs::remove(directory/"loose/levels/sub/room.dlc"),"Remove loose cooked file before Pak-only proof");
    require(!fs::exists(content/"levels/sub/room.dlc"),"Project Pak does not have a loose replacement at its logical path");
    require(manager.openDatabase((content/"resources.db").string()),"Mount actual project output database");
    resource::setAssetRoot(content.string());const auto pakPath=resource::resolveAssetPath({},"levels/sub/room.dlc");
    require(manager.hasDatabaseResource(pakPath),"Actual logical level has a database record");
    require(!fs::exists(fs::path(pakPath)),"No loose file exists at the ResourceManager fallback key");
    auto packed=manager.load<DetLevelResource2D>(pakPath);
    require(packed && !manager.hasLoadFailed(pakPath) && packed->level(),"Actual ProjectBuild Pak-only ResourceManager load");
    require(encodeDetLevel2D(*packed->level())==canonical,"Pak snapshot retains complete canonical identity");
    const auto packedStates=simulate(*packed->level(),&expected);write(directory/"pak-final.state",packedStates.back());
    manager.unloadAll();manager.setDatabase({});resource::setAssetRoot({});

    write(directory/"ship-input/levels/sub/room.dlc",cooked);
    resource::CookShipOptions ship;ship.assetsRoot=(directory/"ship-input").string();ship.outputDir=(directory/"ship").string();
    auto shipped=resource::cookShipPackage(ship);require(shipped.ok && shipped.fileCount==1,"Real CookShip validated final level: "+shipped.error);
    const auto oldPak=read(shipped.pakPath),oldDb=read(shipped.dbPath);
    write(directory/"ship-input/levels/sub/room.dlc",damaged);shipped=resource::cookShipPackage(ship);
    require(!shipped.ok && read(directory/"ship/content.pak")==oldPak && read(directory/"ship/resources.db")==oldDb,
        "Damaged level is rejected before replacing either old ship artifact");++negativeChecks;
    require(fs::remove(directory/"ship-input/levels/sub/room.dlc"),"Remove CookShip loose input before Pak-only load");
    require(manager.openDatabase((directory/"ship/resources.db").string()),"Mount actual CookShip output database");
    resource::setAssetRoot((directory/"ship").string());const auto shipPath=resource::resolveAssetPath({},"levels/sub/room.dlc");
    require(manager.hasDatabaseResource(shipPath) && !fs::exists(fs::path(shipPath)),"CookShip record exists with no loose fallback key");
    auto shipLevel=manager.load<DetLevelResource2D>(shipPath);
    require(shipLevel && !manager.hasLoadFailed(shipPath) && shipLevel->level(),"Actual CookShip Pak-only ResourceManager load");
    (void)simulate(*shipLevel->level(),&expected);manager.unloadAll();manager.setDatabase({});resource::setAssetRoot({});
    require(fullComparisons==27,"Three resource execution lanes each compare all nine actual checkpoint rows");
    write(directory/"summary.json","{\"schema\":1,\"status\":\"passed\",\"format\":1,\"authoringFormat\":1,\"ticksPerLane\":8,\"resourceLanes\":3,\"fullCheckpointComparisons\":"
        +std::to_string(fullComparisons)+",\"negativeChecks\":"+std::to_string(negativeChecks)+",\"cachePathCharacters\":"+std::to_string(cacheFile.native().size())+",\"cacheObjectReplaced\":true,\"projectCacheHit\":true,\"projectPakOnly\":true,\"cookShipPakOnly\":true,\"oldPackagePreserved\":true,\"contentHash\":\""
        +std::to_string(detLevelSessionConfig(original).contentHash)+"\",\"finalHash\":\""+std::to_string(referenceHash)+"\"}\n");
}
} // namespace
int main(int argc,char** argv) {
    fs::path directory;try {
        if(argc!=3 || std::string_view(argv[1])!="--output")throw std::invalid_argument("Usage: AYEntity_LevelResource2DChecks --output EXISTING_PARENT");
        directory=fresh(argv[2]);run(directory);std::cout<<"Deterministic level actual loose/ProjectBuild-Pak/CookShip-Pak passed; checkpoints="<<fullComparisons<<" negatives="<<negativeChecks<<'\n';return 0;
    } catch(const std::exception& error) {
        std::cerr<<"Level Resource2D failed: "<<error.what()<<"; evidence="<<directory.string()<<'\n';
        auto& manager=resource::ResourceManager::instance();manager.unloadAll();manager.setDatabase({});resource::setAssetRoot({});return 1;
    }
}
