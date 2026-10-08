// Included once after Reader::Impl: share the production parser and bounded file load.
#include <sstream>
#include <iomanip>
#include <iostream>

namespace ayt::entity {
namespace regression {
using Result=DetReplayRegressionResult;
void validate(DetReplayRegressionOptions o) {
    if(!o.maxRecords || o.maxRecords>10000000 || o.maxArtifactBytes<1024 || o.maxArtifactBytes>256ull*1024*1024)
        throw std::runtime_error("Regression record/artifact budget outside supported range");
}
std::string quote(const std::string& value) {
    std::ostringstream s;s<<'"';for(unsigned char c:value) {
        if(c=='"')s<<"\\\"";else if(c=='\\')s<<"\\\\";
        else if(c<32)s<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<unsigned(c)<<std::dec;
        else s<<c;
    }s<<'"';return s.str();
}
const char* name(Result r) {
    switch(r) {
    case Result::Verified:return "verified";case Result::VerifiedWithGaps:return "verified-with-gaps";
    case Result::Equal:return "equal";case Result::ManifestMismatch:return "manifest-mismatch";
    case Result::InitialStateMismatch:return "initial-state-mismatch";case Result::InputMismatch:return "input-mismatch";
    case Result::StateMismatch:return "state-mismatch";case Result::EventMismatch:return "event-mismatch";
    case Result::RecoveryMismatch:return "recovery-mismatch";case Result::RangeMismatch:return "range-mismatch";
    case Result::InvalidFile:return "invalid-file";case Result::ExecutionFailed:return "execution-failed";
    case Result::BudgetExceeded:return "budget-exceeded";
    }return "unknown";
}
DetReplayInputDifference inputDifference(const DetTickInput& a,const DetTickInput& b) {
    if(a.version!=b.version)return {0,0,"version",a.version,b.version,{}};
    std::size_t i=0,j=0;
    while(i<a.commands.size() || j<b.commands.size()) {
        const auto* x=i<a.commands.size()?&a.commands[i]:nullptr;const auto* y=j<b.commands.size()?&b.commands[j]:nullptr;
        if(!y || (x && commandKey(*x)<commandKey(*y)))return {x->source,x->sequence,"presence",1,0,{}};
        if(!x || commandKey(*y)<commandKey(*x))return {y->source,y->sequence,"presence",0,1,{}};
        if(x->type!=y->type)return {x->source,x->sequence,"type",x->type,y->type,{}};
        const auto size=std::min(x->payload.size(),y->payload.size());
        for(std::size_t n=0;n<size;++n)if(x->payload[n]!=y->payload[n])return {x->source,x->sequence,"payload",x->payload[n],y->payload[n],n};
        if(x->payload.size()!=y->payload.size())return {x->source,x->sequence,"payload-size",x->payload.size(),y->payload.size(),{}};
        ++i;++j;
    }
    return {0,0,"tick",a.tick,b.tick,{}};
}
}
struct DetReplayRegression::Cursor {
    using Entry=DetRollbackReplayReader::Impl::Entry;
    DetRollbackReplayReader reader;
    std::string source;
    std::size_t file=0,position=0;
    std::uint64_t beforeRecordHash=0;
    bool ended=false;
    explicit Cursor(const std::string& path):source(path) {
        if(!reader.open(path))throw detarchive::FileError(reader.issue()?reader.issue()->path:path,
            reader.issue()?reader.issue()->offset:std::nullopt,reader.error());
        beforeRecordHash=get().recordHash;
    }
    auto& data() const {return reader._impl->child?*reader._impl->child->_impl:*reader._impl;}
    const Entry& get() const {return data().entries.at(position);}
    DetReplayFileIssue issue() const {return {reader._impl->child?reader._impl->files.at(file).path:source,get().offset};}
    void next() {
        beforeRecordHash=get().recordHash;
        ++position;
        while(position>=data().entries.size()) {
            auto& p=*reader._impl;
            if(!p.child || file+1>=p.files.size()){ended=true;return;}
            auto next=p.load(file+1);
            if(!sameState(data().entries.back().state,next->_impl->entries.front().state))
                throw detarchive::FileError(p.files.at(file+1).path,std::nullopt,"Regression physical boundary state mismatch");
            p.child=std::move(next);++file;position=1; // Duplicate boundary is not a logical record.
        }
    }
    void locate(DetReplayRegressionReport& r,bool right=false) const {
        const auto& e=get();
        const auto hash=replay::fnv1a64(e.state.manifest.data(),e.state.manifest.size());
        if(right){r.rightRecordHash=e.recordHash;r.rightBeforeRecordHash=beforeRecordHash;r.rightRecordPresent=true;r.rightManifestHash=hash;return;}
        r.epoch=e.epoch;r.tick=e.input?e.input->tick:e.state.nextTick;r.nextTick=e.state.nextTick;
        r.leftRecordHash=e.recordHash;r.leftBeforeRecordHash=beforeRecordHash;r.leftRecordPresent=true;r.manifestHash=hash;r.issue=issue();
    }
    struct Case {Entry entry;DetSessionCheckpoint before;std::uint32_t beforeEpoch;bool initial;};
    static Case extract(const std::string& source,std::uint64_t ordinal,std::uint64_t hash,std::uint64_t beforeHash,DetReplayRegressionOptions o) {
        Cursor c(source);DetSessionCheckpoint before=c.get().state;auto epoch=c.get().epoch;
        for(std::uint64_t i=0;!c.ended && i<o.maxRecords;++i) {
            if(i==ordinal) {
                if(c.get().recordHash!=hash || c.beforeRecordHash!=beforeHash)throw std::runtime_error("Source record/context changed since regression report");
                return {c.get(),std::move(before),epoch,i==0};
            }
            before=c.get().state;epoch=c.get().epoch;c.next();
        }
        throw std::runtime_error("Reported regression record is unavailable/beyond budget");
    }
};
bool DetReplayRegressionReport::success() const {
    return result==regression::Result::Verified || result==regression::Result::VerifiedWithGaps || result==regression::Result::Equal;
}
DetReplayRegressionReport DetReplayRegression::verify(const std::string& path,const DetReplaySessionFactory& factory,DetReplayRegressionOptions options) {
    using namespace regression;DetReplayRegressionReport r;r.left=path;
    try {
        validate(options);Cursor c(path);r.leftRecovery=c.reader.recovery();c.locate(r);
        if(!factory) {r.result=Result::ExecutionFailed;r.error="Application Session factory is required";return r;}
        auto session=factory();
        if(!session) {r.result=Result::ExecutionFailed;r.error="Application Session factory returned null";return r;}
        if(session->manifest()!=c.get().state.manifest) {r.result=Result::ManifestMismatch;r.error="Application/recorded manifest mismatch";return r;}
        std::uint64_t previous=c.get().state.nextTick;
        for(;!c.ended;c.next()) {
            if(r.recordsChecked==options.maxRecords){r.result=Result::BudgetExceeded;r.error="Global logical record budget";return r;}
            c.locate(r);r.record=r.recordsChecked++;
            const auto& e=c.get();
            bool ok;
            if(e.input) {ok=session->advance(*e.input);++r.ticksExecuted;}
            else {ok=session->restore(e.state);if(r.record)r.skippedTicks+=e.state.nextTick-previous;}
            if(!ok) {r.result=Result::ExecutionFailed;r.error=session->error();return r;}
            const auto actual=session->checkpoint();
            if(!actual) {r.result=Result::ExecutionFailed;r.error=session->error();return r;}
            r.difference=firstDetDifference(e.state,*actual);
            if(r.difference) {
                r.actualCheckpoint=encodeDetCheckpoint(*actual);
                r.result=r.difference->section=="events"?Result::EventMismatch:Result::StateMismatch;
                r.error="Executed state differs from recorded witness";return r;
            }
            if(e.input)r.eventsChecked+=e.state.pendingEvents.size();else r.trustedRecoveryEvents+=e.repair.size();previous=e.state.nextTick;
        }
        r.result=r.skippedTicks?Result::VerifiedWithGaps:Result::Verified;r.issue.reset();
    }catch(const detarchive::FileError& e){r.result=Result::InvalidFile;r.error=e.what();r.issue=e.issue;}
    catch(const std::exception& e){r.result=Result::ExecutionFailed;r.error=e.what();}
    return r;
}
DetReplayRegressionReport DetReplayRegression::compare(const std::string& left,const std::string& right,DetReplayRegressionOptions options) {
    using namespace regression;DetReplayRegressionReport r;r.left=left;r.right=right;
    try {
        validate(options);Cursor a(left),b(right);r.leftRecovery=a.reader.recovery();r.rightRecovery=b.reader.recovery();
        std::uint64_t previous=a.get().state.nextTick;
        for(;;) {
            if(a.ended && b.ended){r.result=Result::Equal;r.issue.reset();return r;}
            if(r.recordsChecked==options.maxRecords){r.result=Result::BudgetExceeded;r.error="Global logical record budget";return r;}
            if(a.ended || b.ended) {
                r.record=r.recordsChecked;r.leftRecordHash=r.rightRecordHash=0;r.leftRecordPresent=r.rightRecordPresent=false;
                if(!a.ended)a.locate(r);if(!b.ended){b.locate(r,true);if(a.ended){r.epoch=b.get().epoch;r.tick=b.get().input?b.get().input->tick:b.get().state.nextTick;r.nextTick=b.get().state.nextTick;r.issue=b.issue();}}
                r.result=Result::RangeMismatch;r.error="Different recorded ranges; one source ended";return r;
            }
            a.locate(r);b.locate(r,true);r.record=r.recordsChecked++;
            const auto& x=a.get();const auto& y=b.get();
            auto stop=[&](Result result,const char* message){r.result=result;r.error=message;};
            if(x.state.manifest!=y.state.manifest){stop(Result::ManifestMismatch,"Recorded manifests differ");return r;}
            if(x.epoch!=y.epoch || x.state.nextTick!=y.state.nextTick || x.input.has_value()!=y.input.has_value()) {
                stop(r.record && (!x.input || !y.input)?Result::RecoveryMismatch:Result::RangeMismatch,"Epoch/tick/record-kind boundaries differ");return r;
            }
            if(x.input!=y.input){r.inputDifference=regression::inputDifference(*x.input,*y.input);stop(Result::InputMismatch,"Canonical inputs differ before state comparison");return r;}
            if(x.repair!=y.repair){stop(Result::EventMismatch,"Recovery event identities/payloads differ");return r;}
            r.difference=firstDetDifference(x.state,y.state);
            if(r.difference){stop(!r.record?Result::InitialStateMismatch:r.difference->section=="events"?Result::EventMismatch:Result::StateMismatch,"Recorded checkpoint fields differ");return r;}
            r.eventsChecked+=x.input?x.state.pendingEvents.size():x.repair.size();
            if(r.record && !x.input)r.skippedTicks+=x.state.nextTick-previous;previous=x.state.nextTick;a.next();b.next();
        }
    }catch(const detarchive::FileError& e){r.result=Result::InvalidFile;r.error=e.what();r.issue=e.issue;}
    catch(const std::exception& e){r.result=Result::InvalidFile;r.error=e.what();}
    return r;
}
std::string DetReplayRegression::toJson(const DetReplayRegressionReport& r) {
    using regression::quote;std::ostringstream s;
    s<<"{\"schema\":1,\"success\":"<<(r.success()?"true":"false")<<",\"result\":"<<quote(regression::name(r.result))
        <<",\"left\":"<<quote(r.left)<<",\"right\":"<<quote(r.right)<<",\"error\":"<<quote(r.error)
        <<",\"epoch\":"<<r.epoch<<",\"tick\":"<<r.tick<<",\"nextTick\":"<<r.nextTick<<",\"record\":"<<r.record
        <<",\"recordsChecked\":"<<r.recordsChecked<<",\"ticksExecuted\":"<<r.ticksExecuted<<",\"eventsChecked\":"<<r.eventsChecked
        <<",\"skippedTicks\":"<<r.skippedTicks<<",\"trustedRecoveryEvents\":"<<r.trustedRecoveryEvents<<",\"manifestHash\":"<<quote(std::to_string(r.manifestHash))
        <<",\"rightManifestHash\":"<<quote(std::to_string(r.rightManifestHash))
        <<",\"leftRecordHash\":"<<quote(std::to_string(r.leftRecordHash))<<",\"rightRecordHash\":"<<quote(std::to_string(r.rightRecordHash))
        <<",\"leftBeforeRecordHash\":"<<quote(std::to_string(r.leftBeforeRecordHash))<<",\"rightBeforeRecordHash\":"<<quote(std::to_string(r.rightBeforeRecordHash))
        <<",\"leftRecordPresent\":"<<(r.leftRecordPresent?"true":"false")<<",\"rightRecordPresent\":"<<(r.rightRecordPresent?"true":"false")
        <<",\"difference\":";
    if(r.difference){const auto& d=*r.difference;s<<"{\"tick\":"<<d.tick<<",\"entity\":"<<quote(std::to_string(d.entity))
        <<",\"component\":"<<d.component<<",\"field\":"<<d.field<<",\"lane\":"<<d.lane<<",\"section\":"<<quote(d.section)
        <<",\"expected\":"<<quote(std::to_string(d.expected))<<",\"actual\":"<<quote(std::to_string(d.actual))<<'}';}else s<<"null";
    s<<",\"inputDifference\":";if(r.inputDifference){const auto& d=*r.inputDifference;s<<"{\"source\":"<<d.source<<",\"sequence\":"<<d.sequence<<",\"field\":"<<quote(d.field)
        <<",\"expected\":"<<quote(std::to_string(d.expected))<<",\"actual\":"<<quote(std::to_string(d.actual))<<",\"payloadOffset\":";if(d.payloadOffset)s<<*d.payloadOffset;else s<<"null";s<<'}';}else s<<"null";
    s<<",\"issue\":";if(r.issue){s<<"{\"path\":"<<quote(r.issue->path)<<",\"offset\":";if(r.issue->offset)s<<*r.issue->offset;else s<<"null";s<<'}';}else s<<"null";
    auto recovery=[&](const char* name,const auto& value){s<<",\""<<name<<"\":";if(value)s<<"{\"reason\":"<<static_cast<unsigned>(value->reason)<<",\"stopOrdinal\":"<<value->stopOrdinal<<'}';else s<<"null";};
    recovery("leftRecovery",r.leftRecovery);recovery("rightRecovery",r.rightRecovery);s<<'}';return s.str();
}
bool DetReplayRegression::writeArtifacts(const DetReplayRegressionReport& report,const std::string& directory,std::string& error,DetReplayRegressionOptions options) {
    try {
        regression::validate(options);
        std::optional<Cursor::Case> left,right;
        const bool context=!report.success() && report.result!=regression::Result::InvalidFile && report.result!=regression::Result::BudgetExceeded;
        if(context && report.leftRecordPresent)left=Cursor::extract(report.left,report.record,report.leftRecordHash,report.leftBeforeRecordHash,options);
        if(context && report.rightRecordPresent)right=Cursor::extract(report.right,report.record,report.rightRecordHash,report.rightBeforeRecordHash,options);
        if(directory.empty() || !std::filesystem::create_directory(directory))throw std::runtime_error("Regression artifacts require a new directory");
        std::uint64_t bytes=0;
        auto charge=[&](std::uint64_t count){if(count>options.maxArtifactBytes-bytes)throw std::runtime_error("Regression artifact byte budget");bytes+=count;};
        auto save=[&](const std::string& name,std::span<const std::uint8_t> data){charge(data.size());
            std::ofstream out(std::filesystem::path(directory)/name,std::ios::binary);out.write(reinterpret_cast<const char*>(data.data()),data.size());out.close();if(!out)throw std::runtime_error("Regression artifact write/close failed: "+name);};
        auto saveCase=[&](const Cursor::Case& c,const char* label){
            const auto base=(std::filesystem::path(directory)/(std::string(label)+".rpl")).string();
            replay::FileReplayRecorder file(base,fileLimit,UINT32_MAX);replay::ReplayFileHeader h{};
            h.magic=replay::kReplayMagic;h.version=replay::kReplayVersion;h.schemaVersion=2;std::memcpy(h.sceneName,"DetRepro",8);
            charge(sizeof(h)+2*sizeof(replay::ReplayEventHeader));if(!file.beginSession(h))throw std::runtime_error("Repro file open failed");
            auto emit=[&](Writer w,std::uint64_t tick){const auto payload=w.finish();charge(payload.size()+sizeof(replay::ReplayEventHeader));
                if(!file.recordEvent(tick,recordEvent,payload.data(),payload.size()) || file.rotationIndex())throw std::runtime_error("Repro record write failed");};
            auto initial=recordHeader(0,c.beforeEpoch);initial.blob(encodeDetCheckpoint(c.before));emit(std::move(initial),c.before.nextTick);
            const auto& e=c.entry;
            if(!c.initial){auto w=recordHeader(e.input?1:2,e.epoch);
                if(e.input){w.u32(1);w.blob(encodeDetInput(*e.input));}else w.u64(c.before.nextTick);
                w.blob(encodeDetCheckpoint(e.state));if(!e.input){w.u32(static_cast<unsigned>(e.repair.size()));for(const auto& event:e.repair){w.u32(event.epoch);w.u64(event.tick);command(w,event.command);}}
                emit(std::move(w),e.state.nextTick);
            }
            auto seal=recordHeader(3,e.epoch);seal.u64(e.state.nextTick);seal.u64(e.state.nextTick);seal.u64(detCheckpointHash(e.state));seal.u32(c.initial?1:2);emit(std::move(seal),e.state.nextTick);
            if(!file.flush() || !file.endSession())throw std::runtime_error("Repro seal/close failed");
            DetRollbackReplayReader checked;if(!checked.open(replay::FileReplayRecorder::rotationPathFor(base,0)))throw std::runtime_error(checked.error());
        };
        if(left){saveCase(*left,"left");save("manifest.bin",left->entry.state.manifest);}
        if(right){saveCase(*right,"right");if(!left || right->entry.state.manifest!=left->entry.state.manifest)save("right-manifest.bin",right->entry.state.manifest);}
        if(!report.actualCheckpoint.empty()){
            DetSessionCheckpoint actual;std::string why;
            if(!decodeDetCheckpoint(report.actualCheckpoint,actual,why) || encodeDetCheckpoint(actual)!=report.actualCheckpoint)throw std::runtime_error("Invalid actual checkpoint in regression report");
            save("actual.checkpoint",report.actualCheckpoint);
        }
        const auto json=toJson(report);save("report.json.partial",{reinterpret_cast<const std::uint8_t*>(json.data()),json.size()});
        std::filesystem::rename(std::filesystem::path(directory)/"report.json.partial",std::filesystem::path(directory)/"report.json");error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
int DetReplayRegression::runnerMain(int argc,char** argv,const DetReplaySessionFactory& factory,DetReplayRegressionOptions options) {
    if(argc!=4 || std::string(argv[1])!="--ayreplay-verify") {std::cerr<<"usage: --ayreplay-verify SOURCE NEW_DIRECTORY\n";return 2;}
    const auto report=verify(argv[2],factory,options);std::string error;
    if(!writeArtifacts(report,argv[3],error,options)){std::cerr<<error<<'\n';std::cout<<toJson(report)<<'\n';return 3;}
    std::cout<<toJson(report)<<'\n';return report.success()?0:1;
}
} // namespace ayt::entity
