#include <AYEntity/DeterministicRollbackReplay.h>
#include <AYReplay/FileReplayRecorder.h>
#include <AYReplay/FileReplayPlayer.h>
#include "detail/DetSessionWire.h"
#include <filesystem>
#include <cstring>
#include <fstream>

namespace ayt::entity {
using namespace detwire;
namespace {
constexpr std::uint32_t recordEvent=0x20006, magic=0x52524441;
constexpr std::size_t fileLimit=256*1024*1024, recordLimit=100000;
Writer recordHeader(unsigned kind,unsigned epoch) {Writer w;w.u32(magic);w.u32(1);w.u32(kind);w.u32(epoch);return w;}
bool sameState(const DetSessionCheckpoint& a,const DetSessionCheckpoint& b) {return !firstDetDifference(a,b);}
// Foundation synthesizes SessionEnd at EOF. This adapter requires the actual
// physical marker, rejects trailing bytes and bounds records before decoding.
void validateContainerShape(const std::string& path) {
    const auto size=std::filesystem::file_size(path);
    if(size<sizeof(replay::ReplayFileHeader)+2*sizeof(replay::ReplayEventHeader) || size>fileLimit)
        throw std::runtime_error("Rollback replay stored byte budget/size");
    std::ifstream stream(path,std::ios::binary);stream.seekg(sizeof(replay::ReplayFileHeader));
    std::uint64_t offset=sizeof(replay::ReplayFileHeader);bool ended=false;
    for(std::size_t n=0;offset<size && n<recordLimit;++n) {
        replay::ReplayEventHeader h{};
        if(size-offset<sizeof(h) || !stream.read(reinterpret_cast<char*>(&h),sizeof(h)))
            throw std::runtime_error("Truncated rollback container header");
        offset+=sizeof(h);
        if(h.payloadSize>size-offset || h.reserved[0] || h.reserved[1] || h.reserved[2]
            || (h.flags & ~replay::kEvtFlagPayloadCompressed))throw std::runtime_error("Invalid rollback container shape");
        if(h.eventType==replay::kEvtFoundation_SessionBegin) {
            if(n!=0 || h.tick || h.payloadSize || h.flags)
                throw std::runtime_error("Unexpected physical session begin");
        }else if(h.eventType==replay::kEvtFoundation_SessionEnd) {
            if(h.tick || h.payloadSize || h.flags || offset!=size)throw std::runtime_error("Trailing rollback replay bytes");
            ended=true;break;
        }else if(h.eventType!=recordEvent)throw std::runtime_error("Unknown rollback container event");
        offset+=h.payloadSize;stream.seekg(h.payloadSize,std::ios::cur);
    }
    if(!ended)throw std::runtime_error("Missing physical session end/record budget");
}
}
struct DetRollbackReplayWriter::Impl {
    std::unique_ptr<replay::FileReplayRecorder> file;
    const DeterministicRollbackNetwork* owner=nullptr;
    std::uint32_t epoch=0, interval=300;
    std::uint64_t next=0;
    std::size_t records=0;
    DetSessionCheckpoint last;
    bool failed=false, finished=false;
    std::string error;
    bool fail(std::string e){failed=true;error=std::move(e);return false;}
    bool write(Writer w) {
        auto bytes=w.finish();
        if(records+3>=recordLimit || file->bytesWritten()+bytes.size()+1024>=fileLimit)
            return fail("Rollback recording size/record budget");
        if(!file->recordEvent(next,recordEvent,bytes.data(),bytes.size()) || file->rotationIndex()!=0)
            return fail("Rollback recording I/O/rotation failure");
        ++records;return true;
    }
};
DetRollbackReplayWriter::DetRollbackReplayWriter():_impl(std::make_unique<Impl>()){}
DetRollbackReplayWriter::~DetRollbackReplayWriter()=default;
bool DetRollbackReplayWriter::begin(const DeterministicRollbackNetwork& net,std::string base,std::uint32_t interval) {
    auto& p=*_impl;
    try {
        if(p.file || p.failed || !interval || net.faulted() || net.diagnostics().head!=net.epochInitialTick())
            return p.fail("Rollback recording must begin at a healthy epoch initial boundary");
        auto cp=net.history().checkpointAt(net.epochInitialTick());if(!cp)return p.fail("Recording initial checkpoint unavailable");
        if(base.empty())return p.fail("Rollback recording path is empty");
        if(!std::filesystem::path(base).has_extension())base+=".rpl";
        if(std::filesystem::exists(replay::FileReplayRecorder::rotationPathFor(base,0)))return p.fail("Recording destination already exists");
        p.owner=&net;p.last=*cp;p.next=cp->nextTick;p.epoch=net.config().epoch;p.interval=interval;
        p.file=std::make_unique<replay::FileReplayRecorder>(std::move(base),fileLimit,UINT32_MAX);
        replay::ReplayFileHeader h{};h.magic=replay::kReplayMagic;h.version=replay::kReplayVersion;h.schemaVersion=2;
        std::memcpy(h.sceneName,"DetRollback",11);
        if(!p.file->beginSession(h))return p.fail("Rollback recording open failed");
        auto w=recordHeader(0,p.epoch);w.blob(encodeDetCheckpoint(p.last));
        return p.write(std::move(w));
    }catch(const std::exception& e){return p.fail(e.what());}
}
bool DetRollbackReplayWriter::sync(const DeterministicRollbackNetwork& net) {
    auto& p=*_impl;
    if(!p.file || p.failed || p.finished)return false;
    try {
        if(p.owner!=&net)return p.fail("Recording network owner changed");
        if(net.faulted())return p.fail("Cannot record a faulted rollback owner");
        if(net.config().epoch!=p.epoch) {
            const auto start=net.epochInitialTick();
            if(net.config().epoch<=p.epoch || start<p.next || net.recoveryJournalStart()>p.next)
                return p.fail("Recording recovery rewinds history or lacks event journal");
            auto cp=net.history().checkpointAt(start);
            if(!cp || cp->manifest!=p.last.manifest)return p.fail("Recording recovery checkpoint unavailable/incompatible");
            if(start==p.next && !sameState(p.last,*cp))return p.fail("Recovery changed already recorded verified state");
            auto journal=net.recoveryJournal();
            std::erase_if(journal,[&](const auto& e){return e.tick<p.next || e.tick>=start;});
            auto w=recordHeader(2,net.config().epoch);w.u64(p.next);w.blob(encodeDetCheckpoint(*cp));
            w.u32(static_cast<std::uint32_t>(journal.size()));
            for(const auto& e:journal){w.u32(e.epoch);w.u64(e.tick);command(w,e.command);}
            p.next=start;p.epoch=net.config().epoch;
            if(!p.write(std::move(w)))return false;p.last=std::move(*cp);
        }
        const auto end=net.verifiedNextTick();
        if(end<p.next)return p.fail("Recording confirmation frontier rewound");
        while(p.next<end) {
            const auto input=net.history().confirmedInputAt(p.next);
            auto cp=net.history().checkpointAt(p.next+1);
            if(!input || !cp)return p.fail("Recording missed retained confirmed history; sync every mutation");
            auto w=recordHeader(1,p.epoch);w.u32(cp->nextTick%p.interval==0?1:0);
            w.blob(encodeDetInput(*input));w.blob(encodeDetCheckpoint(*cp));
            ++p.next;if(!p.write(std::move(w)))return false;p.last=std::move(*cp);
        }
        p.error.clear();return true;
    }catch(const std::exception& e){return p.fail(e.what());}
}
bool DetRollbackReplayWriter::finish(const DeterministicRollbackNetwork& net) {
    auto& p=*_impl;if(!sync(net))return false;
    try {
        auto w=recordHeader(3,p.epoch);w.u64(p.next);w.u64(net.diagnostics().head);
        w.u64(detCheckpointHash(p.last));w.u32(static_cast<std::uint32_t>(p.records));
        if(!p.write(std::move(w)) || !p.file->endSession())return p.fail("Rollback recording completion failed");
        p.finished=true;return true;
    }catch(const std::exception& e){return p.fail(e.what());}
}
std::string DetRollbackReplayWriter::path() const {return _impl->file?_impl->file->currentPath():std::string{};}
std::uint64_t DetRollbackReplayWriter::recordedNextTick() const{return _impl->next;}
const std::string& DetRollbackReplayWriter::error() const{return _impl->error;}

struct DetRollbackReplayReader::Impl {
    struct Entry {
        std::uint32_t epoch=0;
        DetSessionCheckpoint state;
        std::optional<DetTickInput> input;
        std::vector<DetConfirmedEvent> repair;
        bool restart=false;
    };
    std::vector<Entry> entries;
    std::vector<DetReplaySegment> segments;
    std::vector<DetConfirmedEvent> effects;
    std::size_t cursor=0;
    std::uint32_t epoch=0;
    std::uint64_t speculativeHead=0;
    bool opened=false;
    std::string error;
    std::optional<DetStateDifference> difference;
    bool fail(std::string e){error=std::move(e);return false;}
};
DetRollbackReplayReader::DetRollbackReplayReader():_impl(std::make_unique<Impl>()){}
DetRollbackReplayReader::~DetRollbackReplayReader()=default;
bool DetRollbackReplayReader::open(std::string path) {
    _impl=std::make_unique<Impl>();auto staged=std::make_unique<Impl>();auto& p=*staged;
    try {
        validateContainerShape(path);
        replay::FileReplayPlayer file(std::move(path));
        if(file.open()!=replay::IReplayPlayer::Error::Ok || file.getHeader().schemaVersion!=2 || file.getHeader().rotationIndex!=0)
            return _impl->fail("Rollback replay container/version mismatch");
        std::size_t bytes=0,records=0;bool sealed=false,ended=false;
        for(unsigned n=0;n<recordLimit;++n) {
            replay::ReplayEventHeader h{};std::vector<std::uint8_t> data;bool legacy=false;
            if(file.readNextEvent(h,data,&legacy)!=replay::IReplayPlayer::Error::Ok || legacy)throw std::runtime_error("Corrupt rollback replay container");
            bytes+=data.size();if(bytes>fileLimit)throw std::runtime_error("Rollback replay decoded byte budget");
            if(h.eventType==replay::kEvtFoundation_SessionBegin){if(n!=0 || h.tick!=0)throw std::runtime_error("Unexpected session marker");continue;}
            if(h.eventType==replay::kEvtFoundation_SessionEnd){if(!sealed)throw std::runtime_error("Rollback replay missing completion seal");ended=true;break;}
            if(sealed || h.eventType!=recordEvent)throw std::runtime_error("Unknown/trailing rollback replay record");
            auto r=checked(data,magic);const auto kind=r.u32(),epoch=r.u32();
            if(!epoch)throw std::runtime_error("Zero replay epoch");
            if(kind==3) {
                const auto tick=r.u64(),head=r.u64(),hash=r.u64();const auto count=r.u32();r.end();
                if(p.entries.empty() || epoch!=p.entries.back().epoch || tick!=p.entries.back().state.nextTick
                    || tick!=h.tick || head<tick || count!=records || hash!=detCheckpointHash(p.entries.back().state))
                    throw std::runtime_error("Invalid rollback replay completion");
                p.speculativeHead=head;sealed=true;continue;
            }
            Impl::Entry e;e.epoch=epoch;
            std::uint64_t previous=0;
            if(kind==1) {
                const auto restart=r.u32();if(restart>1)throw std::runtime_error("Invalid restart flag");e.restart=restart!=0;
                const auto input=r.blob(maxInputBytes+64);DetTickInput in;std::string error;
                if(!decodeDetInput(input,in,error) || encodeDetInput(in)!=input)throw std::runtime_error("Invalid confirmed input: "+error);
                e.input=std::move(in);
            }else if(kind==2)previous=r.u64();
            else if(kind!=0)throw std::runtime_error("Unknown rollback replay kind");
            const auto cp=r.blob();std::string error;
            if(!decodeDetCheckpoint(cp,e.state,error) || encodeDetCheckpoint(e.state)!=cp || e.state.nextTick!=h.tick)
                throw std::runtime_error("Invalid rollback replay checkpoint: "+error);
            if(kind==0) {
                if(!p.entries.empty())throw std::runtime_error("Duplicate rollback replay initial state");
                p.segments.push_back({epoch,e.state.nextTick,e.state.nextTick,0});e.restart=true;
            }else {
                if(p.entries.empty() || e.state.manifest!=p.entries.front().state.manifest)throw std::runtime_error("Rollback replay manifest mismatch");
                const auto& last=p.entries.back();
                if(kind==1) {
                    if(epoch!=last.epoch || e.input->tick!=last.state.nextTick || last.state.nextTick==UINT64_MAX
                        || e.state.nextTick!=last.state.nextTick+1)throw std::runtime_error("Gapped confirmed replay input");
                    p.segments.back().endTick=e.state.nextTick;
                }else {
                    if(epoch<=last.epoch || previous!=last.state.nextTick || e.state.nextTick<previous)
                        throw std::runtime_error("Invalid replay epoch boundary");
                    const auto count=r.count(65536);
                    for(unsigned i=0;i<count;++i) {
                        DetConfirmedEvent event;event.epoch=r.u32();event.tick=r.u64();event.command=command(r);
                        if(!event.epoch || event.epoch>=epoch || event.tick<previous || event.tick>=e.state.nextTick
                            || !event.command.source || !event.command.type)throw std::runtime_error("Invalid replay recovery event");
                        if(!e.repair.empty()) {const auto& tail=e.repair.back();
                            if(std::tuple(event.tick,event.command.source,event.command.sequence)<=std::tuple(tail.tick,tail.command.source,tail.command.sequence))
                                throw std::runtime_error("Unordered replay recovery events");}
                        e.repair.push_back(std::move(event));
                    }
                    if(e.state.nextTick==previous && !sameState(last.state,e.state))throw std::runtime_error("Recovery changed already verified state");
                    p.segments.push_back({epoch,e.state.nextTick,e.state.nextTick,e.state.nextTick-previous});e.restart=true;
                }
            }
            r.end();p.entries.push_back(std::move(e));++records;
        }
        if(!sealed || !ended || p.entries.empty())throw std::runtime_error("Incomplete rollback replay");
        p.opened=true;p.epoch=p.entries.front().epoch;_impl=std::move(staged);return true;
    }catch(const std::exception& e){return _impl->fail(e.what());}
}
bool DetRollbackReplayReader::restoreInitial(DeterministicSession& session) {
    auto& p=*_impl;if(!p.opened)return p.fail("Rollback replay not open");
    return seek(session,p.segments.front().epoch,p.segments.front().firstTick);
}
bool DetRollbackReplayReader::advance(DeterministicSession& session) {
    auto& p=*_impl;if(!p.opened || p.cursor==0 || p.cursor>=p.entries.size())return p.fail("Rollback replay exhausted/not restored");
    p.difference.reset();
    while(p.cursor<p.entries.size()) {
        const auto& e=p.entries[p.cursor];
        if(session.manifest()!=e.state.manifest) return p.fail("Rollback replay session manifest mismatch");
        if(!e.input) {
            if(session.nextTick()!=p.entries[p.cursor-1].state.nextTick)return p.fail("Recovery playback lost tick ownership");
            if(!session.restore(e.state))return p.fail(session.error());
            p.effects.insert(p.effects.end(),e.repair.begin(),e.repair.end());p.epoch=e.epoch;++p.cursor;continue;
        }
        if(session.nextTick()!=e.input->tick)return p.fail("Rollback replay input tick mismatch");
        if(!session.advance(*e.input))return p.fail(session.error());
        const auto actual=session.checkpoint();if(!actual)return p.fail(session.error());
        p.difference=firstDetDifference(e.state,*actual);
        if(p.difference)return p.fail("Rollback replay diverged at tick "+std::to_string(p.difference->tick));
        for(const auto& c:e.state.pendingEvents)p.effects.push_back({e.epoch,e.input->tick,c});
        p.epoch=e.epoch;++p.cursor;p.error.clear();return true;
    }
    p.error.clear();return true;
}
bool DetRollbackReplayReader::seek(DeterministicSession& session,std::uint32_t epoch,std::uint64_t tick) {
    auto& p=*_impl;
    if(!p.opened || session.manifest()!=p.entries.front().state.manifest)return p.fail("Rollback replay not open/manifest mismatch");
    const auto segment=std::find_if(p.segments.begin(),p.segments.end(),[&](const auto& s){return s.epoch==epoch;});
    if(segment==p.segments.end() || tick<segment->firstTick || tick>segment->endTick)return p.fail("Seek outside recorded epoch or inside recovery gap");
    std::size_t restart=0;
    for(std::size_t i=0;i<p.entries.size();++i)if(p.entries[i].epoch==epoch && p.entries[i].restart && p.entries[i].state.nextTick<=tick)restart=i;
    if(!session.restore(p.entries[restart].state))return p.fail(session.error());
    p.cursor=restart+1;p.epoch=epoch;p.effects.clear();p.difference.reset();
    while(session.nextTick()<tick) {if(!advance(session)){p.effects.clear();return false;}p.effects.clear();}
    p.error.clear();return true;
}
bool DetRollbackReplayReader::atEnd() const{return _impl->opened && _impl->cursor==_impl->entries.size();}
std::uint64_t DetRollbackReplayReader::endTick() const{return _impl->entries.empty()?0:_impl->entries.back().state.nextTick;}
std::uint64_t DetRollbackReplayReader::speculativeHeadAtSeal() const{return _impl->speculativeHead;}
std::uint32_t DetRollbackReplayReader::epoch() const{return _impl->epoch;}
const std::vector<DetReplaySegment>& DetRollbackReplayReader::segments() const{return _impl->segments;}
std::vector<DetConfirmedEvent> DetRollbackReplayReader::takeConfirmedEvents(){std::vector<DetConfirmedEvent> out;out.swap(_impl->effects);return out;}
const std::string& DetRollbackReplayReader::error() const{return _impl->error;}
const std::optional<DetStateDifference>& DetRollbackReplayReader::difference() const{return _impl->difference;}
} // namespace ayt::entity
