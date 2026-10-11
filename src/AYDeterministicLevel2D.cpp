#include <AYEntity/DeterministicLevel2D.h>
#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>

namespace ayt::entity {
namespace {
using D=math::DetFloat32;using V=math::DetVec2;using Bytes=std::vector<std::uint8_t>;
[[noreturn]] void invalid(const char* reason){throw std::invalid_argument(reason);}
std::uint32_t indexed(std::uint32_t base,std::size_t offset){
    if(!base || offset>std::numeric_limits<std::uint32_t>::max()-base)invalid("Level registration ID overflow");
    const auto id=base+static_cast<std::uint32_t>(offset);if(id==UINT32_MAX)invalid("Reserved level registration ID");return id;
}
struct Writer {Bytes bytes;
    void u(std::uint64_t value,unsigned width=4){for(unsigned n=0;n<width;++n)bytes.push_back(static_cast<std::uint8_t>(value>>(8*n)));}
    void scalar(D value){if(!value.isFinite())invalid("Nonfinite level Float32");u(value.bits());}
    void vector(V value){scalar(value.x);scalar(value.y);}
};
template<class Codec> void collision(Codec& io,DetCollision2DConfig& c){
    io.word(c.bodySchema);io.word(c.historySchema);io.word(c.systemId);io.signedWord(c.priority);
    io.word(c.eventType);io.word(c.maxBodies);io.word(c.maxTriggerPairs);io.word(c.maxContactIterations);
    io.boolean(c.extended);io.boolean(c.oneWayPlatforms);io.boolean(c.stepUp);io.scalar(c.maxStepHeight);
    io.enumeration(c.motionPolicy);io.boolean(c.staticSlopes);io.boolean(c.movingSlopes);io.boolean(c.continuousTerrain);
}
template<class Codec> void character(Codec& io,DetCharacterController2DConfig& c){
    io.word(c.stateSchema);io.word(c.policySchema);io.word(c.preSystemId);io.word(c.postSystemId);
    io.word(c.validatorId);io.word(c.logicProfileId);
    io.scalar(c.maxHorizontalSpeed);io.scalar(c.acceleration);io.scalar(c.deceleration);io.scalar(c.gravity);
    io.scalar(c.jumpSpeed);io.scalar(c.maxFallSpeed);io.scalar(c.maxPlatformSpeed);io.scalar(c.groundProbeDistance);
    io.scalar(c.groundContactTolerance);io.scalar(c.snapDistance);io.enumeration(c.leaveVelocity);io.boolean(c.advanced);
    io.word(c.coyoteTicks);io.word(c.jumpBufferTicks);io.word(c.dropThroughTicks);io.boolean(c.crouchEnabled);
    io.scalar(c.crouchHeightScale);io.scalar(c.maxSlopeRisePerRun);
}
struct BinaryCodec {Writer w;
    void word(std::uint32_t value){w.u(value);}void signedWord(std::int32_t value){w.u(static_cast<std::uint32_t>(value));}
    void boolean(bool value){w.u(value?1:0);}void scalar(D value){w.scalar(value);}
    template<class E>void enumeration(E value){w.u(static_cast<std::uint32_t>(value));}
};
Bytes canonical(const DetLevel2D& level){
    if(level.paths.size()>8 || level.actors.empty() || level.actors.size()>64)invalid("Level body/path count limit");
    BinaryCodec io;for(auto ch:std::string_view("AYDLV001"))io.w.u(static_cast<unsigned char>(ch),1);
    io.word(kDetLevel2DProfileVersion);io.word(level.applicationVersion);io.word(level.inputVersion);
    io.word(level.stepNumerator);io.word(level.stepDenominator);
    auto policy=level.character;collision(io,policy.collision);character(io,policy);
    io.word(static_cast<std::uint32_t>(level.paths.size()));
    for(const auto& path:level.paths){if(path.waypoints.size()<2 || path.waypoints.size()>24)invalid("Level waypoint count limit");
        io.word(path.id);io.enumeration(path.playback);io.word(static_cast<std::uint32_t>(path.waypoints.size()));
        for(const auto& point:path.waypoints){io.w.vector(point.offset);io.word(point.travelTicks);io.word(point.dwellTicks);}}
    io.word(static_cast<std::uint32_t>(level.actors.size()));
    for(const auto& actor:level.actors){io.w.u(actor.id,8);io.enumeration(actor.body.mode);io.enumeration(actor.body.shape);
        io.w.vector(actor.position);io.w.vector(actor.body.half);io.w.vector(actor.body.offset);io.w.vector(actor.body.velocity);
        io.word(actor.body.layer);io.word(actor.body.mask);io.boolean(actor.body.trigger);io.boolean(actor.body.oneWay);
        io.boolean(actor.character);io.word(actor.path);
        if(actor.body.ignoredOneWay.value)invalid("Level cannot author ignored platform IDs");}
    if(io.w.bytes.size()>kDetLevel2DMaxCanonicalBytes)invalid("Level canonical content limit");return std::move(io.w.bytes);
}
std::uint64_t hash(const Bytes& bytes){std::uint64_t h=14695981039346656037ull;for(auto byte:bytes){h^=byte;h*=1099511628211ull;}return h;}
struct Chunk {DetTypedStateSchema schema;std::vector<std::uint64_t> words;};
struct Prepared {Bytes bytes;std::vector<DetPathMotion2DConfig> paths;std::vector<Chunk> chunks;std::vector<std::pair<SimEntityId,DetActorState>> actors;};
void unique(std::set<std::uint32_t>& ids,std::uint32_t value,bool schema=false){
    if(!value || value==UINT32_MAX || (schema && value<2) || !ids.insert(value).second)invalid("Level registration namespace conflict");
}
Prepared prepare(const DetLevel2D& level,const DetLevel2DOptions& options){
    Prepared result;result.bytes=canonical(level);const auto& cc=level.character;const auto& bc=cc.collision;
    if(!level.applicationVersion || !level.inputVersion || !level.stepNumerator || !level.stepDenominator)invalid("Zero level session version/step");
    if(level.actors.size()>bc.maxBodies)invalid("Level exceeds configured body budget");
    std::set<std::uint32_t> schemas,systems,validators,logic;
    for(auto id:{bc.bodySchema,bc.historySchema,cc.stateSchema,cc.policySchema})unique(schemas,id,true);
    for(auto id:{bc.systemId,cc.preSystemId,cc.postSystemId})unique(systems,id);
    unique(validators,bc.systemId);unique(validators,cc.validatorId);unique(logic,cc.logicProfileId);
    unique(validators,options.identityValidatorId);unique(logic,options.identityLogicId);
    std::uint32_t previousPath=0;
    for(std::size_t n=0;n<level.paths.size();++n){const auto& source=level.paths[n];if(source.id<=previousPath)invalid("Path IDs must increase");previousPath=source.id;
        DetPathMotion2DConfig p;p.collision=bc;p.playback=source.playback;p.waypoints=source.waypoints;
        p.tickPeriod=D::fromUInt(level.stepNumerator)/D::fromUInt(level.stepDenominator);
        p.stateSchema=indexed(options.pathSchemaBase,2*n);p.policySchema=indexed(options.pathSchemaBase,2*n+1);
        p.preSystemId=indexed(options.pathPreSystemBase,n);p.postSystemId=indexed(options.pathPostSystemBase,n);
        p.validatorId=indexed(options.pathValidatorBase,n);p.logicProfileId=indexed(options.pathLogicBase,n);
        if(!(p.preSystemId<cc.preSystemId && p.postSystemId>cc.postSystemId))invalid("Level path/character ordering conflict");
        unique(schemas,p.stateSchema,true);unique(schemas,p.policySchema,true);unique(systems,p.preSystemId);unique(systems,p.postSystemId);
        unique(validators,p.validatorId);unique(logic,p.logicProfileId);result.paths.push_back(std::move(p));}
    std::vector<std::uint64_t> words(1+(result.bytes.size()+7)/8);words[0]=result.bytes.size();
    for(std::size_t n=0;n<result.bytes.size();++n)words[1+n/8]|=std::uint64_t{result.bytes[n]}<<(8*(n%8));
    for(std::size_t offset=0;offset<words.size();offset+=128){const auto id=indexed(options.identitySchemaBase,result.chunks.size());
        if(result.chunks.size()>=16)invalid("Level identity schema limit");unique(schemas,id,true);
        Chunk chunk;chunk.schema.id=id;chunk.schema.version=kDetLevel2DProfileVersion;
        const auto size=std::min<std::size_t>(128,words.size()-offset);
        for(std::size_t n=0;n<size;++n)chunk.schema.fields.push_back({static_cast<std::uint32_t>(n+1),words[offset+n]});
        chunk.words=DetStateLayout(chunk.schema).defaults();result.chunks.push_back(std::move(chunk));}
    std::set<std::uint32_t> usedPaths;SimEntityId previousActor=0;
    for(const auto& actor:level.actors){if(!actor.id || actor.id<=previousActor)invalid("Actor IDs must increase");previousActor=actor.id;
        if(actor.body.mode==DetBodyMode2D::Disabled)invalid("Level actors must be active");
        if(actor.character && (actor.body.mode!=DetBodyMode2D::Kinematic || actor.body.trigger || actor.body.velocity!=V{}))invalid("Invalid initial character role/velocity");
        DetActorState state;DetSimTransformComponent pose;
        if(!pose.setPosition({actor.position.x,actor.position.y,{}}))invalid("Invalid level pose");state.pose=pose.snapshot();
        state.blocks[bc.bodySchema]=detCollisionBodyState2D(bc,actor.body);
        if(actor.character){DetCharacterState2D initial;initial.enabled=true;state.blocks[cc.stateSchema]=detCharacterState2D(cc,initial);}
        if(actor.path){auto path=std::find_if(level.paths.begin(),level.paths.end(),[&](const auto& p){return p.id==actor.path;});
            if(path==level.paths.end() || actor.character || actor.body.mode!=DetBodyMode2D::MovingObstacle || actor.body.trigger || actor.body.velocity!=V{})invalid("Invalid level path binding");
            const auto index=static_cast<std::size_t>(path-level.paths.begin());DetPathMotionState2D initial;initial.bound=true;initial.origin=actor.position;
            state.blocks[result.paths[index].stateSchema]=detPathMotionState2D(result.paths[index],initial);usedPaths.insert(actor.path);}
        result.actors.emplace_back(actor.id,std::move(state));}
    if(usedPaths.size()!=level.paths.size())invalid("Unused level path policy");
    std::vector<DetCollisionProxy2D> proxies;
    for(const auto& actor:level.actors){const auto center=actor.position+actor.body.offset;
        proxies.push_back({actor.id,{center-actor.body.half,center+actor.body.half},actor.body.layer,actor.body.mask,actor.body.trigger,actor.body.shape});}
    // The real pure narrowphase excludes the empty triangular bounding corner.
    for(const auto& pair:detCollisionPairs2D(proxies)){
        const auto first=std::find_if(level.actors.begin(),level.actors.end(),[&](const auto& a){return a.id==pair.first;});
        const auto second=std::find_if(level.actors.begin(),level.actors.end(),[&](const auto& a){return a.id==pair.second;});
        if(first->body.trigger || second->body.trigger || first->body.oneWay || second->body.oneWay
            || (first->body.mode==DetBodyMode2D::Static && second->body.mode==DetBodyMode2D::Static))continue;
        if(!bc.extended && !(first->body.mode==DetBodyMode2D::Kinematic && second->body.mode==DetBodyMode2D::Static)
            && !(second->body.mode==DetBodyMode2D::Kinematic && first->body.mode==DetBodyMode2D::Static))continue;
        invalid("Level initial ordinary solid penetration");}
    return result;
}
bool install(DeterministicSession& session,const DetLevel2D& level,const DetLevel2DOptions& options,const Prepared& prepared){
    if(!installDetCharacterController2D(session,level.character))return false;
    for(const auto& p:prepared.paths)if(!installDetPathMotion2D(session,p))return false;
    for(const auto& chunk:prepared.chunks)if(!(session.registerTypedSchema(chunk.schema) && session.registerGlobalState(chunk.schema.id)))return false;
    if(!session.registerLogicProfile(options.identityLogicId,kDetLevel2DProfileVersion,0x41444c5630303031ull))return false;
    if(!session.registerValidator(options.identityValidatorId,kDetLevel2DProfileVersion,[chunks=prepared.chunks](const DetSessionCheckpoint& cp,std::string& error){
        for(const auto& chunk:chunks){auto g=cp.globals.find(chunk.schema.id);if(g==cp.globals.end() || g->second!=chunk.words){error="Immutable level content global changed";return false;}
            for(const auto& [id,actor]:cp.actors){(void)id;auto block=actor.blocks.find(chunk.schema.id);
                if(block==actor.blocks.end() || block->second!=chunk.words){error="Immutable level content actor copy changed";return false;}}}return true;}))return false;
    for(const auto& [id,state]:prepared.actors)if(!session.addEntity(id,state))return false;return true;
}
Prepared checked(const DetLevel2D& level,const DetLevel2DOptions& options){auto prepared=prepare(level,options);
    DeterministicSession scratch(detLevelSessionConfig(level));if(!install(scratch,level,options,prepared) || !scratch.seal())throw std::invalid_argument("Level preflight: "+scratch.error());return prepared;}
struct TokenReader {
    std::string_view text;std::size_t offset=0;
    explicit TokenReader(std::string_view source):text(source){if(source.empty() || source.size()>kDetLevel2DMaxTextBytes)invalid("Level text size limit");
        for(unsigned char c:source)if(c>126 || (c<32 && c!='\t' && c!='\r' && c!='\n'))invalid("Level text must be ASCII");}
    std::string_view next(){while(offset<text.size() && (text[offset]==' ' || text[offset]=='\t' || text[offset]=='\r' || text[offset]=='\n'))++offset;
        const auto begin=offset;while(offset<text.size() && text[offset]!=' ' && text[offset]!='\t' && text[offset]!='\r' && text[offset]!='\n')++offset;
        if(begin==offset)invalid("Truncated level token");return text.substr(begin,offset-begin);}
    void expect(std::string_view value){if(next()!=value)invalid("Unexpected level token");}
    template<class T>T integer(){const auto token=next();T result{};const auto [end,error]=std::from_chars(token.data(),token.data()+token.size(),result);
        if(error!=std::errc{} || end!=token.data()+token.size())invalid("Invalid level integer");return result;}
    void word(std::uint32_t& value){value=integer<std::uint32_t>();}void signedWord(std::int32_t& value){value=integer<std::int32_t>();}
    void boolean(bool& value){auto x=integer<std::uint32_t>();if(x>1)invalid("Invalid level Boolean");value=x!=0;}
    void scalar(D& value){const auto token=next();std::uint32_t bits=0;const auto [end,error]=std::from_chars(token.data(),token.data()+token.size(),bits,16);
        if(token.size()!=8 || error!=std::errc{} || end!=token.data()+token.size())invalid("Float32 needs eight hex digits");value=D::fromBits(bits);if(!value.isFinite())invalid("Nonfinite level Float32");}
    void vector(V& value){scalar(value.x);scalar(value.y);}template<class E>void enumeration(E& value){value=static_cast<E>(integer<std::uint32_t>());}
    void end(){while(offset<text.size() && (text[offset]==' ' || text[offset]=='\t' || text[offset]=='\r' || text[offset]=='\n'))++offset;if(offset!=text.size())invalid("Trailing level tokens");}
};
struct TextWriter {std::ostringstream stream;
    TextWriter(){stream.imbue(std::locale::classic());}
    void word(std::uint32_t value){stream<<' '<<value;}void signedWord(std::int32_t value){stream<<' '<<value;}
    void boolean(bool value){word(value?1:0);}template<class E>void enumeration(E value){word(static_cast<std::uint32_t>(value));}
    void scalar(D value){constexpr char digits[]="0123456789abcdef";stream<<' ';for(int n=7;n>=0;--n)stream<<digits[(value.bits()>>(4*n))&15];}
    void vector(V value){scalar(value.x);scalar(value.y);}
};
} // namespace
DetSessionConfig detLevelSessionConfig(const DetLevel2D& level){const auto bytes=canonical(level);return {level.applicationVersion,level.inputVersion,level.stepNumerator,level.stepDenominator,hash(bytes)};}
DetPathMotion2DConfig detLevelPathConfig2D(const DetLevel2D& level,std::uint32_t path,const DetLevel2DOptions& options){const auto p=prepare(level,options);
    for(std::size_t n=0;n<level.paths.size();++n)if(level.paths[n].id==path)return p.paths[n];invalid("Unknown level path ID");}
bool validateDetLevel2D(const DetLevel2D& level,std::string& error,const DetLevel2DOptions& options){try{(void)checked(level,options);error.clear();return true;}catch(const std::exception& e){error=e.what();return false;}}
bool decodeDetLevel2D(std::string_view text,DetLevel2D& output,std::string& error,const DetLevel2DOptions& options){try{
    TokenReader r(text);DetLevel2D value;r.expect("AYDLEVEL");if(r.integer<std::uint32_t>()!=kDetLevel2DProfileVersion)invalid("Unknown level format version");
    r.expect("session");r.word(value.applicationVersion);r.word(value.inputVersion);r.word(value.stepNumerator);r.word(value.stepDenominator);
    r.expect("collision");collision(r,value.character.collision);r.expect("character");character(r,value.character);
    r.expect("paths");const auto paths=r.integer<std::uint32_t>();if(paths>8)invalid("Level path count limit");
    for(std::uint32_t n=0;n<paths;++n){r.expect("path");DetLevelPath2D p;r.word(p.id);r.enumeration(p.playback);const auto count=r.integer<std::uint32_t>();
        if(count<2 || count>24)invalid("Level waypoint count limit");for(std::uint32_t k=0;k<count;++k){r.expect("point");DetPathWaypoint2D point;r.vector(point.offset);r.word(point.travelTicks);r.word(point.dwellTicks);p.waypoints.push_back(point);}value.paths.push_back(std::move(p));}
    r.expect("actors");const auto actors=r.integer<std::uint32_t>();if(!actors || actors>64)invalid("Level actor count limit");
    for(std::uint32_t n=0;n<actors;++n){r.expect("actor");DetLevelActor2D actor;actor.id=r.integer<std::uint64_t>();r.enumeration(actor.body.mode);r.enumeration(actor.body.shape);
        r.vector(actor.position);r.vector(actor.body.half);r.vector(actor.body.offset);r.vector(actor.body.velocity);r.word(actor.body.layer);r.word(actor.body.mask);
        r.boolean(actor.body.trigger);r.boolean(actor.body.oneWay);r.boolean(actor.character);r.word(actor.path);value.actors.push_back(actor);}
    r.expect("end");r.end();(void)checked(value,options);output=std::move(value);error.clear();return true;
}catch(const std::exception& e){error=e.what();return false;}}
bool loadDetLevel2D(const std::filesystem::path& path,DetLevel2D& output,std::string& error,const DetLevel2DOptions& options){try{
    std::ifstream input(path,std::ios::binary);if(!input){error="Cannot open level resource";return false;}
    std::string text(kDetLevel2DMaxTextBytes+1,'\0');input.read(text.data(),static_cast<std::streamsize>(text.size()));const auto count=input.gcount();
    if(input.bad() || count<0 || static_cast<std::size_t>(count)>kDetLevel2DMaxTextBytes){error="Level file read/size limit";return false;}text.resize(static_cast<std::size_t>(count));return decodeDetLevel2D(text,output,error,options);
}catch(const std::exception& e){error=e.what();return false;}}
std::string encodeDetLevel2D(const DetLevel2D& level,const DetLevel2DOptions& options){(void)checked(level,options);TextWriter w;w.stream<<"AYDLEVEL 1\nsession";
    w.word(level.applicationVersion);w.word(level.inputVersion);w.word(level.stepNumerator);w.word(level.stepDenominator);
    auto policy=level.character;w.stream<<"\ncollision";collision(w,policy.collision);w.stream<<"\ncharacter";character(w,policy);
    w.stream<<"\npaths";w.word(static_cast<std::uint32_t>(level.paths.size()));for(const auto& path:level.paths){w.stream<<"\npath";w.word(path.id);w.enumeration(path.playback);w.word(static_cast<std::uint32_t>(path.waypoints.size()));
        for(const auto& point:path.waypoints){w.stream<<"\npoint";w.vector(point.offset);w.word(point.travelTicks);w.word(point.dwellTicks);}}
    w.stream<<"\nactors";w.word(static_cast<std::uint32_t>(level.actors.size()));for(const auto& actor:level.actors){w.stream<<"\nactor "<<actor.id;w.enumeration(actor.body.mode);w.enumeration(actor.body.shape);
        w.vector(actor.position);w.vector(actor.body.half);w.vector(actor.body.offset);w.vector(actor.body.velocity);w.word(actor.body.layer);w.word(actor.body.mask);
        w.boolean(actor.body.trigger);w.boolean(actor.body.oneWay);w.boolean(actor.character);w.word(actor.path);}w.stream<<"\nend\n";
    auto result=w.stream.str();if(result.size()>kDetLevel2DMaxTextBytes)invalid("Encoded level text size limit");return result;}
bool configureDetLevel2D(DeterministicSession& session,const DetLevel2D& level,std::string& error,const DetLevel2DOptions& options){try{
    if(session.sealed() || session.faulted() || session.nextTick()!=0 || session.inputVersion()!=level.inputVersion
        || session.fixedStep().bits()!=(D::fromUInt(level.stepNumerator)/D::fromUInt(level.stepDenominator)).bits())invalid("Level needs a fresh matching Session");
    const auto prepared=checked(level,options);if(!install(session,level,options,prepared)){
        error=session.error().empty()?"Level destination registration conflict":session.error();return false;}error.clear();return true;
}catch(const std::exception& e){error=e.what();return false;}}
} // namespace ayt::entity
