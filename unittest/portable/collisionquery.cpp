// design reference: AYEntity/design.md Stage20; immutable scene queries and integer-grid oracles.
#include <AYEntity/DeterministicCollisionQuery2D.h>
#include <AYEntity/DeterministicRollback.h>
#include <AYReplay/ReplayHash.h>
#include <algorithm>
#include <bit>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <tuple>
using namespace ayt::entity;
namespace {
using D=ayt::math::DetFloat32;using V=ayt::math::DetVec2;using Box=ayt::math::DetAabb2;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F> void rejects(F f,const char* message){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,message);}
V v(int x,int y){return {D::fromInt(x),D::fromInt(y)};}
Box box(int x,int y,int hx,int hy){return Box::fromCenterHalf(v(x,y),v(hx,hy));}
DetActorState actor(const DetCollision2DConfig& config,int x,int y,int hx=1,int hy=1,
    std::uint32_t layer=1,std::uint32_t mask=UINT32_MAX,bool trigger=false,int ox=0,int oy=0,DetBodyMode2D mode=DetBodyMode2D::Static) {
    DetActorState a;DetSimTransformComponent pose;check(pose.setPosition({D::fromInt(x),D::fromInt(y),D::fromInt(7)}),"test pose");a.pose=pose.snapshot();
    a.blocks[config.bodySchema]=detCollisionBodyState2D(config,{mode,v(hx,hy),v(ox,oy),{},layer,mask,trigger});return a;
}
void handChecks() {
    DetCollision2DConfig policy;policy.maxBodies=8;policy.maxTriggerPairs=8;
    DeterministicSession s;check(installDetCollision2D(s,policy),"install query collision");
    check(s.addEntity(30,actor(policy,5,0,1,1,1,UINT32_MAX,true)) && s.addEntity(10,actor(policy,4,0,1,1,1,UINT32_MAX,false,1)),"offset/trigger actors");
    check(s.addEntity(20,actor(policy,5,0,1,1,2,2,true)) && s.addEntity(60,actor(policy,-4,0)),"filtered/left actors");
    check(s.addEntity(40,actor(policy,0,5,1,1,1,UINT32_MAX,true)) && s.addEntity(50,actor(policy,0,0,0,0,1,UINT32_MAX,false,0,0,DetBodyMode2D::Disabled)),"upper/disabled actors");
    check(s.seal(),"seal query scene");const auto before=*s.checkpoint();const auto bytes=encodeDetCheckpoint(before);const DetCollisionQuery2D query(before,policy);
    check(query.size()==5,"disabled exclusion");
    check(query.overlap(box(4,0,0,0))==std::vector<SimEntityId>({10,30}),"closed point boundary and stable IDs");
    check(query.overlap(box(4,0,0,0),{},true).empty(),"strict overlap excludes degenerate/touching query");
    DetCollisionQueryFilter2D filter;filter.includeTriggers=false;
    check(query.overlap(box(5,0,1,1),filter)==std::vector<SimEntityId>({10}),"trigger filter");
    filter={};filter.layer=2;filter.mask=2;
    check(query.overlap(box(5,0,1,1),filter)==std::vector<SimEntityId>({20}),"reciprocal layer/mask");
    filter={};filter.ignore=10;check(query.overlap(box(5,0,1,1),filter)==std::vector<SimEntityId>({30}),"ignore stable ID");
    filter.mask=0;check(query.raycast(v(-10,0),v(20,0),filter).empty(),"zero mask is empty");
    const auto ray=query.raycast(v(-10,0),v(20,0));
    check(ray.size()==3 && ray[0].entity==60 && ray[1].entity==10 && ray[2].entity==30,"ray ordering");
    check(ray[0].hit.fraction.bits()==0x3e800000u && ray[1].hit.fraction.bits()==0x3f333333u && ray[0].hit.normal.x.bits()==0xbf800000u,"ray independent fractions/normals");
    const auto inside=query.raycast(v(5,0),{D::fromBits(0x80000000u),D{}});
    check(inside.size()==2 && inside[0].entity==10 && inside[0].hit.initialOverlap && inside[0].hit.normal.x.isZero() && inside[0].hit.fraction.isZero(),"inside signed-zero ray");
    const auto sweep=query.sweep(box(0,0,1,1),v(10,0));
    check(sweep.size()==2 && sweep[0].entity==10 && sweep[1].entity==30 && sweep[0].hit.fraction.bits()==0x3e99999au,"sweep independent fraction/tie");
    check(query.sweep(box(3,0,1,1),v(-1,0)).empty(),"outward touching sweep");
    const auto inwardPositive=query.sweep(box(3,0,1,1),{D::fromInt(1),D::fromBits(0x80000000u)});
    const auto inwardNegative=query.sweep(box(7,0,1,1),v(-1,0));
    check(inwardPositive.size()==2 && inwardPositive[0].hit.fraction.bits()==0 && inwardPositive[0].hit.normal.x.bits()==0xbf800000u,"positive inward face fraction +0");
    check(inwardNegative.size()==2 && inwardNegative[0].hit.fraction.bits()==0x80000000u && inwardNegative[0].hit.normal.x.bits()==0x3f800000u,"negative inward face fraction -0");
    const auto penetrating=query.sweep(box(5,0,1,1),{});check(penetrating.size()==2 && penetrating[0].hit.initialOverlap,"initial sweep penetration");
    check(encodeDetCheckpoint(*s.checkpoint())==bytes,"queries preserve registered bytes");
    rejects([&]{auto f=filter;f.layer=0;(void)query.overlap(box(0,0,1,1),f);},"zero layer rejected");
    rejects([&]{(void)query.raycast({D::fromBits(0x7f800000u),{}},{});},"nonfinite ray rejected");
    rejects([&]{(void)query.sweep(box(0,0,0,1),{});},"degenerate sweep rejected");
    rejects([&]{auto cfg=policy;++cfg.maxBodies;(void)DetCollisionQuery2D(before,cfg);},"policy mismatch rejected");
    rejects([&]{auto bad=before;bad.globals.erase(policy.historySchema);(void)DetCollisionQuery2D(bad,policy);},"missing policy rejected");
    rejects([&]{auto bad=before;bad.actors.at(50).blocks.at(policy.bodySchema)[1]=0x7f800000u;(void)DetCollisionQuery2D(bad,policy);},"disabled nonfinite body rejected before filtering");
    rejects([&]{auto bad=before;bad.actors.at(10).pose.position[0]=0x7f800000u;(void)DetCollisionQuery2D(bad,policy);},"invalid active pose rejected");
    rejects([&]{auto bad=before;bad.actors.at(10).pose.profileVersion=999;(void)DetCollisionQuery2D(bad,policy);},"pose numeric profile rejected");
    rejects([&]{auto bad=before;bad.actors.at(10).pose.snapshotVersion=999;(void)DetCollisionQuery2D(bad,policy);},"pose snapshot version rejected");
    const auto manifestChanged=[&](std::size_t offset,std::uint8_t value){auto bad=before;bad.manifest.at(offset)=value;
        const auto hash=ayt::replay::fnv1a64(bad.manifest.data(),bad.manifest.size()-8);
        for(unsigned i=0;i<8;++i)bad.manifest[bad.manifest.size()-8+i]=static_cast<std::uint8_t>(hash>>(8*i));return bad;};
    // This scene has body schema4 first: offset68=version,76=first field ID,80=type.
    rejects([&]{(void)DetCollisionQuery2D(manifestChanged(68,2),policy);},"manifest body schema version rejected");
    rejects([&]{(void)DetCollisionQuery2D(manifestChanged(76,11),policy);},"manifest body field identity rejected");
    rejects([&]{(void)DetCollisionQuery2D(manifestChanged(80,static_cast<std::uint8_t>(DetStateType::Int32)),policy);},"manifest body same-width wrong type rejected");
    rejects([&]{auto bad=before;bad.globals.at(policy.historySchema)[6]=10;(void)DetCollisionQuery2D(bad,policy);},"noncanonical unused history rejected");
    rejects([&]{auto cfg=policy;cfg.maxBodies=1;auto bad=before;bad.globals.at(cfg.historySchema)[1]=1;(void)DetCollisionQuery2D(bad,cfg);},"active body capacity rejected");
    // A very distant finite origin can overflow a coordinate difference; it is an error, not a miss.
    auto far=before;far.actors.clear();far.actors.emplace(10,actor(policy,0,0));
    far.actors.at(10).pose.position[0]=0x7f7fffffu;far.actors.at(10).blocks.at(policy.bodySchema)[1]=0x7effffffu;
    // Invalid represented bounds are rejected at capture, before filters can hide them.
    rejects([&]{(void)DetCollisionQuery2D(far,policy);},"represented bounds overflow rejected");
    auto distant=before;distant.actors.clear();distant.actors.emplace(10,actor(policy,0,0));
    distant.actors.at(10).pose.position[0]=0x7e800000u;distant.actors.at(10).blocks.at(policy.bodySchema)[1]=0x72800000u;
    const DetCollisionQuery2D distantQuery(distant,policy);
    rejects([&]{(void)distantQuery.raycast({D::fromBits(0xff7fffffu),{}},v(1,0));},"query difference overflow is not a miss");
    DeterministicSession empty;check(installDetCollision2D(empty,policy) && empty.seal(),"empty scene");
    const DetCollisionQuery2D noBodies(*empty.checkpoint(),policy);check(noBodies.size()==0 && noBodies.overlap(box(0,0,1,1)).empty(),"empty complete result");
    rejects([&]{(void)noBodies.raycast({}, {D::fromBits(0x7fc00000u),{}});},"invalid query rejected even on empty scene");
}
void callbackChecks() {
    for(bool extended:{false,true}) {
        DetCollision2DConfig policy;policy.extended=extended;policy.maxBodies=4;policy.maxTriggerPairs=2;
        auto configure=[&](DeterministicSession& s,bool observe) {
            check(installDetCollision2D(s,policy),"callback collision installation");
            check(s.registerSystem(10,0,[policy](DetTickContext& c){c.write(10,policy.bodySchema,DetBodyVelocity,v(1,0));return true;}),"velocity system");
            check(s.registerSystem(20,0,[policy,observe](DetTickContext& c){if(observe){const DetCollisionQuery2D q(c,policy);check(q.overlap(box(0,0,1,1))==std::vector<SimEntityId>({10}),"pre-movement capture");}return true;}),"pre-query system");
            check(s.registerSystem(40,0,[policy,observe](DetTickContext& c){if(observe){const DetCollisionQuery2D q(c,policy);check(q.size()==1 && q.raycast(v(-2,0),v(4,0)).size()==1,"post-movement capture");}return true;}),"post-query system");
            check(s.addEntity(10,actor(policy,0,0,1,1,1,UINT32_MAX,false,0,0,DetBodyMode2D::Kinematic)) && s.seal(),"callback actor");
        };
        DeterministicSession a,b;configure(a,true);configure(b,false);const auto before=*a.checkpoint();const DetCollisionQuery2D old(before,policy);
        for(std::uint64_t tick=0;tick<16;++tick){check(a.advance({tick,1,{}}) && b.advance({tick,1,{}}),"callback query tick");check(encodeDetCheckpoint(*a.checkpoint())==encodeDetCheckpoint(*b.checkpoint()),"observation doesn't mutate Sim");}
        check(old.overlap(box(1,0,0,0))==std::vector<SimEntityId>({10}),"snapshot owns original values");
        check(a.restore(before),"query scene restore");const DetCollisionQuery2D restored(*a.checkpoint(),policy);
        check(restored.raycast(v(-2,0),v(4,0))[0].hit.fraction.bits()==0x3e800000u,"restore reacquisition");
    }
}
struct GridBody {SimEntityId id;int lx,ly,hx,hy;std::uint32_t layer,mask;bool trigger;};
struct Expected {SimEntityId id;int time,axis,normal;bool inside;};
std::uint32_t fractionBits(unsigned time) {if(!time)return 0;const unsigned high=31-std::countl_zero(time);return ((high+127-6)<<23)|((time<<(23-high))&0x7fffffu);}
bool match(const GridBody& b,const DetCollisionQueryFilter2D& f){return b.id!=f.ignore && (f.includeTriggers || !b.trigger) && (b.mask&f.layer) && (f.mask&b.layer);}
bool closed(const GridBody& b,int lx,int ly,int hx,int hy){return b.lx<=hx && b.hx>=lx && b.ly<=hy && b.hy>=ly;}
bool interior(const GridBody& b,int lx,int ly,int hx,int hy){return b.lx<hx && b.hx>lx && b.ly<hy && b.hy>ly && lx<hx && ly<hy;}
std::optional<Expected> rayOracle(const GridBody& b,int x,int y,int dx,int dy) {
    if(x>=b.lx && x<=b.hx && y>=b.ly && y<=b.hy)return Expected{b.id,0,0,0,true};
    int enter=0,exit=64,axis=0,normal=0;bool face=false;
    for(int a=0;a<2;++a){const int d=a?dy:dx,p=a?y:x,lo=a?b.ly:b.lx,hi=a?b.hy:b.hx;
        if(!d){if(p<lo || p>hi)return {};continue;}
        const int t=(d>0?lo-p:hi-p)*64/d,u=(d>0?hi-p:lo-p)*64/d;
        if(t>enter || (!face && t==enter)){enter=t;axis=a;normal=d>0?-1:1;face=true;}exit=std::min(exit,u);if(enter>exit)return {};
    }if(enter<0 || enter>64 || exit<0)return {};return Expected{b.id,enter,axis,normal,false};
}
std::optional<Expected> sweepOracle(const GridBody& b,int lx,int ly,int hx,int hy,int dx,int dy) {
    if(interior(b,lx,ly,hx,hy))return Expected{b.id,0,0,0,true};
    int enter=-100000,exit=100000,axis=0,normal=0;
    for(int a=0;a<2;++a){const int d=a?dy:dx,lo=a?ly:lx,hi=a?hy:hx,targetLo=a?b.ly:b.lx,targetHi=a?b.hy:b.hx;
        if(!d){if(lo>=targetHi || hi<=targetLo)return {};continue;}
        const int t=(d>0?targetLo-hi:targetHi-lo)*64/d,u=(d>0?targetHi-lo:targetLo-hi)*64/d;
        if(t>enter){enter=t;axis=a;normal=d>0?-1:1;}exit=std::min(exit,u);
    }if(enter<0 || enter>64 || exit<=enter)return {};return Expected{b.id,enter,axis,normal,false};
}
void sameHits(const std::vector<DetCollisionQueryHit2D>& actual,std::vector<Expected> expected) {
    std::sort(expected.begin(),expected.end(),[](const auto& a,const auto& b){return std::tuple(a.time,a.axis,a.id)<std::tuple(b.time,b.axis,b.id);});
    check(actual.size()==expected.size(),"integer oracle hit count");
    for(std::size_t i=0;i<actual.size();++i){const auto& a=actual[i];const auto& e=expected[i];
        const auto normal=e.normal==0?0u:e.normal<0?0xbf800000u:0x3f800000u;
        const auto fraction=fractionBits(e.time)|(!e.time && !e.inside && e.normal>0?0x80000000u:0u);
        check(a.entity==e.id && a.hit.fraction.bits()==fraction && a.hit.axis==unsigned(e.axis) && a.hit.initialOverlap==e.inside,"integer oracle identity/fraction/axis/penetration");
        check(a.hit.normal.x.bits()==(e.axis==0?normal:0u) && a.hit.normal.y.bits()==(e.axis==1?normal:0u),"integer oracle normal");
    }
}
void gridCase(std::uint64_t seed) {
    auto rng=ayt::math::make_pcg32_state(seed);auto integer=[&](int lo,int hi){return ayt::math::random_int(rng,lo,hi);};
    DetCollision2DConfig policy;policy.maxBodies=16;policy.maxTriggerPairs=16;
    DeterministicSession a,b;check(installDetCollision2D(a,policy) && installDetCollision2D(b,policy),"grid policies");
    std::vector<std::pair<SimEntityId,DetActorState>> actors;std::vector<GridBody> bodies;
    for(unsigned i=0;i<8;++i){const auto id=SimEntityId(10+i*7);const int x=integer(-24,24),y=integer(-24,24),hx=integer(1,3),hy=integer(1,3),ox=integer(-2,2),oy=integer(-2,2);
        const auto layer=1u<<integer(0,2),mask=std::uint32_t(integer(0,7));const bool trigger=true;
        actors.push_back({id,actor(policy,x,y,hx,hy,layer,mask,trigger,ox,oy)});bodies.push_back({id,x+ox-hx,y+oy-hy,x+ox+hx,y+oy+hy,layer,mask,trigger});}
    for(const auto& [id,value]:actors)check(a.addEntity(id,value),"grid actor");std::reverse(actors.begin(),actors.end());for(const auto& [id,value]:actors)check(b.addEntity(id,value),"reverse grid actor");
    check(a.seal() && b.seal(),"grid sealing");const auto ca=*a.checkpoint(),cb=*b.checkpoint();check(encodeDetCheckpoint(ca)==encodeDetCheckpoint(cb),"registration order canonical");
    const DetCollisionQuery2D qa(ca,policy),qb(cb,policy);
    for(unsigned i=0;i<32;++i){const int x=integer(-32,32),y=integer(-32,32),hx=integer(0,5),hy=integer(0,5);
        const auto delta=[&]{const auto power=integer(0,6);return power?((integer(0,1)?1:-1)*(1<<power)):0;};const int dx=delta(),dy=delta();
        DetCollisionQueryFilter2D filter;filter.layer=1u<<integer(0,2);filter.mask=integer(0,7);filter.includeTriggers=integer(0,1);filter.ignore=integer(0,1)?0:10+7*integer(0,7);
        std::vector<SimEntityId> closedIds,interiorIds;std::vector<Expected> rays,sweeps;
        for(const auto& body:bodies)if(match(body,filter)){if(closed(body,x-hx,y-hy,x+hx,y+hy))closedIds.push_back(body.id);if(interior(body,x-hx,y-hy,x+hx,y+hy))interiorIds.push_back(body.id);
            if(auto hit=rayOracle(body,x,y,dx,dy))rays.push_back(*hit);if(hx && hy)if(auto hit=sweepOracle(body,x-hx,y-hy,x+hx,y+hy,dx,dy))sweeps.push_back(*hit);}
        const auto bounds=box(x,y,hx,hy);check(qa.overlap(bounds,filter)==closedIds && qb.overlap(bounds,filter)==closedIds,"closed query integer oracle");
        check(qa.overlap(bounds,filter,true)==interiorIds && qb.overlap(bounds,filter,true)==interiorIds,"strict query integer oracle");
        sameHits(qa.raycast(v(x,y),v(dx,dy),filter),rays);sameHits(qb.raycast(v(x,y),v(dx,dy),filter),rays);
        if(hx && hy){sameHits(qa.sweep(bounds,v(dx,dy),filter),sweeps);sameHits(qb.sweep(bounds,v(dx,dy),filter),sweeps);}
    }
}
std::uint64_t number(const char* text){std::uint64_t n=0;const std::string_view s=text;const auto result=std::from_chars(s.data(),s.data()+s.size(),n);if(result.ec!=std::errc{} || result.ptr!=s.data()+s.size())throw std::invalid_argument("Invalid campaign integer");return n;}
std::string quoteBytes(std::string_view value) {
    constexpr char hex[]="0123456789abcdef";std::string result="\"";
    for(const unsigned char c:value){if(c=='"' || c=='\\'){result+='\\';result+=char(c);}
        else if(c<0x20 || c>=0x80){result+="\\u00";result+=hex[c>>4];result+=hex[c&15];}
        else result+=char(c);}
    return result+'"';
}
}
int main(int argc,char** argv) {
    std::uint64_t seed=1,cases=128,seconds=0,completed=0;std::filesystem::path output;
    const auto start=std::chrono::steady_clock::now();
    try {
        for(int i=1;i<argc;++i){const std::string_view arg=argv[i];if(i+1>=argc)throw std::invalid_argument("Missing option value");
            if(arg=="--seed")seed=number(argv[++i]);else if(arg=="--cases")cases=number(argv[++i]);else if(arg=="--seconds")seconds=number(argv[++i]);else if(arg=="--output")output=argv[++i];else throw std::invalid_argument("Unknown campaign option");}
        if(!cases || cases>10000000 || seconds>86400)throw std::invalid_argument("Campaign bounds: cases1..10000000, seconds0..86400");
        if(!output.empty())std::filesystem::create_directories(output);
        handChecks();callbackChecks();auto nextReport=start;
        const auto writeReport=[&](const char* status,const std::string& error){const auto elapsed=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now()-start).count();
            const auto json=std::string("{\"schema\":1,\"campaign\":\"collision-query\",\"status\":\"")+status+"\",\"firstSeed\":\""+std::to_string(seed)+"\",\"currentSeed\":\""+std::to_string(seed+completed)+"\",\"cases\":"+std::to_string(completed)+",\"querySets\":"+std::to_string(completed*32)+",\"elapsedSeconds\":"+std::to_string(elapsed)+",\"error\":"+quoteBytes(error)+"}";
            if(!output.empty()){std::ofstream file(output/"progress.json",std::ios::binary|std::ios::trunc);file<<json<<'\n';file.close();if(!file)throw std::runtime_error("Campaign report write failed");}return json;};
        for(;completed<cases || (seconds && std::chrono::steady_clock::now()<start+std::chrono::seconds(seconds));){try{gridCase(seed+completed);}catch(const std::exception& e){std::cerr<<writeReport("failed",e.what())<<'\n';return 1;}++completed;
            if(std::chrono::steady_clock::now()>=nextReport){(void)writeReport("running","");nextReport=std::chrono::steady_clock::now()+std::chrono::seconds(30);}}
        std::cout<<"PASS "<<writeReport("passed","")<<'\n';return 0;
    }catch(const std::exception& e){std::cerr<<"collision query campaign: "<<e.what()<<"; seed="<<(seed+completed)<<'\n';return 1;}
}
