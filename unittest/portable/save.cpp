// design reference: AYEntity/design.md Stage38; short persistence/restore closure.
#include <AYEntity/DeterministicSave.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace {
using namespace ayt::entity;
namespace fs=std::filesystem;
using D=ayt::math::DetFloat32;
using V=ayt::math::DetVec3;
constexpr DetSessionConfig config{38,1,1,4,0x4453563338000001ULL};
void require(bool ok,const std::string& text){if(!ok)throw std::runtime_error(text);}
std::vector<std::uint8_t> bytes(const DeterministicSession& session){
    const auto cp=session.checkpoint();require(cp.has_value(),session.error());return encodeDetCheckpoint(*cp);
}
bool configure(DeterministicSession& session,std::uint64_t logicHash=0x4453563338503031ULL){
    if(!session.registerTypedSchema({2,1,{{1,std::uint64_t{0}},{2,D::fromBits(0x80000000U)},
        {3,DetEntityRef{3}},{4,std::uint64_t{0}},{5,ayt::math::DetVec2{D::fromBits(1),D{}}}}})
        || !session.registerTypedSchema({3,1,{{1,std::uint64_t{0}},{2,std::uint64_t{0}}}})
        || !session.registerGlobalState(3) || !session.registerRandomStream(7,42)
        || !session.registerLogicProfile(10,1,logicHash))return false;
    if(!session.registerValidator(10,1,[](const DetSessionCheckpoint& cp,std::string& error){
        const auto found=cp.globals.find(3);
        if(found==cp.globals.end() || found->second.size()!=2 || found->second.front()!=cp.nextTick){
            error="Save fixture counter must match nextTick";return false;}return true;
    }))return false;
    if(!session.registerSystem(10,0,[](DetTickContext& context){
        const auto count=context.readGlobal<std::uint64_t>(3,1);context.writeGlobal(3,1,count+1);
        auto digest=context.readGlobal<std::uint64_t>(3,2);
        for(const auto& event:context.events())for(auto byte:event.payload)digest=digest*33+byte;
        for(const auto& command:context.input().commands){
            if(command.type==99)return false;
            for(auto byte:command.payload)digest=digest*33+byte;
        }
        context.writeGlobal(3,2,digest);
        for(auto id:context.entities()){
            context.write(id,2,1,context.read<std::uint64_t>(id,2,1)+1);
            context.write(id,2,4,context.read<std::uint64_t>(id,2,4)+ayt::math::random_uint(context.random(7),100));
            const auto speed=D::fromInt(context.tick()%2 ? -1 : 1);
            if(!context.pose(id).translate(V{speed*context.dt(),{}, {}}))return false;
        }
        if(context.tick()==2)context.spawn(3);
        if(context.tick()==5)context.despawn(3);
        const std::array<std::uint8_t,2> payload{static_cast<std::uint8_t>(context.tick()),0xa5};
        context.emit(7,payload);return true;
    }))return false;
    DetActorState a,b;DetSimTransformComponent pose;
    if(!pose.setRotation({}))return false;a.pose=pose.snapshot();
    if(!pose.setPosition(V::fromInts(0,2,0)))return false;b.pose=pose.snapshot();
    return session.addEntity(1,a) && session.addEntity(2,b) && session.seal();
}
DetTickInput input(std::uint64_t tick){return {tick,1,{{1,0,1,{static_cast<std::uint8_t>(tick),0xff}}}};}
void advance(DeterministicSession& session,unsigned end){
    while(session.nextTick()<end)require(session.advance(input(session.nextTick())),session.error());
}
std::vector<std::uint8_t> raw(const fs::path& file){
    std::ifstream stream(file,std::ios::binary);require(bool(stream),"fixture raw open");
    return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}
void rawWrite(const fs::path& file,std::span<const std::uint8_t> data){
    std::ofstream stream(file,std::ios::binary|std::ios::out|std::ios::noreplace);
    require(bool(stream),"fixture raw fresh open");stream.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));
    stream.close();require(!stream.fail(),"fixture raw close");
}
void put32(std::vector<std::uint8_t>& data,std::size_t offset,std::uint32_t value){
    for(unsigned i=0;i<4;++i)data.at(offset+i)=static_cast<std::uint8_t>(value>>(8*i));
}
struct Evidence {std::uint64_t hash=0;std::size_t fileBytes=0;unsigned malformed=0,linkSkips=0;};
Evidence checks(const fs::path& directory){
    std::string error;DeterministicSession baseline(config);require(configure(baseline),baseline.error());advance(baseline,6);
    const auto saved=*baseline.checkpoint();const auto save=directory/"boundary6.dsv";
    require(saved.nextTick==6 && saved.actors.size()==2 && saved.retiredIds==std::vector<SimEntityId>{3}
        && saved.randomStreams.contains(7) && saved.pendingEvents.size()==1,"fixture exercises complete boundary channels");
    require(saveDetSession(save,baseline,error),error);const auto file=raw(save),canonical=encodeDetCheckpoint(saved);
    require(file.size()==canonical.size()+kDetSaveHeaderBytes
        && std::equal(canonical.begin(),canonical.end(),file.begin()+kDetSaveHeaderBytes),"envelope carries existing checkpoint bytes unchanged");
    require(file[0]=='A' && file[7]=='1' && file[8]==1 && file[9]==0 && file[10]==0 && file[11]==0,"envelope1 byte order");
    DetSessionCheckpoint decoded;require(readDetSave(save,decoded,error) && error.empty(),error);
    require(encodeDetCheckpoint(decoded)==canonical,"complete read bytes");
    DeterministicSession restarted(config);require(configure(restarted),restarted.error());
    require(loadDetSession(save,restarted,error) && bytes(restarted)==canonical,error);
    advance(baseline,12);advance(restarted,12);require(bytes(restarted)==bytes(baseline),"new Session continuation RNG/events/identity/full checkpoint");
    Evidence evidence{detCheckpointHash(*baseline.checkpoint()),file.size(),0,0};
    require(!saveDetSession(save,baseline,error) && !error.empty() && raw(save)==file,"fresh-only does not overwrite earlier save");
    const auto existing=directory/"existing-empty.dsv";rawWrite(existing,{});
    require(!writeDetSave(existing,saved,error) && fs::file_size(existing)==0,"existing empty save protected");
    const auto folder=directory/"directory.dsv";require(fs::create_directory(folder),"fixture directory");
    require(!writeDetSave(folder,saved,error) && fs::is_directory(folder),"directory target protected");
    require(!readDetSave(folder,decoded,error),"directory source rejected");
    require(!writeDetSave(directory/"missing-parent"/"save.dsv",saved,error),"missing output parent rejected");

    const auto rejectRead=[&](const fs::path& path){
        const auto before=encodeDetCheckpoint(decoded);require(!readDetSave(path,decoded,error) && !error.empty(),"malformed save rejected: "+path.string());
        require(encodeDetCheckpoint(decoded)==before,"failed read output is atomic");
        const auto state=bytes(restarted);require(!loadDetSession(path,restarted,error) && bytes(restarted)==state,"file failure does not change Session");++evidence.malformed;
    };
    rejectRead(directory/"missing.dsv");
    for(unsigned kind=0;kind<9;++kind){auto damaged=file;
        if(kind==0)damaged[0]^=1;
        if(kind==1)put32(damaged,8,2);
        if(kind==2)put32(damaged,12,static_cast<std::uint32_t>(kDetSaveMaxCheckpointBytes+1));
        if(kind==3)put32(damaged,12,15);
        if(kind==4)damaged.pop_back();
        if(kind==5)damaged.push_back(0);
        if(kind==6)damaged.back()^=1;
        if(kind==7)damaged.resize(15);
        if(kind==8)damaged=canonical; // A raw .state payload is not a .dsv envelope.
        const auto path=directory/("malformed-"+std::to_string(kind)+".dsv");rawWrite(path,damaged);rejectRead(path);
    }
    const auto oversized=directory/"oversized.dsv";
    {std::ofstream stream(oversized,std::ios::binary|std::ios::out|std::ios::noreplace);
        require(bool(stream),"oversized fresh open");stream.seekp(static_cast<std::streamoff>(kDetSaveMaxFileBytes));stream.put(0);stream.close();require(!stream.fail(),"oversized close");}
    rejectRead(oversized);
    // Codec validity is intentionally distinct from application semantic acceptance.
    auto semantic=saved;semantic.globals.at(3).front()+=1;const auto semanticFile=directory/"semantic-invalid.dsv";
    require(writeDetSave(semanticFile,semantic,error),error);require(readDetSave(semanticFile,decoded,error),error);
    const auto state=bytes(restarted);require(!loadDetSession(semanticFile,restarted,error) && bytes(restarted)==state,"pure validator rejection is atomic");
    auto altered=config;altered.contentHash+=1;DeterministicSession contentMismatch(altered);require(configure(contentMismatch),contentMismatch.error());
    const auto mismatchBefore=bytes(contentMismatch);require(!loadDetSession(save,contentMismatch,error) && bytes(contentMismatch)==mismatchBefore,"content identity rejection atomic");
    DeterministicSession logicMismatch(config);require(configure(logicMismatch,0x4453563338503032ULL),logicMismatch.error());
    const auto logicBefore=bytes(logicMismatch);require(!loadDetSession(save,logicMismatch,error) && bytes(logicMismatch)==logicBefore,"code identity rejection atomic");
    DeterministicSession unsealed(config);require(!loadDetSession(save,unsealed,error) && !unsealed.sealed() && unsealed.nextTick()==0,"unsealed load rejected");
    DeterministicSession faulted(config);require(configure(faulted),faulted.error());
    require(!faulted.advance({0,1,{{1,0,99,{}}}}) && faulted.faulted(),"fixture execution fault");
    const auto invalidCapture=directory/"invalid-capture.dsv";
    require(!saveDetSession(invalidCapture,faulted,error) && !fs::exists(invalidCapture),"failed capture creates no file");
    require(loadDetSession(save,faulted,error) && !faulted.faulted() && bytes(faulted)==canonical,"explicit persisted restore recovers faulted Session");
    auto bad=saved;bad.actors.begin()->second.pose.profileVersion=999;
    const auto invalidEncode=directory/"invalid-encode.dsv";
    require(!writeDetSave(invalidEncode,bad,error) && !fs::exists(invalidEncode),"invalid checkpoint preflight creates no file");
    std::error_code linkError;const auto link=directory/"link.dsv";fs::create_symlink(save.filename(),link,linkError);
    if(linkError){++evidence.linkSkips;std::cout<<"SKIP symbolic-link creation: "<<linkError.message()<<'\n';}
    else {const auto before=encodeDetCheckpoint(decoded);require(!readDetSave(link,decoded,error)
        && encodeDetCheckpoint(decoded)==before && !writeDetSave(link,saved,error) && raw(save)==file,"symbolic-link source/destination rejected");
        const auto dangling=directory/"dangling.dsv";fs::create_symlink("absent.dsv",dangling);
        require(!writeDetSave(dangling,saved,error) && fs::is_symlink(dangling),"dangling symbolic-link target protected");}
    return evidence;
}
fs::path fresh(const fs::path& parent){require(fs::is_directory(parent),"--output requires an existing parent");
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned n=0;n<1024;++n){auto path=parent/("save-"+std::to_string(stamp)+"-"+std::to_string(n));if(fs::create_directory(path))return path;}
    throw std::runtime_error("Cannot create fresh save fixture directory");
}
void report(const fs::path& path,const std::string& text){std::ofstream stream(path,std::ios::binary|std::ios::out|std::ios::noreplace);require(bool(stream),"summary fresh open");stream<<text;stream.close();require(!stream.fail(),"summary close");}
} // namespace
int main(int argc,char** argv){fs::path directory;try{
    fs::path parent=fs::temp_directory_path();if(argc==3 && std::string(argv[1])=="--output")parent=argv[2];
    else if(argc!=1){std::cerr<<"usage: [--output EXISTING_PARENT]\n";return 2;}
    directory=fresh(parent);const auto evidence=checks(directory);std::ostringstream summary;
    summary<<"{\"status\":\"passed\",\"saveEnvelope\":1,\"ticks\":12,\"restoreTick\":6,\"saveBytes\":"<<evidence.fileBytes
        <<",\"malformedFiles\":"<<evidence.malformed<<",\"symbolicLinkSkips\":"<<evidence.linkSkips
        <<",\"finalHash\":\""<<evidence.hash<<"\"}\n";
    report(directory/"summary.json",summary.str());std::cout<<summary.str();return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';if(!directory.empty())std::cerr<<"Failure artifacts: "<<directory.string()<<'\n';return 1;}}
