#include <AYEntity/DeterministicRollback.h>
#include <algorithm>
#include <deque>
#include <limits>
#include <stdexcept>

namespace ayt::entity {
namespace {
DetRollbackConfig validated(DetRollbackConfig c) {
    std::sort(c.members.begin(),c.members.end());
    std::sort(c.prediction.begin(),c.prediction.end(),[](const auto& a,const auto& b){return a.type<b.type;});
    if(c.members.empty() || c.members.size()>8 || !c.members.front()
        || std::adjacent_find(c.members.begin(),c.members.end())!=c.members.end()
        || c.historyTicks<2 || c.historyTicks>256 || c.maxPredictionTicks>c.historyTicks-1
        || c.futureTicks>256 || c.maxBufferedBytes<1024 || c.maxBufferedBytes>256ull*1024*1024
        || c.prediction.size()>256)throw std::invalid_argument("Invalid rollback bounds/roster");
    std::uint32_t last=0;
    for(const auto& r:c.prediction) {
        if(!r.type || r.type==last || r.mode>DetPredictionMode::Zero)throw std::invalid_argument("Invalid prediction rule");
        last=r.type;
    }
    return c;
}
std::uint64_t commandBytes(const DetTickCommand& c) {return 16+c.payload.size();}
}
struct DeterministicRollback::Impl {
    struct Snapshot { DetSessionCheckpoint state; std::uint64_t bytes=0; };
    DeterministicSession& session;
    DetRollbackConfig cfg;
    std::uint32_t epoch;
    std::uint64_t initial=0, confirmed=0, rollbacks=0, replayed=0;
    std::uint64_t lastDepth=0, maxDepth=0, predicted=0;
    std::map<std::uint64_t,Snapshot> snapshots;
    std::map<std::uint64_t,std::map<std::uint32_t,DetTickInput>> actual;
    std::map<std::uint32_t,DetTickInput> seed;
    std::map<std::uint64_t,DetTickInput> used;
    std::deque<DetConfirmedEvent> effects;
    bool failed=false, busy=false;
    std::string error;
    Impl(DeterministicSession& s,DetRollbackConfig c,std::uint32_t e):session(s),cfg(validated(std::move(c))),epoch(e) {
        const auto cp=session.checkpoint();
        if(!e || !session.sealed() || !cp)throw std::invalid_argument("Rollback needs a sealed valid Session/epoch");
        initial=confirmed=cp->nextTick;
        inputVersion=session.inputVersion();
        snapshots.emplace(initial,Snapshot{*cp,encodeDetCheckpoint(*cp).size()});
        if(bytes()>cfg.maxBufferedBytes)throw std::invalid_argument("Initial checkpoint exceeds rollback budget");
    }
    bool reject(std::string text,bool fault=false) {error=std::move(text);failed|=fault;return false;}
    bool member(std::uint32_t id) const {return std::binary_search(cfg.members.begin(),cfg.members.end(),id);}
    std::uint32_t version() const {
        return inputVersion;
    }
    std::uint32_t inputVersion=0;
    bool has(std::uint32_t id,std::uint64_t tick) const {
        auto f=actual.find(tick);return f!=actual.end() && f->second.contains(id);
    }
    DetTickInput prediction(std::uint32_t id,std::uint64_t tick) const {
        DetTickInput result{tick,version(),{}};
        const DetTickInput* previous=nullptr;
        auto f=actual.lower_bound(tick);
        while(f!=actual.begin()) {--f;auto p=f->second.find(id);if(p!=f->second.end()){previous=&p->second;break;}}
        if(!previous) {auto p=seed.find(id);if(p!=seed.end() && p->second.tick<tick)previous=&p->second;}
        if(previous)for(auto c:previous->commands) {
            auto rule=std::lower_bound(cfg.prediction.begin(),cfg.prediction.end(),c.type,
                [](const auto& r,std::uint32_t type){return r.type<type;});
            if(rule==cfg.prediction.end() || rule->type!=c.type || rule->mode==DetPredictionMode::Omit)continue;
            if(rule->mode==DetPredictionMode::Zero)std::fill(c.payload.begin(),c.payload.end(),0);
            result.commands.push_back(std::move(c));
        }
        return result;
    }
    DetTickInput merged(std::uint64_t tick) const {
        DetTickInput result{tick,version(),{}};
        for(auto id:cfg.members) {
            auto f=actual.find(tick);
            auto part=f!=actual.end() && f->second.contains(id)?f->second.at(id):prediction(id,tick);
            result.commands.insert(result.commands.end(),part.commands.begin(),part.commands.end());
        }
        std::string diagnostic;
        if(!canonicalizeDetInput(result,diagnostic))throw std::invalid_argument(diagnostic);
        return result;
    }
    std::uint64_t bytes() const {
        std::uint64_t n=0;
        for(const auto& [tick,s]:snapshots)n+=s.bytes;
        for(const auto& [tick,f]:actual)for(const auto& [id,in]:f) {n+=32;for(const auto& c:in.commands)n+=commandBytes(c);}
        for(const auto& [id,in]:seed){n+=32;for(const auto& c:in.commands)n+=commandBytes(c);}
        for(const auto& [tick,in]:used){n+=32;for(const auto& c:in.commands)n+=commandBytes(c);}
        for(const auto& e:effects)n+=12+commandBytes(e.command);
        return n;
    }
    void prune() {
        const auto head=session.nextTick();
        const auto floor=std::min(confirmed,head>cfg.historyTicks?head-cfg.historyTicks:0);
        while(snapshots.size()>1 && snapshots.begin()->first<floor)snapshots.erase(snapshots.begin());
        while(!used.empty() && used.begin()->first<floor)used.erase(used.begin());
        while(!actual.empty() && actual.begin()->first<floor) {
            for(auto& [id,input]:actual.begin()->second)seed[id]=std::move(input);
            actual.erase(actual.begin());
        }
    }
    void confirm() {
        prune();
        auto end=confirmed;
        std::uint64_t extra=0;
        while(end<session.nextTick()) {
            auto it=actual.find(end);
            if(it==actual.end() || it->second.size()!=cfg.members.size())break;
            for(const auto& c:snapshots.at(end+1).state.pendingEvents)extra+=12+commandBytes(c);
            ++end;
        }
        if(bytes()+extra>cfg.maxBufferedBytes)throw std::length_error("Rollback history/effect budget exhausted; drain confirmed effects");
        while(confirmed<end) {
            for(const auto& c:snapshots.at(confirmed+1).state.pendingEvents)effects.push_back({epoch,confirmed,c});
            ++confirmed;
        }
    }
    bool ownsBoundary() {
        const auto cp=session.checkpoint();
        auto it=snapshots.find(session.nextTick());
        return cp && it!=snapshots.end() && !firstDetDifference(*cp,it->second.state);
    }
    bool replay(std::uint64_t tick) {
        const auto head=session.nextTick();
        auto start=snapshots.find(tick);
        if(start==snapshots.end() || tick>head)return reject("Replay outside retained checkpoint window");
        if(!ownsBoundary())return reject("Rollback Session changed outside its owner",true);
        const auto before=snapshots.at(head).state;
        try {
            if(!session.restore(start->second.state))throw std::runtime_error(session.error());
            std::map<std::uint64_t,Snapshot> staged;
            std::map<std::uint64_t,DetTickInput> inputs;
            for(auto t=tick;t<head;++t) {
                auto in=merged(t);
                if(!session.advance(in))throw std::runtime_error(session.error());
                auto cp=session.checkpoint();if(!cp)throw std::runtime_error(session.error());
                staged.emplace(t+1,Snapshot{*cp,encodeDetCheckpoint(*cp).size()});
                inputs.emplace(t,std::move(in));
            }
            for(auto& [t,s]:staged)snapshots.insert_or_assign(t,std::move(s));
            for(auto& [t,in]:inputs)used.insert_or_assign(t,std::move(in));
            if(tick<head){++rollbacks;lastDepth=head-tick;maxDepth=std::max(maxDepth,lastDepth);replayed+=lastDepth;}
            confirm();error.clear();return true;
        } catch(const std::exception& e) {
            const std::string diagnostic=e.what();
            if(!session.restore(before))return reject(diagnostic+"; rollback restoration failed: "+session.error(),true);
            return reject(diagnostic,true);
        }
    }
};
DeterministicRollback::DeterministicRollback(DeterministicSession& s,DetRollbackConfig c,std::uint32_t e)
    :_impl(std::make_unique<Impl>(s,std::move(c),e)) {}
DeterministicRollback::~DeterministicRollback()=default;
bool DeterministicRollback::submit(std::uint32_t id,DetTickInput in) {
    auto& p=*_impl;
    if(p.failed || p.busy)return p.reject("Rollback fault/reentry");
    if(!p.member(id))return p.reject("Unknown rollback member");
    std::string diagnostic;
    if(!canonicalizeDetInput(in,diagnostic))return p.reject(diagnostic);
    for(const auto& c:in.commands)if(c.source!=id)return p.reject("Input source differs from admitted member");
    if(!in.version || (p.version() && in.version!=p.version()))return p.reject("Rollback input version mismatch");
    if(in.tick<p.snapshots.begin()->first || (in.tick>p.session.nextTick() && in.tick-p.session.nextTick()>p.cfg.futureTicks))
        return p.reject("Input outside rollback window");
    auto f=p.actual.find(in.tick);
    if(f!=p.actual.end() && f->second.contains(id)) {
        if(f->second.at(id)==in){p.error.clear();return true;}
        return p.reject("Conflicting actual input",true);
    }
    if(in.tick<p.confirmed)return p.reject("Cannot alter confirmed input",true);
    const auto size=encodeDetInput(in).size();
    if(size>p.cfg.maxBufferedBytes-p.bytes())return p.reject("Rollback input buffer full");
    if(!p.ownsBoundary())return p.reject("Rollback Session changed outside its owner",true);
    struct Guard {bool& b;~Guard(){b=false;}} guard{p.busy};p.busy=true;
    const auto tick=in.tick;
    try {
        p.inputVersion=in.version;p.actual[tick].emplace(id,std::move(in));
        if(tick<p.session.nextTick()) {
            // Even a matching late frame changes the predictor seed for later ticks.
            std::optional<std::uint64_t> changed;
            for(auto t=tick;t<p.session.nextTick();++t)if(p.merged(t)!=p.used.at(t)){changed=t;break;}
            if(changed)return p.replay(*changed);
        }
        p.confirm();p.error.clear();return true;
    }catch(const std::exception& e){return p.reject(e.what(),true);}
}
bool DeterministicRollback::ready() const {
    const auto& p=*_impl;if(p.failed || p.busy || !p.version() || p.session.nextTick()==UINT64_MAX)return false;
    bool all=true;for(auto id:p.cfg.members)all&=p.has(id,p.session.nextTick());
    return (all && p.session.nextTick()==p.confirmed) || p.session.nextTick()-p.confirmed<p.cfg.maxPredictionTicks;
}
bool DeterministicRollback::advance() {
    auto& p=*_impl;if(!ready())return false;
    if(!p.ownsBoundary())return p.reject("Rollback Session changed outside its owner",true);
    struct Guard {bool& b;~Guard(){b=false;}} guard{p.busy};p.busy=true;
    const auto tick=p.session.nextTick();const auto before=p.snapshots.at(tick).state;
    try {
        auto in=p.merged(tick);
        if(!p.session.advance(in))throw std::runtime_error(p.session.error());
        auto cp=p.session.checkpoint();if(!cp)throw std::runtime_error(p.session.error());
        p.snapshots.emplace(tick+1,Impl::Snapshot{*cp,encodeDetCheckpoint(*cp).size()});
        p.used.emplace(tick,std::move(in));p.confirm();if(p.confirmed<=tick)++p.predicted;p.error.clear();return true;
    }catch(const std::exception& e){const std::string diagnostic=e.what();
        p.snapshots.erase(tick+1);p.used.erase(tick);
        if(!p.session.restore(before))return p.reject(diagnostic+"; restore failed: "+p.session.error(),true);
        return p.reject(diagnostic,true);}
}
bool DeterministicRollback::replayFrom(std::uint64_t tick) {
    auto& p=*_impl;if(p.failed || p.busy)return false;
    struct Guard{bool& b;~Guard(){b=false;}}guard{p.busy};p.busy=true;
    return p.replay(tick);
}
std::uint64_t DeterministicRollback::confirmedNextTick() const{return _impl->confirmed;}
std::uint64_t DeterministicRollback::oldestTick() const{return _impl->snapshots.begin()->first;}
std::uint64_t DeterministicRollback::rollbackCount() const{return _impl->rollbacks;}
std::uint64_t DeterministicRollback::replayedTicks() const{return _impl->replayed;}
bool DeterministicRollback::hasInput(std::uint32_t id,std::uint64_t tick) const{return _impl->has(id,tick);}
std::vector<DetTickInput> DeterministicRollback::actualInputs(std::uint32_t id) const {
    std::vector<DetTickInput> out;for(const auto& [tick,f]:_impl->actual)if(f.contains(id))out.push_back(f.at(id));return out;
}
std::optional<DetSessionCheckpoint> DeterministicRollback::checkpointAt(std::uint64_t tick) const {
    auto it=_impl->snapshots.find(tick);if(it==_impl->snapshots.end())return std::nullopt;return it->second.state;
}
std::optional<DetTickInput> DeterministicRollback::confirmedInputAt(std::uint64_t tick) const {
    const auto& p=*_impl;if(tick>=p.confirmed || tick<p.snapshots.begin()->first)return std::nullopt;
    for(auto id:p.cfg.members)if(!p.has(id,tick))return std::nullopt;
    return p.merged(tick);
}
DetRollbackDiagnostics DeterministicRollback::diagnostics() const {
    const auto& p=*_impl;return {p.epoch,p.session.nextTick(),p.confirmed,p.confirmed,
        p.snapshots.begin()->first,p.rollbacks,p.replayed,p.lastDepth,p.maxDepth,p.predicted,p.bytes()};
}
std::vector<DetConfirmedEvent> DeterministicRollback::takeConfirmedEvents(std::uint64_t through) {
    auto& p=*_impl;std::vector<DetConfirmedEvent> out;if(p.busy || p.failed)return out;
    for(const auto& e:p.effects){if(e.tick>=through)break;out.push_back(e);}
    for(std::size_t i=0;i<out.size();++i)p.effects.pop_front();return out;
}
bool DeterministicRollback::faulted() const{return _impl->failed;}
const std::string& DeterministicRollback::error() const{return _impl->error;}
const DetRollbackConfig& DeterministicRollback::config() const{return _impl->cfg;}
} // namespace ayt::entity
