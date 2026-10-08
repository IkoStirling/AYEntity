#include <AYEntity/DeterministicRollbackReplay.h>
#include <AYReplay/FileReplayRecorder.h>
#include <AYReplay/FileReplayPlayer.h>
#include "detail/DetSessionWire.h"
#include "detail/DetRollbackArchive.h"
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
    std::uint32_t epoch=0,interval=300,parts=0,epochCount=0,startEpoch=0;
    std::uint64_t next=0,decoded=0,indexBytes=0,indexChain=0,startTick=0,startHash=0;
    std::size_t records=0;
    DetSessionCheckpoint last;
    DetReplaySegment logical;
    std::optional<DetRollbackReplayArchiveOptions> archive;
    std::ofstream index;
    std::string output,partial,base,current,error;
    bool failed=false,finished=false;
    bool fail(std::string e){failed=true;error=std::move(e);return false;}
    std::uint64_t byteCap() const {return archive?archive->maxSegmentBytes:fileLimit;}
    std::uint32_t recordCap() const {return archive?archive->maxSegmentRecords:99996;}
    static constexpr std::uint64_t sealBytes=52,sealReserve=sealBytes+2*sizeof(replay::ReplayEventHeader);
    bool appendIndex(Writer w) {
        auto bytes=w.finish();
        if(indexBytes+4+bytes.size()+128>detarchive::indexLimit)return fail("Archive index byte budget");
        const auto size=static_cast<std::uint32_t>(bytes.size());char prefix[4];
        for(unsigned i=0;i<4;++i)prefix[i]=static_cast<char>(size>>(8*i));
        index.write(prefix,4);index.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());index.flush();
        if(!index)return fail("Archive index write/flush failed");
        Reader trailer{std::span<const std::uint8_t>(bytes).last(8)};indexChain=trailer.u64();indexBytes+=4+bytes.size();return true;
    }
    bool raw(std::span<const std::uint8_t> bytes,std::uint64_t tick) {
        if(!file->recordEvent(tick,recordEvent,bytes.data(),bytes.size()) || file->rotationIndex()!=0)
            return fail("Rollback recording I/O/unexpected rotation failure");
        ++records;decoded+=bytes.size();return true;
    }
    bool openPart() {
        if(archive && parts>=archive->maxSegments)return fail("Archive file segment budget");
        const auto fileBase=archive?detarchive::partBase(output,parts):base;
        current=replay::FileReplayRecorder::rotationPathFor(fileBase,0);
        if(std::filesystem::exists(current))return fail("Recording segment destination already exists");
        file=std::make_unique<replay::FileReplayRecorder>(fileBase,fileLimit,UINT32_MAX);
        replay::ReplayFileHeader h{};h.magic=replay::kReplayMagic;h.version=replay::kReplayVersion;h.schemaVersion=2;
        std::memcpy(h.sceneName,"DetRollback",11);
        if(!file->beginSession(h))return fail("Rollback recording segment open failed");
        records=0;decoded=0;startEpoch=epoch;startTick=next;startHash=detCheckpointHash(last);
        auto initial=recordHeader(0,epoch);initial.blob(encodeDetCheckpoint(last));auto bytes=initial.finish();
        if(file->bytesWritten()+sizeof(replay::ReplayEventHeader)+bytes.size()+sealReserve>byteCap() || bytes.size()+sealBytes>byteCap())
            return fail("Archive initial checkpoint exceeds segment budget");
        if(!archive)output=current;
        return raw(bytes,next);
    }
    bool closePart(std::uint64_t head) {
        auto seal=recordHeader(3,epoch);seal.u64(next);seal.u64(head);seal.u64(detCheckpointHash(last));seal.u32(static_cast<std::uint32_t>(records));
        auto bytes=seal.finish();
        if(!file->recordEvent(next,recordEvent,bytes.data(),bytes.size()) || file->rotationIndex()!=0
            || !file->flush() || !file->endSession())return fail("Rollback recording segment seal/flush failed");
        validateContainerShape(current); // Require a readable physical SessionEnd, not synthesized EOF.
        if(archive) {
            DetReplayFileSegment e;e.path=current;e.storedBytes=std::filesystem::file_size(current);
            e.fileHash=detarchive::hashFile(current,e.storedBytes);e.firstEpoch=startEpoch;e.firstTick=startTick;e.initialHash=startHash;
            e.lastEpoch=epoch;e.endTick=next;e.finalHash=detCheckpointHash(last);e.records=records;
            auto w=detarchive::header(1,indexChain);detarchive::describeFile(w,e,parts);
            if(!appendIndex(std::move(w)))return false;
        }
        file.reset();++parts;return true;
    }
    bool appendEpoch() {
        if(epochCount>=detarchive::maxSegments)return fail("Archive epoch index budget");
        auto w=detarchive::header(2,indexChain);w.u32(logical.epoch);w.u64(logical.firstTick);w.u64(logical.endTick);w.u64(logical.skippedTicks);
        if(!appendIndex(std::move(w)))return false;++epochCount;return true;
    }
    bool write(Writer w,std::uint64_t tick) {
        auto bytes=w.finish();
        auto fits=[&]{return records<recordCap() && decoded+bytes.size()+sealBytes<=byteCap()
            && file->bytesWritten()+sizeof(replay::ReplayEventHeader)+bytes.size()+sealReserve<=byteCap();};
        if(!fits()) {
            if(!archive)return fail("Rollback recording size/record budget");
            if(records<=1)return fail("Archive indivisible record exceeds segment budget");
            if(parts+1>=archive->maxSegments)return fail("Archive file segment budget");
            if(!closePart(next) || !openPart())return false;
            if(!fits())return fail("Archive indivisible record exceeds segment budget");
        }
        return raw(bytes,tick);
    }
    bool start(const DeterministicRollbackNetwork& net,std::string path,std::uint32_t every,
               std::optional<DetRollbackReplayArchiveOptions> options) {
        try {
            if(file || owner || failed || !every || net.faulted() || net.diagnostics().head!=net.epochInitialTick())
                return fail("Rollback recording must begin at a healthy epoch initial boundary");
            if(options && (options->maxSegmentBytes<1024 || options->maxSegmentBytes>fileLimit
                || options->maxSegmentRecords<2 || options->maxSegmentRecords>99996
                || !options->maxSegments || options->maxSegments>detarchive::maxSegments))return fail("Invalid archive segment budgets");
            auto cp=net.history().checkpointAt(net.epochInitialTick());if(!cp)return fail("Recording initial checkpoint unavailable");
            if(path.empty())return fail("Rollback recording path is empty");
            if(!std::filesystem::path(path).has_extension())path+=".rpl";
            owner=&net;last=*cp;next=cp->nextTick;epoch=net.config().epoch;interval=every;archive=options;base=path;
            logical={epoch,next,next,0};
            if(archive) {
                auto destination=std::filesystem::path(path);destination.replace_extension(".rpi");output=destination.string();partial=output+".partial";
                if(std::filesystem::exists(output) || std::filesystem::exists(partial)
                    || std::filesystem::exists(detarchive::partPath(output,0)))return fail("Archive destination already exists");
                index.open(partial,std::ios::binary|std::ios::trunc);if(!index)return fail("Archive index open failed");
                auto w=detarchive::header(0,0);w.blob(last.manifest);if(!appendIndex(std::move(w)))return false;
            }
            return openPart();
        }catch(const std::exception& e){return fail(e.what());}
    }
};
DetRollbackReplayWriter::DetRollbackReplayWriter():_impl(std::make_unique<Impl>()){}
DetRollbackReplayWriter::~DetRollbackReplayWriter()=default;
bool DetRollbackReplayWriter::begin(const DeterministicRollbackNetwork& net,std::string base,std::uint32_t interval) {
    return _impl->start(net,std::move(base),interval,std::nullopt);
}
bool DetRollbackReplayWriter::begin(const DeterministicRollbackNetwork& net,std::string base,DetRollbackReplayArchiveOptions options,std::uint32_t interval) {
    return _impl->start(net,std::move(base),interval,options);
}
bool DetRollbackReplayWriter::sync(const DeterministicRollbackNetwork& net) {
    auto& p=*_impl;if(!p.file || p.failed || p.finished)return false;
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
            auto journal=net.recoveryJournal();std::erase_if(journal,[&](const auto& e){return e.tick<p.next || e.tick>=start;});
            auto w=recordHeader(2,net.config().epoch);w.u64(p.next);w.blob(encodeDetCheckpoint(*cp));w.u32(static_cast<std::uint32_t>(journal.size()));
            for(const auto& e:journal){w.u32(e.epoch);w.u64(e.tick);command(w,e.command);}
            if(!p.write(std::move(w),start) || (p.archive && !p.appendEpoch()))return false;
            p.logical={net.config().epoch,start,start,start-p.next};p.next=start;p.epoch=net.config().epoch;p.last=std::move(*cp);
        }
        const auto end=net.verifiedNextTick();if(end<p.next)return p.fail("Recording confirmation frontier rewound");
        while(p.next<end) {
            const auto input=net.history().confirmedInputAt(p.next);auto cp=net.history().checkpointAt(p.next+1);
            if(!input || !cp)return p.fail("Recording missed retained confirmed history; sync every mutation");
            auto w=recordHeader(1,p.epoch);w.u32(cp->nextTick%p.interval==0?1:0);w.blob(encodeDetInput(*input));w.blob(encodeDetCheckpoint(*cp));
            if(!p.write(std::move(w),cp->nextTick))return false;
            p.next=cp->nextTick;p.logical.endTick=p.next;p.last=std::move(*cp);
        }
        p.error.clear();return true;
    }catch(const std::exception& e){return p.fail(e.what());}
}
bool DetRollbackReplayWriter::finish(const DeterministicRollbackNetwork& net) {
    auto& p=*_impl;if(!sync(net))return false;
    try {
        const auto head=net.diagnostics().head;if(!p.closePart(head))return false;
        if(p.archive) {
            if(!p.appendEpoch())return false;
            auto w=detarchive::header(3,p.indexChain);w.u32(p.parts);w.u32(p.epochCount);w.u32(p.epoch);w.u64(p.next);w.u64(head);w.u64(detCheckpointHash(p.last));
            if(!p.appendIndex(std::move(w)))return false;p.index.close();if(!p.index)return p.fail("Archive index close failed");
            if(std::filesystem::exists(p.output))return p.fail("Archive destination appeared before publication");
            std::filesystem::rename(p.partial,p.output);
        }
        p.finished=true;return true;
    }catch(const std::exception& e){return p.fail(e.what());}
}
std::string DetRollbackReplayWriter::path() const{return _impl->output;}
std::uint64_t DetRollbackReplayWriter::recordedNextTick() const{return _impl->next;}
std::uint32_t DetRollbackReplayWriter::fileSegmentCount() const{return _impl->parts+(_impl->file?1u:0u);}
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
    std::string archivePath;
    std::vector<std::uint8_t> archiveManifest;
    std::vector<DetReplayFileSegment> files;
    std::unique_ptr<DetRollbackReplayReader> child;
    std::size_t fileIndex=0;
    std::unique_ptr<DetRollbackReplayReader> load(std::size_t index) const {
        const auto& meta=files.at(index);detarchive::checkChildPath(archivePath,meta.path);
        if(detarchive::hashFile(meta.path,meta.storedBytes)!=meta.fileHash || detarchive::looksLikeIndex(meta.path))
            throw std::runtime_error("Archive segment hash/type mismatch");
        auto reader=std::make_unique<DetRollbackReplayReader>();
        if(!reader->open(meta.path))throw std::runtime_error(reader->error());
        const auto& data=*reader->_impl;
        if(data.entries.empty() || data.entries.size()!=meta.records || data.entries.front().state.manifest!=archiveManifest
            || data.entries.front().epoch!=meta.firstEpoch || data.entries.front().state.nextTick!=meta.firstTick
            || detCheckpointHash(data.entries.front().state)!=meta.initialHash || data.entries.back().epoch!=meta.lastEpoch
            || data.entries.back().state.nextTick!=meta.endTick || detCheckpointHash(data.entries.back().state)!=meta.finalHash
            || data.speculativeHead!=(index+1==files.size()?speculativeHead:meta.endTick))
            throw std::runtime_error("Archive segment/index boundary mismatch");
        auto global=std::lower_bound(segments.begin(),segments.end(),meta.firstEpoch,[](const auto& e,auto id){return e.epoch<id;});
        for(std::size_t i=0;i<data.segments.size();++i,++global) {
            const auto& local=data.segments[i];
            if(global==segments.end() || local.epoch!=global->epoch
                || local.firstTick!=(i?global->firstTick:meta.firstTick)
                || local.endTick!=(i+1==data.segments.size()?meta.endTick:global->endTick)
                || local.skippedTicks!=(i?global->skippedTicks:0))throw std::runtime_error("Archive logical epoch index mismatch");
        }
        if(data.segments.back().epoch!=meta.lastEpoch)throw std::runtime_error("Archive missing logical epoch");
        return reader;
    }
};
DetRollbackReplayReader::DetRollbackReplayReader():_impl(std::make_unique<Impl>()){}
DetRollbackReplayReader::~DetRollbackReplayReader()=default;
bool DetRollbackReplayReader::open(std::string path) {
    _impl=std::make_unique<Impl>();auto staged=std::make_unique<Impl>();auto& p=*staged;
    try {
        if(detarchive::looksLikeIndex(path)) {
            auto index=detarchive::readIndex(path);p.archivePath=std::move(path);p.archiveManifest=std::move(index.manifest);
            p.files=std::move(index.files);p.segments=std::move(index.epochs);p.speculativeHead=index.head;
            p.child=p.load(0);p.opened=true;p.epoch=p.segments.front().epoch;_impl=std::move(staged);return true;
        }
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
    auto& p=*_impl;
    if(p.child) {
        try {
            if(!p.opened || p.child->_impl->cursor==0)return p.fail("Archive replay not restored");
            p.difference.reset();
            while(p.child->atEnd()) {
                if(p.fileIndex+1>=p.files.size())return p.fail("Archive replay exhausted");
                auto next=p.load(p.fileIndex+1);const auto cp=session.checkpoint();
                if(!cp || !sameState(*cp,next->_impl->entries.front().state))return p.fail("Archive playback lost boundary ownership");
                // Adopt equal verified state without restoring World or emitting
                // the duplicated boundary checkpoint's pending presentation events.
                next->_impl->cursor=1;next->_impl->epoch=next->_impl->entries.front().epoch;
                p.child=std::move(next);++p.fileIndex;
            }
            const bool ok=p.child->advance(session);p.difference=p.child->difference();
            if(!ok)return p.fail(p.child->error());
            auto effects=p.child->takeConfirmedEvents();p.effects.insert(p.effects.end(),effects.begin(),effects.end());
            p.epoch=p.child->epoch();p.error.clear();return true;
        }catch(const std::exception& e){return p.fail(e.what());}
    }
    if(!p.opened || p.cursor==0 || p.cursor>=p.entries.size())return p.fail("Rollback replay exhausted/not restored");
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
    if(p.child) {
        if(!p.opened || session.manifest()!=p.archiveManifest)return p.fail("Archive replay not open/manifest mismatch");
        const auto logical=std::lower_bound(p.segments.begin(),p.segments.end(),epoch,[](const auto& e,auto id){return e.epoch<id;});
        if(logical==p.segments.end() || logical->epoch!=epoch || tick<logical->firstTick || tick>logical->endTick)
            return p.fail("Seek outside recorded epoch or inside recovery gap");
        try {
            const auto key=std::pair(epoch,tick);
            auto upper=std::upper_bound(p.files.begin(),p.files.end(),key,[](const auto& k,const auto& file){return k<std::pair(file.firstEpoch,file.firstTick);});
            if(upper==p.files.begin())return p.fail("Archive target file not indexed");
            const auto index=static_cast<std::size_t>(std::prev(upper)-p.files.begin());
            if(std::pair(p.files[index].lastEpoch,p.files[index].endTick)<key)return p.fail("Archive target file range mismatch");
            auto next=p.load(index);p.difference.reset();
            if(!next->seek(session,epoch,tick)) {p.difference=next->difference();return p.fail(next->error());}
            p.child=std::move(next);p.fileIndex=index;p.epoch=epoch;p.effects.clear();p.error.clear();return true;
        }catch(const std::exception& e){return p.fail(e.what());}
    }
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
bool DetRollbackReplayReader::atEnd() const{return _impl->opened && (_impl->child?
    (_impl->fileIndex+1==_impl->files.size() && _impl->child->atEnd()):_impl->cursor==_impl->entries.size());}
std::uint64_t DetRollbackReplayReader::endTick() const{return !_impl->files.empty()?_impl->files.back().endTick:
    _impl->entries.empty()?0:_impl->entries.back().state.nextTick;}
std::uint64_t DetRollbackReplayReader::speculativeHeadAtSeal() const{return _impl->speculativeHead;}
std::uint32_t DetRollbackReplayReader::epoch() const{return _impl->epoch;}
const std::vector<DetReplaySegment>& DetRollbackReplayReader::segments() const{return _impl->segments;}
const std::vector<DetReplayFileSegment>& DetRollbackReplayReader::files() const{return _impl->files;}
std::vector<DetConfirmedEvent> DetRollbackReplayReader::takeConfirmedEvents(){std::vector<DetConfirmedEvent> out;out.swap(_impl->effects);return out;}
const std::string& DetRollbackReplayReader::error() const{return _impl->error;}
const std::optional<DetStateDifference>& DetRollbackReplayReader::difference() const{return _impl->difference;}
} // namespace ayt::entity
