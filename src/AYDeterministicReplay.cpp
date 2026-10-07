#include <AYEntity/DeterministicReplay.h>
#include <AYReplay/FileReplayRecorder.h>
#include <AYReplay/FileReplayPlayer.h>
#include "detail/DetSessionWire.h"
#include <cstring>
#include <filesystem>

namespace ayt::entity {
using namespace detwire;
namespace {
constexpr std::uint32_t manifestEvent=0x20001,inputEvent=0x20002,witnessEvent=0x20003,checkpointEvent=0x20004,endEvent=0x20005;
constexpr std::size_t fileLimit=256*1024*1024;
std::vector<std::uint8_t> completion(std::uint64_t tick,std::uint64_t hash) {Writer w;w.u32(0x45534441);w.u32(1);w.u64(tick);w.u64(hash);return w.finish();}
}
struct DetReplayWriter::Impl {
    DeterministicSession* session=nullptr;
    std::unique_ptr<replay::FileReplayRecorder> recorder;
    std::uint32_t interval=300;
    std::uint64_t next=0;
    bool failed=false,finished=false;
    std::string error;
    bool fail(std::string e){failed=true;error=std::move(e);return false;}
    bool record(std::uint64_t tick,std::uint32_t type,const std::vector<std::uint8_t>& bytes) {
        if(failed || recorder->bytesWritten()+bytes.size()+1024>=fileLimit)return fail("Replay file size limit");
        if(!recorder->recordEvent(tick,type,bytes.data(),bytes.size()) || recorder->rotationIndex()!=0)return fail("Replay write/rotation failure");return true;
    }
};
DetReplayWriter::DetReplayWriter():_impl(std::make_unique<Impl>()) {}
DetReplayWriter::~DetReplayWriter()=default;
bool DetReplayWriter::begin(DeterministicSession& session,std::string basePath,std::uint32_t interval) {
    auto& p=*_impl;if(p.recorder || !interval || !session.sealed() || session.nextTick()!=0)return p.fail("Recording needs a fresh sealed session and nonzero checkpoint interval");
    auto initial=session.checkpoint();if(!initial)return p.fail(session.error());
    if(!std::filesystem::path(basePath).has_extension())basePath+=".rpl";
    p.session=&session;p.interval=interval;p.recorder=std::make_unique<replay::FileReplayRecorder>(std::move(basePath),fileLimit,UINT32_MAX);
    replay::ReplayFileHeader header{};header.magic=replay::kReplayMagic;header.version=replay::kReplayVersion;header.schemaVersion=1;
    std::memcpy(header.sceneName,"DeterministicSession",20);
    const auto rate=(math::DetFloat32::fromInt(1000)/session.fixedStep()).toUInt();header.tickRateMilliHz=rate.value_or(0);
    if(!p.recorder->beginSession(header))return p.fail("Replay open failed");
    try {return p.record(0,manifestEvent,session.manifest()) && p.record(0,checkpointEvent,encodeDetCheckpoint(*initial));}
    catch(const std::exception& e){return p.fail(e.what());}
}
bool DetReplayWriter::advance(DetTickInput input) {
    auto& p=*_impl;if(!p.recorder || p.failed || p.finished || !p.session || p.session->nextTick()!=p.next)return p.fail("Recording lost sequential tick ownership");
    if(!canonicalizeDetInput(input,p.error))return false;
    try {
        auto packet=encodeDetInput(input);
        if(!p.session->advance(input)){p.error=p.session->error();return false;}
        auto state=p.session->checkpoint();if(!state)return p.fail(p.session->error());
        auto witness=encodeDetCheckpoint(*state);
        if(!p.record(input.tick,inputEvent,packet) || !p.record(state->nextTick,witnessEvent,witness))return false;
        p.next=state->nextTick;
        if(p.next%p.interval==0 && !p.record(p.next,checkpointEvent,witness))return false;
        p.error.clear();return true;
    }catch(const std::exception& e){return p.fail(e.what());}
}
bool DetReplayWriter::finish() {
    auto& p=*_impl;if(!p.recorder || p.failed || p.finished || p.session->nextTick()!=p.next)return p.fail("Recording cannot be sealed");
    auto s=p.session->checkpoint();if(!s)return p.fail(p.session->error());
    try {
        if(!p.record(p.next,endEvent,completion(p.next,detCheckpointHash(*s))) || !p.recorder->endSession())return p.fail("Replay completion write failed");
        p.finished=true;return true;
    }catch(const std::exception& e){return p.fail(e.what());}
}
std::string DetReplayWriter::path() const {return _impl->recorder?_impl->recorder->currentPath():std::string{};}
const std::string& DetReplayWriter::error() const {return _impl->error;}
struct DetReplayReader::Impl {
    struct Tick {DetTickInput input;DetSessionCheckpoint expected;};
    std::vector<Tick> ticks;
    std::map<std::uint64_t,DetSessionCheckpoint> checkpoints;
    std::vector<std::uint8_t> manifest;
    std::size_t cursor=0;
    bool opened=false;
    std::string error;
    std::optional<DetStateDifference> difference;
    bool fail(std::string e){error=std::move(e);return false;}
};
DetReplayReader::DetReplayReader():_impl(std::make_unique<Impl>()) {}
DetReplayReader::~DetReplayReader()=default;
bool DetReplayReader::open(std::string path) {
    _impl=std::make_unique<Impl>();
    auto staged=std::make_unique<Impl>();auto& p=*staged;
    replay::FileReplayPlayer player(std::move(path));
    if(player.open()!=replay::IReplayPlayer::Error::Ok || player.getHeader().schemaVersion!=1 || player.getHeader().rotationIndex!=0)return _impl->fail("Replay container open/version failed");
    bool complete=false,waiting=false,sawEnd=false;std::size_t byteBudget=0;std::optional<DetTickInput> pending;
    try {
        for(unsigned records=0;records<100000;++records){
            replay::ReplayEventHeader header{};std::vector<std::uint8_t> payload;bool legacy=false;
            if(player.readNextEvent(header,payload,&legacy)!=replay::IReplayPlayer::Error::Ok || legacy)throw std::runtime_error("Corrupt/legacy replay record");
            byteBudget+=payload.size();if(byteBudget>fileLimit)throw std::runtime_error("Decoded replay size limit");
            if(header.eventType==replay::kEvtFoundation_SessionEnd){if(!complete)throw std::runtime_error("Replay missing completion seal");sawEnd=true;break;}
            if(header.eventType==replay::kEvtFoundation_SessionBegin){if(records!=0 || header.tick!=0 || !p.manifest.empty())throw std::runtime_error("Unexpected foundation session marker");continue;}
            if(complete)throw std::runtime_error("Records after completion seal");
            if(header.eventType==manifestEvent){if(!p.manifest.empty() || header.tick!=0 || !p.ticks.empty())throw std::runtime_error("Unexpected manifest");
                (void)checked(payload,manifestMagic);p.manifest=std::move(payload);continue;}
            if(p.manifest.empty())throw std::runtime_error("Missing session manifest");
            if(header.eventType==inputEvent){
                if(waiting || !p.checkpoints.contains(0))throw std::runtime_error("Input without initial checkpoint or duplicate input");
                DetTickInput input;if(!decodeDetInput(payload,input,p.error) || input.tick!=p.ticks.size() || input.tick!=header.tick)throw std::runtime_error("Invalid/gapped replay input");
                pending=std::move(input);waiting=true;
            }else if(header.eventType==witnessEvent || header.eventType==checkpointEvent){
                DetSessionCheckpoint s;if(!decodeDetCheckpoint(payload,s,p.error) || s.manifest!=p.manifest || s.nextTick!=header.tick)throw std::runtime_error("Invalid replay state witness/checkpoint");
                if(header.eventType==witnessEvent){
                    if(!waiting || s.nextTick!=p.ticks.size()+1)throw std::runtime_error("Missing/unordered tick witness");
                    p.ticks.push_back({std::move(*pending),std::move(s)});pending.reset();waiting=false;
                }else {
                    if(waiting || s.nextTick!=p.ticks.size() || p.checkpoints.contains(s.nextTick))throw std::runtime_error("Unexpected checkpoint boundary");
                    if(!p.ticks.empty() && firstDetDifference(p.ticks.back().expected,s))throw std::runtime_error("Checkpoint disagrees with tick witness");
                    p.checkpoints.emplace(s.nextTick,std::move(s));
                }
            }else if(header.eventType==endEvent){
                auto r=checked(payload,0x45534441);const auto tick=r.u64(),hash=r.u64();r.end();
                if(waiting || !p.checkpoints.contains(0) || tick!=p.ticks.size() || header.tick!=tick)throw std::runtime_error("Invalid completion boundary");
                const auto& s=p.ticks.empty()?p.checkpoints.at(0):p.ticks.back().expected;
                if(hash!=detCheckpointHash(s))throw std::runtime_error("Completion state hash mismatch");complete=true;
            }else throw std::runtime_error("Unknown session adapter event");
        }
        if(!complete || !sawEnd)throw std::runtime_error("Replay record limit or missing seal/end");
        p.opened=true;_impl=std::move(staged);return true;
    }catch(const std::exception& e){return _impl->fail(e.what());}
}
bool DetReplayReader::restoreInitial(DeterministicSession& session) {return seek(session,0);}
bool DetReplayReader::advance(DeterministicSession& session) {
    auto& p=*_impl;if(!p.opened || p.cursor>=p.ticks.size())return p.fail("Replay exhausted/not open");
    p.difference.reset();const auto& t=p.ticks[p.cursor];
    if(session.manifest()!=p.manifest || session.nextTick()!=t.input.tick)return p.fail("Replay session manifest/tick mismatch");
    if(!session.advance(t.input))return p.fail(session.error());
    auto actual=session.checkpoint();if(!actual)return p.fail(session.error());
    p.difference=firstDetDifference(t.expected,*actual);
    if(p.difference)return p.fail("Replay state diverged at tick "+std::to_string(p.difference->tick));
    ++p.cursor;p.error.clear();return true;
}
bool DetReplayReader::seek(DeterministicSession& session,std::uint64_t tick) {
    auto& p=*_impl;if(!p.opened || tick>p.ticks.size() || session.manifest()!=p.manifest)return p.fail("Invalid replay seek/manifest");
    auto cp=p.checkpoints.upper_bound(tick);if(cp==p.checkpoints.begin())return p.fail("No replay restart point");--cp;
    if(!session.restore(cp->second))return p.fail(session.error());p.cursor=static_cast<std::size_t>(cp->first);p.difference.reset();
    while(p.cursor<tick)if(!advance(session))return false;p.error.clear();return true;
}
bool DetReplayReader::atEnd() const {return _impl->opened && _impl->cursor==_impl->ticks.size();}
std::uint64_t DetReplayReader::tickCount() const {return _impl->ticks.size();}
const std::string& DetReplayReader::error() const {return _impl->error;}
const std::optional<DetStateDifference>& DetReplayReader::difference() const {return _impl->difference;}
} // namespace ayt::entity
