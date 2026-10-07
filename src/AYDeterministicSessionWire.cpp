#include "detail/DetSessionWire.h"
#include <algorithm>

namespace ayt::entity {
using namespace detwire;
namespace {
void pose(Writer& w,const DetSimTransformComponent::Snapshot& p) {
    w.u32(p.snapshotVersion);w.u32(p.profileVersion);w.u32(p.rotationProfileVersion);
    for(auto v:p.previousPosition)w.u32(v);for(auto v:p.position)w.u32(v);
    for(auto v:p.previousRotation)w.u32(v);for(auto v:p.rotation)w.u32(v);
    w.u64(p.revision);w.u64(p.hasPreviousPosition,1);w.u64(p.rotationEnabled,1);w.u64(p.hasPreviousRotation,1);
}
DetSimTransformComponent::Snapshot pose(Reader& r) {
    DetSimTransformComponent::Snapshot p;
    p.snapshotVersion=r.u32();p.profileVersion=r.u32();p.rotationProfileVersion=r.u32();
    for(auto& v:p.previousPosition)v=r.u32();for(auto& v:p.position)v=r.u32();
    for(auto& v:p.previousRotation)v=r.u32();for(auto& v:p.rotation)v=r.u32();
    p.revision=r.u64();p.hasPreviousPosition=r.boolean();p.rotationEnabled=r.boolean();p.hasPreviousRotation=r.boolean();return p;
}
}
bool canonicalizeDetInput(DetTickInput& input,std::string& error) {
    if(input.version==0 || input.commands.size()>maxCommands) { error="Invalid input version/count";return false; }
    std::size_t bytes=0;
    for(const auto& c:input.commands) {
        if(!c.source || !c.type || c.payload.size()>maxInputBytes || (bytes+=c.payload.size()+16)>maxInputBytes) {
            error="Invalid command or input size";return false;
        }
    }
    auto sorted=input.commands;
    std::sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return commandKey(a)<commandKey(b);});
    for(std::size_t i=1;i<sorted.size();++i)if(commandKey(sorted[i-1])==commandKey(sorted[i])) {
        error="Duplicate input source/sequence";return false;
    }
    input.commands=std::move(sorted);error.clear();return true;
}
std::vector<std::uint8_t> encodeDetInput(const DetTickInput& input) {
    auto canonical=input;std::string error;
    if(!canonicalizeDetInput(canonical,error))throw std::invalid_argument(error);
    Writer w;w.u32(inputMagic);w.u32(1);w.u64(input.tick);w.u32(input.version);
    w.u32(static_cast<std::uint32_t>(canonical.commands.size()));for(const auto& c:canonical.commands)command(w,c);return w.finish();
}
bool decodeDetInput(std::span<const std::uint8_t> bytes,DetTickInput& input,std::string& error) {
    try {
        if(bytes.size()>maxInputBytes+32)throw std::runtime_error("Input record size limit");
        auto r=checked(bytes,inputMagic);DetTickInput staged;staged.tick=r.u64();staged.version=r.u32();
        const auto n=r.count(maxCommands);for(unsigned i=0;i<n;++i)staged.commands.push_back(command(r));r.end();
        if(!canonicalizeDetInput(staged,error))return false;
        if(encodeDetInput(staged)!=std::vector<std::uint8_t>(bytes.begin(),bytes.end()))throw std::runtime_error("Non-canonical input record");
        input=std::move(staged);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
std::vector<std::uint8_t> encodeDetCheckpoint(const DetSessionCheckpoint& s) {
    Writer w;w.u32(checkpointMagic);w.u32(1);w.blob(s.manifest);w.u64(s.nextTick);
    w.u32(static_cast<std::uint32_t>(s.actors.size()));
    for(const auto& [id,a]:s.actors){w.u64(id);pose(w,a.pose);blocks(w,a.blocks);}
    blocks(w,s.globals);w.u32(static_cast<std::uint32_t>(s.randomStreams.size()));
    for(const auto& [id,rng]:s.randomStreams){w.u32(id);w.u64(rng.state);w.u64(rng.inc);}
    w.u32(static_cast<std::uint32_t>(s.pendingEvents.size()));for(const auto& c:s.pendingEvents)command(w,c);
    w.u32(static_cast<std::uint32_t>(s.retiredIds.size()));for(auto id:s.retiredIds)w.u64(id);return w.finish();
}
bool decodeDetCheckpoint(std::span<const std::uint8_t> bytes,DetSessionCheckpoint& state,std::string& error) {
    try {
        auto r=checked(bytes,checkpointMagic);DetSessionCheckpoint s;s.manifest=r.blob(maxInputBytes);
        (void)checked(s.manifest,manifestMagic);s.nextTick=r.u64();
        auto n=r.count(maxActors);for(unsigned i=0;i<n;++i){auto id=r.u64();DetActorState a;a.pose=pose(r);a.blocks=blocks(r);
            DetSimTransformComponent test;if(!test.restore(a.pose))throw std::runtime_error("Invalid checkpoint pose/profile");
            if(!id || !s.actors.emplace(id,std::move(a)).second)throw std::runtime_error("Duplicate/zero actor ID");}
        s.globals=blocks(r);n=r.count(maxSchemas);for(unsigned i=0;i<n;++i){auto id=r.u32();auto a=r.u64(),b=r.u64();
            if(!id || !(b&1) || !s.randomStreams.emplace(id,math::pcg32_state{a,b}).second)throw std::runtime_error("Invalid RNG stream");}
        n=r.count(maxCommands);for(unsigned i=0;i<n;++i)s.pendingEvents.push_back(command(r));
        n=r.count(65536);for(unsigned i=0;i<n;++i)s.retiredIds.push_back(r.u64());r.end();
        DetTickInput events{s.nextTick,1,s.pendingEvents};
        if(!canonicalizeDetInput(events,error) || events.commands!=s.pendingEvents)
            throw std::runtime_error("Invalid checkpoint event order/size");
        if(!std::is_sorted(s.retiredIds.begin(),s.retiredIds.end())
            || std::adjacent_find(s.retiredIds.begin(),s.retiredIds.end())!=s.retiredIds.end())
            throw std::runtime_error("Invalid checkpoint retired identity order");
        for(auto id:s.retiredIds)if(!id || s.actors.contains(id))
            throw std::runtime_error("Invalid checkpoint retired identity");
        if(encodeDetCheckpoint(s)!=std::vector<std::uint8_t>(bytes.begin(),bytes.end()))throw std::runtime_error("Non-canonical checkpoint");
        // The session additionally validates the exact manifest, schemas and event producers.
        state=std::move(s);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
std::uint64_t detCheckpointHash(const DetSessionCheckpoint& s) {
    const auto b=encodeDetCheckpoint(s);return replay::fnv1a64(b.data(),b.size()-8);
}
std::optional<DetStateDifference> firstDetDifference(const DetSessionCheckpoint& a,const DetSessionCheckpoint& b) {
    const auto tick=std::min(a.nextTick,b.nextTick);
    if(a.manifest!=b.manifest)return DetStateDifference{tick,0,0,0,0,0,"manifest"};
    if(a.nextTick!=b.nextTick)return DetStateDifference{tick,0,a.nextTick,b.nextTick,0,0,"tick"};
    // Pull stable custom field IDs from the exact common manifest.
    std::map<std::uint32_t,std::vector<std::uint32_t>> schema;
    try {
        auto r=checked(a.manifest,manifestMagic);
        for(unsigned i=0;i<7;++i)r.u32();r.u64();for(unsigned i=0;i<4;++i)r.u32();
        auto n=r.count(maxSchemas);for(unsigned i=0;i<n;++i){auto id=r.u32();r.u32();auto c=r.count(maxFields);
            auto& fields=schema[id];for(unsigned j=0;j<c;++j){fields.push_back(r.u32());r.u64();}}
    }catch(...){return DetStateDifference{tick,0,0,0,0,0,"manifest"};}
    using Key=std::tuple<std::string,SimEntityId,std::uint32_t,std::uint32_t>;
    auto flatten=[&](const DetSessionCheckpoint& s){
        std::map<Key,std::uint64_t> f;
        auto addBlocks=[&](SimEntityId id,const DetStateBlocks& blocks){for(const auto& [cid,words]:blocks){
            f[{"state-shape",id,cid,0}]=words.size();for(std::size_t i=0;i<words.size();++i){
                auto found=schema.find(cid);auto fid=found!=schema.end() && i<found->second.size()?found->second[i]:static_cast<std::uint32_t>(i+1);
                f[{"state",id,cid,fid}]=words[i];}}};
        addBlocks(0,s.globals);
        for(const auto& [id,actor]:s.actors){
            f[{"actor",id,0,0}]=1;Writer w;pose(w,actor.pose);
            // Pose fields: schema/profile IDs 1..3, previous/current position
            // 4..9, previous/current rotation 10..17, revision 18, flags 19..21.
            Reader r{w.bytes};for(unsigned i=1;i<=17;++i)f[{"pose",id,1,i}]=r.u32();
            f[{"pose",id,1,18}]=r.u64();for(unsigned i=19;i<=21;++i)f[{"pose",id,1,i}]=r.u64(1);
            addBlocks(id,actor.blocks);
        }
        for(const auto& [id,rng]:s.randomStreams){f[{"rng",0,id,1}]=rng.state;f[{"rng",0,id,2}]=rng.inc;}
        Writer ew;ew.u32(static_cast<std::uint32_t>(s.pendingEvents.size()));for(const auto& c:s.pendingEvents)command(ew,c);
        for(std::size_t i=0;i<ew.bytes.size();++i)f[{"events",0,0,static_cast<std::uint32_t>(i)}]=ew.bytes[i];
        for(std::size_t i=0;i<s.retiredIds.size();++i)f[{"retired",0,0,static_cast<std::uint32_t>(i)}]=s.retiredIds[i];return f;
    };
    auto left=flatten(a),right=flatten(b);auto li=left.begin(),ri=right.begin();
    while(li!=left.end() || ri!=right.end()) {
        const bool l=li!=left.end(),r=ri!=right.end();
        Key key=!r || (l && li->first<ri->first)?li->first:ri->first;
        const bool hasL=l && li->first==key,hasR=r && ri->first==key;
        auto av=hasL?li->second:0,bv=hasR?ri->second:0;
        if(!hasL || !hasR || av!=bv){auto [section,id,cid,fid]=key;return DetStateDifference{tick,id,av,bv,cid,fid,section};}
        ++li;++ri;
    }return std::nullopt;
}
} // namespace ayt::entity
