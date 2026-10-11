// Content authoring boundary: no file access or native float conversion in Sim.
#include <AYEntity/DeterministicLevel2D.h>
#include <AYMath/DetFloatText.h>
#include <AYMath/DetGeometry2D.h>
#include <AYMath/DetSlopeGeometry2D.h>
#include <algorithm>
#include <charconv>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace ayt::entity;
std::string json(std::string_view text) {
    std::string result="\"";
    constexpr char hex[]="0123456789abcdef";
    for(unsigned char c:text) {
        if(c=='"' || c=='\\') {result+='\\';result+=static_cast<char>(c);}
        else if(c<32) {result+="\\u00";result+=hex[c>>4];result+=hex[c&15];}
        else result+=static_cast<char>(c);
    }
    return result+'"';
}
std::string bits(std::uint32_t value) {
    constexpr char hex[]="0123456789abcdef";std::string result(8,'0');
    for(unsigned n=0;n<8;++n)result[n]=hex[(value>>(4*(7-n)))&15];
    return result;
}
void require(bool value,const std::string& message) {if(!value)throw std::runtime_error(message);}
void ordinary(const std::filesystem::path& path) {
    require(!std::filesystem::is_symlink(path) && std::filesystem::is_regular_file(path),
        "Input must be an ordinary, non-symlink file");
}
// Offline authoring helper: every value entering a level is software binary32.
DetLevel2D starter(std::string_view kind) {
    using D=ayt::math::DetFloat32;
    require(kind=="character" || kind=="dash","Unknown starter; choose character or dash");
    DetLevel2D level;
    DetLevelActor2D hero;hero.id=10;hero.character=true;hero.body.mode=DetBodyMode2D::Kinematic;
    hero.position={D{},D::fromInt(1)/D::fromInt(2)};
    hero.body.half={D::fromInt(1)/D::fromInt(4),D::fromInt(1)/D::fromInt(2)};
    hero.body.layer=1;hero.body.mask=2;
    DetLevelActor2D floor;floor.id=100;floor.body.mode=DetBodyMode2D::Static;
    floor.position={D{},-D::fromInt(1)/D::fromInt(2)};
    floor.body.half={D::fromInt(8),D::fromInt(1)/D::fromInt(2)};
    floor.body.layer=2;floor.body.mask=1;
    level.actors={hero,floor};
    if(kind=="dash") {
        level.formatVersion=kDetLevel2DCharacterControlProfileVersion;
        level.character.advanced=true;level.character.collision.oneWayPlatforms=true;
        DetCharacterControlOwner2D owner;owner.actor=hero.id;owner.groundOnly=true;
        level.characterControlOwners={owner};
    }
    return level;
}
void writeFresh(const std::filesystem::path& destination,const std::string& canonical) {
    require(!std::filesystem::exists(destination) && !std::filesystem::is_symlink(destination),"Output must be fresh");
    std::ofstream output(destination,std::ios::binary|std::ios::out|std::ios::noreplace);
    require(bool(output),"Cannot exclusively create output");
    output.write(canonical.data(),static_cast<std::streamsize>(canonical.size()));output.flush();
    const bool wrote=bool(output);output.close();
    require(wrote && bool(output),"Canonical output failed; partial fresh file may remain");
}
ayt::math::DetFloat32 finiteBits(std::string_view token) {
    require(token.size()==8,"float-bits requires exactly eight hexadecimal digits");
    std::uint32_t raw=0;
    for(char c:token) {
        unsigned digit;
        if(c>='0' && c<='9')digit=static_cast<unsigned>(c-'0');
        else if(c>='a' && c<='f')digit=static_cast<unsigned>(c-'a'+10);
        else if(c>='A' && c<='F')digit=static_cast<unsigned>(c-'A'+10);
        else throw std::runtime_error("float-bits requires hexadecimal digits");
        raw=(raw<<4)|digit;
    }
    auto value=ayt::math::DetFloat32::fromBits(raw);
    require(value.isFinite(),"float-bits requires a finite Float32 value");return value;
}
// Stage60 private offline SVG helper. Insert after Stage57 finiteBits(), before
// inspect(); never export this as a gameplay/core facade. No simulation advance.
struct PreviewSvg2D {
    std::string svg;
    ayt::math::DetVec2 worldMin{},worldMax{};
    std::array<ayt::math::DetFloat32,4> viewBox{};
    std::size_t actorCount=0,pathInstances=0,waypointCount=0;
};
std::string xml(std::string_view text) {
    std::string result;
    for(char c:text) {
        if(c=='&')result+="&amp;";else if(c=='<')result+="&lt;";
        else if(c=='>')result+="&gt;";else if(c=='\"')result+="&quot;";
        else if(c=='\'')result+="&apos;";
        else {require(static_cast<unsigned char>(c)>=32 || c=='\n' || c=='\t',"Invalid preview XML text");result+=c;}
    }
    return result;
}
PreviewSvg2D buildPreviewSvg2D(const DetLevel2D& level) {
    using D=ayt::math::DetFloat32;using V=ayt::math::DetVec2;using Box=ayt::math::DetAabb2;
    constexpr std::size_t maxBytes=1024*1024,maxPathPoints=64*24;
    require(level.actors.size()<=64,"Preview actor budget exceeded");
    PreviewSvg2D result;result.actorCount=level.actors.size();
    struct Body {const DetLevelActor2D* source;std::optional<Box> box;};
    struct Path {const DetLevelActor2D* source;const DetLevelPath2D* policy;std::vector<V> points;};
    std::vector<Body> bodies;std::vector<Path> paths;bool haveBounds=false;
    const auto finite=[](D value) {require(value.isFinite(),"Nonfinite or overflowing preview geometry/span");return value;};
    const auto extend=[&](V point) {
        require(point.isFinite(),"Nonfinite or overflowing preview point");
        if(!haveBounds){result.worldMin=result.worldMax=point;haveBounds=true;}
        else {result.worldMin.x=std::min(result.worldMin.x,point.x);result.worldMin.y=std::min(result.worldMin.y,point.y);
            result.worldMax.x=std::max(result.worldMax.x,point.x);result.worldMax.y=std::max(result.worldMax.y,point.y);}
    };
    for(const auto& actor:level.actors) {
        extend(actor.position);std::optional<Box> box;
        if(actor.body.mode!=DetBodyMode2D::Disabled) {
            // Exact runtime construction order; path origins below do not include offset.
            box=Box::fromCenterHalf(actor.position+actor.body.offset,actor.body.half);
            require(box->hasArea(),"Preview active body has no represented area");
            (void)finite(box->max.x-box->min.x);(void)finite(box->max.y-box->min.y);
            if(actor.body.shape!=DetCollisionShape2D::Aabb)
                require(ayt::math::DetSlope2{*box,actor.body.shape==DetCollisionShape2D::SlopeRisesRight}.valid(),"Invalid preview filled wedge");
            extend(box->min);extend(box->max);
        }
        bodies.push_back({&actor,box});
        if(actor.path) {
            const auto source=std::find_if(level.paths.begin(),level.paths.end(),[&](const auto& path){return path.id==actor.path;});
            require(source!=level.paths.end() && source->waypoints.size()>=2 && source->waypoints.size()<=24,"Invalid preview path reference/budget");
            require(result.waypointCount<=maxPathPoints-source->waypoints.size(),"Preview path point budget exceeded");
            Path path{&actor,&*source,{}};
            for(const auto& waypoint:source->waypoints) {const auto point=actor.position+waypoint.offset;extend(point);path.points.push_back(point);}
            result.waypointCount+=path.points.size();paths.push_back(std::move(path));
        }
    }
    result.pathInstances=paths.size();require(result.pathInstances<=64,"Preview path instance budget exceeded");
    if(!haveBounds){result.worldMin={};result.worldMax={D::fromInt(1),D::fromInt(1)};}
    auto width=finite(result.worldMax.x-result.worldMin.x),height=finite(result.worldMax.y-result.worldMin.y);
    require(width>=D{} && height>=D{},"Invalid preview world bounds");
    // A zero-width point-only/empty axis gets an explicit layout fallback, never
    // invented collision area. Display bounds are separate from authored worldMin/Max.
    auto viewX=result.worldMin.x,viewTop=-result.worldMax.y;
    if(width.isZero()){width=D::fromInt(1);viewX=finite(viewX-D::fromInt(1)/D::fromInt(2));}
    if(height.isZero()){height=D::fromInt(1);viewTop=finite(viewTop-D::fromInt(1)/D::fromInt(2));}
    result.viewBox={finite(viewX),finite(viewTop),width,height};
    auto font=finite(std::max(width,height)/D::fromInt(64));
    if(font.isZero())font=std::max(width,height);
    auto radius=finite(font/D::fromInt(4));if(radius.isZero())radius=font;
    const auto number=[&](D value){return ayt::math::detFloat32ExactDecimal(finite(value));};
    const auto point=[&](V value){return number(value.x)+","+number(value.y);};
    const auto append=[&](std::string_view text) {
        require(text.size()<=maxBytes-result.svg.size(),"Preview SVG exceeds 1 MiB budget");result.svg.append(text);
    };
    std::string view;
    for(auto value:result.viewBox){if(!view.empty())view+=' ';view+=number(value);}
    append("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1200\" height=\"800\" viewBox=\"0 0 1200 800\" data-preview-profile=\"1\">\n");
    append("<title>Authored initial deterministic level content; no simulation executed</title>\n");
    append("<rect x=\"0\" y=\"0\" width=\"1200\" height=\"800\" fill=\"white\"/>\n");
    append("<text x=\"20\" y=\"28\" font-family=\"sans-serif\" font-size=\"18\">Initial authoring preview - browser pixels are not authoritative</text>\n");
    append("<text x=\"20\" y=\"52\" font-family=\"sans-serif\" font-size=\"14\">Solid: gray / character: blue / trigger: orange dashed / one-way: purple / moving: teal / disabled-mask: pale</text>\n");
    append("<svg id=\"world\" x=\"20\" y=\"72\" width=\"1160\" height=\"660\" overflow=\"visible\" viewBox=\""+view+"\">\n");
    append("<g id=\"bodies\" transform=\"scale(1,-1)\" stroke-width=\"2\" vector-effect=\"non-scaling-stroke\">\n");
    for(const auto& body:bodies) {
        const auto& actor=*body.source;const auto id=std::to_string(actor.id);
        if(!body.box) {append("<circle data-actor=\""+id+"\" data-kind=\"disabled-pose\" cx=\""+number(actor.position.x)+"\" cy=\""+number(actor.position.y)
                +"\" r=\""+number(radius)+"\" fill=\"none\" stroke=\"#666\" vector-effect=\"non-scaling-stroke\"/>\n");continue;}
        const auto& box=*body.box;std::string vertices=point(box.min)+" "+point({box.max.x,box.min.y});
        if(actor.body.shape==DetCollisionShape2D::Aabb)vertices+=" "+point(box.max)+" "+point({box.min.x,box.max.y});
        else vertices+=" "+point(actor.body.shape==DetCollisionShape2D::SlopeRisesRight?box.max:V{box.min.x,box.max.y});
        const std::string role=actor.body.trigger?"trigger":actor.body.oneWay?"one-way":actor.character?"character":actor.body.mode==DetBodyMode2D::MovingObstacle?"moving":"solid";
        const std::string color=actor.body.trigger?"#ffdcaa":actor.body.oneWay?"#d8c9ef":actor.character?"#b9d8f9":actor.body.mode==DetBodyMode2D::MovingObstacle?"#b9e1d8":"#dedede";
        append("<polygon data-actor=\""+id+"\" data-kind=\""+(actor.body.shape==DetCollisionShape2D::Aabb?std::string{"aabb"}:std::string{"triangle"})
            +"\" data-role=\""+role+"\" data-mode=\""+std::to_string(static_cast<std::uint32_t>(actor.body.mode))+"\" data-layer=\""+std::to_string(actor.body.layer)
            +"\" data-mask=\""+std::to_string(actor.body.mask)+"\" points=\""+vertices+"\" fill=\""+color+"\" stroke=\"#333\" vector-effect=\"non-scaling-stroke\""
            +(actor.body.mask?std::string{}:std::string{" opacity=\"0.35\""})+(actor.body.trigger?std::string{" stroke-dasharray=\"5 4\""}:std::string{})+">\n");
        append("<title>"+xml("Actor "+id+" / "+role+" / authored initial shape")+"</title></polygon>\n");
        if(actor.body.oneWay)append("<line data-one-way-top=\""+id+"\" x1=\""+number(box.min.x)+"\" y1=\""+number(box.max.y)+"\" x2=\""+number(box.max.x)
            +"\" y2=\""+number(box.max.y)+"\" stroke=\"#735391\" stroke-width=\"4\" vector-effect=\"non-scaling-stroke\"/>\n");
    }
    append("</g>\n<g id=\"paths\" transform=\"scale(1,-1)\" fill=\"none\" stroke=\"#087d70\" stroke-width=\"2\">\n");
    for(const auto& path:paths) {
        std::string vertices;for(auto value:path.points){if(!vertices.empty())vertices+=' ';vertices+=point(value);}
        if(path.policy->playback==DetPathPlayback2D::Loop)vertices+=" "+point(path.points.front());
        const auto actorId=std::to_string(path.source->id),pathId=std::to_string(path.policy->id);
        append("<polyline data-path=\""+pathId+"\" data-actor=\""+actorId+"\" data-waypoint-count=\""+std::to_string(path.points.size())
            +"\" data-playback=\""+std::to_string(static_cast<std::uint32_t>(path.policy->playback))+"\" points=\""+vertices+"\" stroke-dasharray=\"4 3\" vector-effect=\"non-scaling-stroke\">\n");
        const std::string mode=path.policy->playback==DetPathPlayback2D::Once?"Once":path.policy->playback==DetPathPlayback2D::Loop?"Loop":"PingPong";
        append("<title>"+xml("Path "+pathId+" / actor "+actorId+" / "+mode+" / authored origin + waypoint offsets; not an executed or guaranteed reachable trajectory")+"</title></polyline>\n");
        for(std::size_t n=0;n<path.points.size();++n)append("<circle data-path-point=\""+std::to_string(n)+"\" data-actor=\""+actorId+"\" cx=\""+number(path.points[n].x)
            +"\" cy=\""+number(path.points[n].y)+"\" r=\""+number(radius)+"\" vector-effect=\"non-scaling-stroke\"/>\n");
    }
    append("</g>\n<g id=\"actor-labels\" font-family=\"sans-serif\" font-size=\""+number(font)+"\" fill=\"#111\" text-anchor=\"middle\">\n");
    for(const auto& actor:level.actors)append("<text data-pose-label=\""+std::to_string(actor.id)+"\" x=\""+number(actor.position.x)+"\" y=\""+number(-actor.position.y)
        +"\" dy=\"-0.25em\">"+xml(std::to_string(actor.id)+(actor.character?std::string{" character"}:std::string{}))+"</text>\n");
    append("</g></svg>\n<text x=\"20\" y=\"772\" font-family=\"sans-serif\" font-size=\"14\">XY only. Paths show authored points; no CP, ticks, collision prediction, recovery, AI or game effects were rendered.</text>\n</svg>\n");
    return result;
}

SimEntityId editActorId(std::string_view token) {
    SimEntityId id=0;const auto parsed=std::from_chars(token.data(),token.data()+token.size(),id,10);
    require(parsed.ec==std::errc{} && parsed.ptr==token.data()+token.size() && id!=0,
        "Actor identity must be a nonzero decimal integer");return id;
}
ayt::math::DetFloat32 editDecimal(std::string_view token) {
    ayt::math::DetFloat32 value;
    require(ayt::math::parseDetFloat32Decimal(token,value),"Invalid, overflowing or over-budget finite decimal");
    return value;
}

// Stage63 private offline authoring helpers; insert after the Stage61 edit
// helpers. Reuse actual FloatText and Level validation, without a second parser.
std::uint32_t solidWord(std::string_view token) {
    std::uint32_t value=0;
    const auto parsed=std::from_chars(token.data(),token.data()+token.size(),value,10);
    require(parsed.ec==std::errc{} && parsed.ptr==token.data()+token.size(),
        "Layer/mask must be a decimal UInt32 without overflow");
    return value;
}
DetCollisionShape2D solidShape(std::string_view token) {
    if(token=="aabb")return DetCollisionShape2D::Aabb;
    if(token=="slope-left")return DetCollisionShape2D::SlopeRisesLeft;
    if(token=="slope-right")return DetCollisionShape2D::SlopeRisesRight;
    throw std::runtime_error("Unknown solid shape; choose aabb, slope-left or slope-right");
}
DetLevel2D terrainStarter() {
    // New explicit acquisition only: old character/dash starters stay unchanged.
    // Static slopes do not require advanced/one-way Character. No moving slopes,
    // continuous-terrain promise, relocation, step-up or gameplay tables implied.
    auto level=starter("character");
    level.character.collision.staticSlopes=true;
    return level;
}

// Stage64 private offline path authoring. Reuse Stage61 editActorId/editDecimal,
// actual Level load/validation/encoding and the existing exclusive writeFresh.
std::uint32_t authorPathWord(std::string_view token,std::uint32_t minimum,
    std::uint32_t maximum,std::string_view name) {
    std::uint32_t value=0;
    const auto parsed=std::from_chars(token.data(),token.data()+token.size(),value,10);
    require(parsed.ec==std::errc{} && parsed.ptr==token.data()+token.size()
        && value>=minimum && value<=maximum,
        std::string(name)+" must be a bounded decimal UInt32");
    return value;
}
DetPathPlayback2D authorPathPlayback(std::string_view token) {
    if(token=="once")return DetPathPlayback2D::Once;
    if(token=="loop")return DetPathPlayback2D::Loop;
    if(token=="pingpong")return DetPathPlayback2D::PingPong;
    throw std::runtime_error("Unknown path playback; choose once, loop or pingpong");
}
DetLevelPath2D& authorExistingPath(DetLevel2D& level,std::uint32_t id) {
    const auto found=std::lower_bound(level.paths.begin(),level.paths.end(),id,
        [](const DetLevelPath2D& path,std::uint32_t identity){return path.id<identity;});
    require(found!=level.paths.end() && found->id==id,"Path identity is not authored in this level");
    return *found;
}
void authorBindOrdinaryPath(DetLevel2D& level,SimEntityId id,const DetLevelPath2D& path) {
    require(!path.movingTrigger,"Ordinary binding cannot grant the explicit moving-trigger path role");
    const auto found=std::lower_bound(level.actors.begin(),level.actors.end(),id,
        [](const DetLevelActor2D& actor,SimEntityId identity){return actor.id<identity;});
    require(found!=level.actors.end() && found->id==id,"Actor identity is not authored in this level");
    require(found->body.mode==DetBodyMode2D::Static && !found->character
        && !found->body.trigger && !found->body.oneWay && found->path==0,
        "Binding requires an unbound ordinary solid Static actor");
    if(found->body.shape!=DetCollisionShape2D::Aabb)
        require(level.character.collision.movingSlopes,
            "A translating wedge requires the source's explicit movingSlopes policy");
    // No source policy/permissions are upgraded. The path origin remains the
    // actor pose, independently of the preserved body's geometric offset.
    found->body.mode=DetBodyMode2D::MovingObstacle;
    found->body.velocity={};
    found->path=path.id;
}

// Stage65 private offline full-content geometry flattening. Reuse actual Level
// and Stage61 ID/FloatText helpers; no new core/Scene/Resource prefab interface.
struct SolidStampMap2D {SimEntityId source=0,destination=0;};
std::vector<SolidStampMap2D> stampMap(std::string_view token) {
    require(!token.empty() && token.size()<=1024,"Solid mapping token must contain1..1024 bytes");
    std::vector<SolidStampMap2D> result;std::size_t begin=0;
    while(begin<token.size()) {
        const auto comma=token.find(',',begin);
        const auto end=comma==std::string_view::npos?token.size():comma;
        const auto entry=token.substr(begin,end-begin);const auto colon=entry.find(':');
        require(colon!=std::string_view::npos && colon>0 && colon+1<entry.size()
            && entry.find(':',colon+1)==std::string_view::npos,
            "Each solid mapping must be exactly nonzero-old-ID:nonzero-new-ID");
        const SolidStampMap2D item{editActorId(entry.substr(0,colon)),editActorId(entry.substr(colon+1))};
        require(result.size()<16,"Solid mapping count exceeds16");
        require(std::none_of(result.begin(),result.end(),[&](const auto& existing){
            return existing.source==item.source || existing.destination==item.destination;
        }),"Solid mapping contains a duplicate donor or new actor identity");
        result.push_back(item);
        if(comma==std::string_view::npos)break;
        begin=comma+1;require(begin<token.size(),"Solid mapping must not contain an empty entry");
    }
    return result;
}
bool stampActorReferenced(const DetLevel2D& level,SimEntityId id) {
    // Only actor-reference namespaces are compared: equal numeric counter/path/
    // rule/objective/permission IDs alone do not confer actor authority.
    for(const auto& row:level.actionTargets)if(row.actor==id)return true;
    for(const auto& row:level.rules)if(row.trigger==id || row.actor==id || row.action.target==id)return true;
    for(const auto& row:level.claims)if(row.trigger==id || row.actor==id)return true;
    for(const auto& row:level.objectiveActions)if(row.action.target==id)return true;
    for(const auto& row:level.respawnSpawns)if(row.actor==id)return true;
    for(const auto& row:level.respawnBindings)if(row.actor==id)return true;
    for(const auto& row:level.respawnRegions)if(row.trigger==id || row.actor==id)return true;
    for(const auto& row:level.vitalityActors)if(row.actor==id)return true;
    for(const auto& row:level.vitalityPermissions)if(row.actor==id)return true;
    for(const auto& row:level.vitalityRegions)if(row.trigger==id || row.actor==id)return true;
    for(const auto& row:level.attackOwners) {
        if(row.actor==id)return true;
        for(const auto& target:row.targets)if(target.actor==id)return true;
    }
    for(const auto& row:level.brainOwners)if(row.actor==id || row.target==id)return true;
    for(const auto& row:level.characterControlOwners)if(row.actor==id)return true;
    // flowBindings contain claim/objective IDs; their actor closure is already
    // inspected through claims/objectiveActions, not through unrelated ID values.
    return false;
}
std::vector<DetLevelActor2D> stampActors(const DetLevel2D& donor,const DetLevel2D& target,
    const std::vector<SolidStampMap2D>& mappings,ayt::math::DetVec2 shift) {
    require(!mappings.empty() && mappings.size()<=16,"Solid mapping count must be1..16");
    require(target.actors.size()<=64 && mappings.size()<=64-target.actors.size(),"Level actor count limit");
    std::vector<DetLevelActor2D> additions;additions.reserve(mappings.size());
    for(const auto& mapping:mappings) {
        const auto source=std::lower_bound(donor.actors.begin(),donor.actors.end(),mapping.source,
            [](const auto& actor,SimEntityId id){return actor.id<id;});
        require(source!=donor.actors.end() && source->id==mapping.source,"Mapped donor actor is not authored");
        const auto conflict=std::lower_bound(target.actors.begin(),target.actors.end(),mapping.destination,
            [](const auto& actor,SimEntityId id){return actor.id<id;});
        require(conflict==target.actors.end() || conflict->id!=mapping.destination,"Mapped new actor identity already exists in target");
        require(source->body.mode==DetBodyMode2D::Static && !source->body.trigger
            && !source->body.oneWay && !source->body.ignoredOneWay.value && !source->character
            && !source->path,"Only ordinary nontrigger unbound Static solids may be stamped");
        require(!stampActorReferenced(donor,source->id),"Mapped donor actor has authored gameplay authority/references");
        if(source->body.shape!=DetCollisionShape2D::Aabb)
            require(target.character.collision.staticSlopes,"Target must already select staticSlopes for a stamped wedge");
        auto copy=*source;copy.id=mapping.destination;
        // Real software addition rounds normally, including signed-zero rules;
        // zero shift is not promised to preserve every source position bit.
        copy.position=source->position+shift;
        require(copy.position.isFinite(),"Nonfinite or overflowing stamped position");
        additions.push_back(std::move(copy));
    }
    return additions;
}

void inspect(const DetLevel2D& level,const std::filesystem::path& source) {
    const auto config=detLevelSessionConfig(level);
    std::cout<<"{\"schema\":1,\"status\":\"passed\",\"source\":"<<json(source.generic_string())
        <<",\"formatVersion\":"<<level.formatVersion<<",\"applicationVersion\":"<<level.applicationVersion
        <<",\"inputVersion\":"<<level.inputVersion<<",\"step\":{\"numerator\":"<<level.stepNumerator
        <<",\"denominator\":"<<level.stepDenominator<<"},\"contentHash\":"<<json(std::to_string(config.contentHash))
        <<",\"actorCount\":"<<level.actors.size()<<",\"pathCount\":"<<level.paths.size()
        <<",\"actionTargetCount\":"<<level.actionTargets.size()<<",\"ruleCount\":"<<level.rules.size()
        <<",\"counterCount\":"<<level.counters.size()<<",\"objectiveCount\":"<<level.objectives.size()
        <<",\"claimCount\":"<<level.claims.size()<<",\"flowBindingCount\":"<<level.flowBindings.size()
        <<",\"objectiveActionCount\":"<<level.objectiveActions.size()
        <<",\"characterRespawn\":"<<(level.character.collision.characterRespawn?"true":"false")
        <<",\"spawnCount\":"<<level.respawnSpawns.size()<<",\"respawnBindingCount\":"<<level.respawnBindings.size()
        <<",\"respawnRegionCount\":"<<level.respawnRegions.size()
        <<",\"vitalityActorCount\":"<<level.vitalityActors.size()<<",\"vitalityPermissionCount\":"<<level.vitalityPermissions.size()
        <<",\"vitalityRegionCount\":"<<level.vitalityRegions.size()<<",\"maxVitalityOperationsPerTick\":"<<level.maxVitalityOperationsPerTick
        <<",\"vitalityRespawnRecovery\":"<<(level.vitalityRespawnRecovery?"true":"false")
        <<",\"vitalityPeriodicRegions\":"<<(level.periodicVitalityRegions?"true":"false")
        <<",\"attackOwnerCount\":"<<level.attackOwners.size()
        <<",\"attackCancelOnRecovery\":"<<(level.attackCancelOnRecovery?"true":"false")
        <<",\"brainOwnerCount\":"<<level.brainOwners.size()
        <<",\"brainLineOfSight\":"<<(level.brainLineOfSight?"true":"false")
        <<",\"controlOwnerCount\":"<<level.characterControlOwners.size()
        <<",\"actors\":[";
    bool first=true;
    for(const auto& actor:level.actors) {
        if(!first)std::cout<<',';first=false;
        std::cout<<"{\"id\":"<<json(std::to_string(actor.id))<<",\"positionBits\":["
            <<json(bits(actor.position.x.bits()))<<','<<json(bits(actor.position.y.bits()))
            <<"],\"halfExtentBits\":["<<json(bits(actor.body.half.x.bits()))<<','<<json(bits(actor.body.half.y.bits()))
            <<"],\"offsetBits\":["<<json(bits(actor.body.offset.x.bits()))<<','<<json(bits(actor.body.offset.y.bits()))
            <<"],\"mode\":"<<static_cast<std::uint32_t>(actor.body.mode)<<",\"shape\":"
            <<static_cast<std::uint32_t>(actor.body.shape)<<",\"character\":"<<(actor.character?"true":"false")
            <<",\"trigger\":"<<(actor.body.trigger?"true":"false")<<",\"layer\":"<<actor.body.layer
            <<",\"mask\":"<<actor.body.mask<<",\"path\":"<<actor.path<<'}';
    }
    std::cout<<"],\"paths\":[";first=true;
    for(const auto& path:level.paths){if(!first)std::cout<<',';first=false;
        std::cout<<"{\"id\":"<<path.id<<",\"playback\":"<<static_cast<std::uint32_t>(path.playback)
            <<",\"movingTrigger\":"<<(path.movingTrigger?"true":"false")<<",\"waypoints\":[";bool pointFirst=true;
        for(const auto& point:path.waypoints){if(!pointFirst)std::cout<<',';pointFirst=false;
            std::cout<<"{\"offsetBits\":["<<json(bits(point.offset.x.bits()))<<','<<json(bits(point.offset.y.bits()))
                <<"],\"travelTicks\":"<<point.travelTicks<<",\"dwellTicks\":"<<point.dwellTicks<<'}';}
        std::cout<<"]}";}
    std::cout<<"],\"rules\":[";first=true;
    for(const auto& rule:level.rules) {
        if(!first)std::cout<<',';first=false;
        std::cout<<"{\"id\":"<<rule.id<<",\"trigger\":"<<json(std::to_string(rule.trigger))
            <<",\"actor\":"<<json(std::to_string(rule.actor))<<",\"phase\":"<<static_cast<std::uint32_t>(rule.phase)
            <<",\"target\":"<<json(std::to_string(rule.action.target))<<",\"kind\":"<<static_cast<std::uint32_t>(rule.action.kind)
            <<",\"once\":"<<(rule.once?"true":"false")<<",\"cooldownTicks\":"<<rule.cooldownTicks
            <<",\"delayTicks\":"<<rule.delayTicks<<'}';
    }
    std::cout<<"],\"counters\":[";first=true;
    for(const auto& counter:level.counters){if(!first)std::cout<<',';first=false;
        std::cout<<"{\"id\":"<<counter.id<<",\"minimum\":"<<json(std::to_string(counter.minimum))
            <<",\"maximum\":"<<json(std::to_string(counter.maximum))<<",\"initial\":"<<json(std::to_string(counter.initial))<<'}';}
    std::cout<<"],\"objectives\":[";first=true;
    for(const auto& objective:level.objectives){if(!first)std::cout<<',';first=false;
        std::cout<<"{\"id\":"<<objective.id<<",\"conditions\":[";bool firstCondition=true;
        for(const auto& condition:objective.conditions){if(!firstCondition)std::cout<<',';firstCondition=false;
            std::cout<<"{\"counter\":"<<condition.counter<<",\"atLeast\":"<<json(std::to_string(condition.atLeast))<<'}';}
        std::cout<<"]}";}
    std::cout<<"],\"claims\":[";first=true;
    for(const auto& claim:level.claims){if(!first)std::cout<<',';first=false;
        std::cout<<"{\"id\":"<<claim.id<<",\"trigger\":"<<json(std::to_string(claim.trigger))
            <<",\"actor\":"<<json(std::to_string(claim.actor))<<",\"counter\":"<<claim.counter
            <<",\"amount\":"<<json(std::to_string(claim.amount))<<'}';}
    std::cout<<"],\"flowBindings\":[";first=true;
    for(const auto& binding:level.flowBindings){if(!first)std::cout<<',';first=false;
        std::cout<<"{\"claim\":"<<binding.claim<<",\"requiredObjective\":"<<binding.requiredObjective<<'}';}
    std::cout<<"],\"objectiveActions\":[";first=true;
    for(const auto& rule:level.objectiveActions){if(!first)std::cout<<',';first=false;
        std::cout<<"{\"id\":"<<rule.id<<",\"objective\":"<<rule.objective<<",\"target\":"<<json(std::to_string(rule.action.target))
            <<",\"kind\":"<<static_cast<std::uint32_t>(rule.action.kind)<<",\"once\":"<<(rule.once?"true":"false")<<'}';}
    std::cout<<"],\"spawns\":[";first=true;
    for(const auto& spawn:level.respawnSpawns){if(!first)std::cout<<',';first=false;
        std::cout<<"{\"id\":"<<spawn.id<<",\"actor\":"<<json(std::to_string(spawn.actor))
            <<",\"positionBits\":["<<json(bits(spawn.position.x.bits()))<<','<<json(bits(spawn.position.y.bits()))<<"]}";}
    std::cout<<"],\"respawnBindings\":[";first=true;
    for(const auto& binding:level.respawnBindings){if(!first)std::cout<<',';first=false;
        std::cout<<"{\"actor\":"<<json(std::to_string(binding.actor))<<",\"spawn\":"<<binding.spawn<<'}';}
    std::cout<<"],\"respawnRegions\":[";first=true;
    for(const auto& region:level.respawnRegions){if(!first)std::cout<<',';first=false;
        std::cout<<"{\"id\":"<<region.id<<",\"trigger\":"<<json(std::to_string(region.trigger))
            <<",\"actor\":"<<json(std::to_string(region.actor))<<",\"kind\":"<<static_cast<std::uint32_t>(region.kind)
            <<",\"spawn\":"<<region.spawn<<'}';}
    std::cout<<"],\"vitalityActors\":[";first=true;
    for(const auto& actor:level.vitalityActors){if(!first)std::cout<<',';first=false;
        std::cout<<"{\"actor\":"<<json(std::to_string(actor.actor))<<",\"counter\":"<<actor.counter
            <<",\"maximumHP\":"<<json(std::to_string(actor.maximumHP))<<",\"initialHP\":"<<json(std::to_string(actor.initialHP))
            <<",\"invulnerabilityTicks\":"<<actor.invulnerabilityTicks<<",\"restoredHP\":"<<json(std::to_string(actor.restoredHP))
            <<",\"recoverManual\":"<<(actor.recoverManual?"true":"false")<<'}';}
    std::cout<<"],\"vitalityPermissions\":[";first=true;
    for(const auto& permission:level.vitalityPermissions){if(!first)std::cout<<',';first=false;
        std::cout<<"{\"id\":"<<permission.id<<",\"actor\":"<<json(std::to_string(permission.actor))<<",\"kind\":"<<static_cast<std::uint32_t>(permission.kind)
            <<",\"maxAmount\":"<<json(std::to_string(permission.maxAmount))<<'}';}
    std::cout<<"],\"vitalityRegions\":[";first=true;
    for(const auto& region:level.vitalityRegions){if(!first)std::cout<<',';first=false;
        std::cout<<"{\"id\":"<<region.id<<",\"trigger\":"<<json(std::to_string(region.trigger))<<",\"actor\":"<<json(std::to_string(region.actor))
            <<",\"kind\":"<<static_cast<std::uint32_t>(region.kind)<<",\"amount\":"<<json(std::to_string(region.amount))
            <<",\"periodTicks\":"<<region.periodTicks<<'}';}
    std::cout<<"],\"attacks\":[";first=true;
    for(const auto& owner:level.attackOwners){if(!first)std::cout<<',';first=false;
        std::cout<<"{\"actor\":"<<json(std::to_string(owner.actor))<<",\"halfExtentBits\":["
            <<json(bits(owner.halfExtent.x.bits()))<<','<<json(bits(owner.halfExtent.y.bits()))
            <<"],\"offsetBits\":["<<json(bits(owner.offset.x.bits()))<<','<<json(bits(owner.offset.y.bits()))
            <<"],\"warmupTicks\":"<<owner.warmupTicks<<",\"activeTicks\":"<<owner.activeTicks
            <<",\"recoveryTicks\":"<<owner.recoveryTicks<<",\"targets\":[";bool targetFirst=true;
        for(const auto& target:owner.targets){if(!targetFirst)std::cout<<',';targetFirst=false;
            std::cout<<"{\"actor\":"<<json(std::to_string(target.actor))<<",\"permission\":"<<target.permission
                <<",\"amount\":"<<json(std::to_string(target.amount))<<'}';}
        std::cout<<"]}";}
    std::cout<<"],\"brains\":[";first=true;
    for(const auto& owner:level.brainOwners){if(!first)std::cout<<',';first=false;
        std::cout<<"{\"actor\":"<<json(std::to_string(owner.actor))<<",\"target\":"<<json(std::to_string(owner.target))
            <<",\"patrolLeftBits\":"<<json(bits(owner.patrolLeft.bits()))<<",\"patrolRightBits\":"<<json(bits(owner.patrolRight.bits()))
            <<",\"patrolSpeedBits\":"<<json(bits(owner.patrolSpeed.bits()))<<",\"chaseSpeedBits\":"<<json(bits(owner.chaseSpeed.bits()))
            <<",\"senseHalfExtentBits\":["<<json(bits(owner.senseHalfExtent.x.bits()))<<','<<json(bits(owner.senseHalfExtent.y.bits()))
            <<"],\"attackDistanceBits\":"<<json(bits(owner.attackDistance.bits()))<<",\"initialRight\":"<<(owner.initialRight?"true":"false")<<'}';}
    std::cout<<"],\"controls\":[";first=true;
    for(const auto& owner:level.characterControlOwners){if(!first)std::cout<<',';first=false;
        std::cout<<"{\"actor\":"<<json(std::to_string(owner.actor))<<",\"dashSpeedBits\":"<<json(bits(owner.dashSpeed.bits()))
            <<",\"durationTicks\":"<<owner.durationTicks<<",\"cooldownTicks\":"<<owner.cooldownTicks
            <<",\"groundOnly\":"<<(owner.groundOnly?"true":"false")<<",\"initialFacingRight\":"<<(owner.initialFacingRight?"true":"false")<<'}';}
    std::cout<<"]}\n";
}
int run(int argc,char** argv) {
    if(argc==2 && std::string_view(argv[1])=="--help") {
        std::cout<<"aydetlevel validate <level.dlt>\naydetlevel inspect <level.dlt>\n"
            "aydetlevel canonicalize <level.dlt> <fresh.dlt>\n"
            "aydetlevel float <finite-decimal>\n"
            "aydetlevel float-bits <eight-hex-digits>\n"
            "aydetlevel create <character|dash|terrain> <fresh.dlt>\n"
            "aydetlevel add-solid <source.dlt> <new-id> <aabb|slope-left|slope-right> <x> <y> <half-x> <half-y> <layer-u32> <mask-u32> <fresh.dlt>\n"
            "aydetlevel add-path <source.dlt> <actor-id> <new-path-u32> <dx> <dy> <travel-ticks> <once|loop|pingpong> <fresh.dlt>\n"
            "aydetlevel bind-path <source.dlt> <actor-id> <existing-path-u32> <fresh.dlt>\n"
            "aydetlevel edit-waypoint <source.dlt> <path-u32> <index0> <dx> <dy> <travel-ticks> <dwell-ticks> <fresh.dlt>\n"
            "aydetlevel stamp-solids <donor.dlt> <target.dlt> <shift-x> <shift-y> <old:new[,old:new...]> <fresh.dlt>\n"
            "aydetlevel preview <level.dlt> <fresh.svg>\n"
            "aydetlevel place <source.dlt> <actor-id> <x> <y> <fresh.dlt>\n"
            "aydetlevel resize-half <source.dlt> <actor-id> <half-x> <half-y> <fresh.dlt>\n"
            "Validates the real initial Session pipeline. Canonicalize preserves exact Float32 bits.\n"
            "Starters use application/input1, fixed1/64 and require a matching application input recipe.\n"
            "Preview shows authored initial shapes/paths only; no simulation ticks or current CP.\n"
            "Output must be fresh; failed physical writes may leave a partial new file.\n";
        return 0;
    }
    require(argc>=3,"Use aydetlevel --help for commands");
    const std::string_view command=argv[1];
    if(command=="preview") {
        require(argc==4,"preview requires a source level and a fresh SVG output path");
        const std::filesystem::path source=argv[2],destination=argv[3];ordinary(source);
        DetLevel2D level;std::string error;
        const bool loaded=loadDetLevel2D(source,level,error);require(loaded,error);
        // load performs the real initial Session preflight. Build every SVG byte
        // and the receipt before exclusively creating the destination; never tick.
        const auto preview=buildPreviewSvg2D(level);
        const auto config=detLevelSessionConfig(level);
        std::ostringstream receipt;
        receipt<<"{\"schema\":1,\"status\":\"passed\",\"command\":\"preview\",\"previewProfile\":1"
            <<",\"scope\":\"authored-initial-content\",\"simulationExecuted\":false"
            <<",\"source\":"<<json(source.generic_string())<<",\"output\":"<<json(destination.generic_string())
            <<",\"formatVersion\":"<<level.formatVersion<<",\"contentHash\":"<<json(std::to_string(config.contentHash))
            <<",\"actorCount\":"<<preview.actorCount<<",\"pathInstances\":"<<preview.pathInstances
            <<",\"waypointCount\":"<<preview.waypointCount<<",\"svgBytes\":"<<preview.svg.size()
            <<",\"worldBoundsBits\":["<<json(bits(preview.worldMin.x.bits()))<<','<<json(bits(preview.worldMin.y.bits()))
            <<','<<json(bits(preview.worldMax.x.bits()))<<','<<json(bits(preview.worldMax.y.bits()))<<"],\"viewBoxBits\":[";
        bool first=true;for(auto value:preview.viewBox){if(!first)receipt<<',';first=false;receipt<<json(bits(value.bits()));}
        receipt<<"],\"browserPixelsAuthoritative\":false}\n";
        const auto record=receipt.str();writeFresh(destination,preview.svg);std::cout<<record;return 0;
    }
    if(command=="place" || command=="resize-half") {
        require(argc==7,"place/resize-half requires source, actor ID, two finite decimals and fresh output");
        const std::filesystem::path source=argv[2];ordinary(source);
        DetLevel2D level;std::string error;require(loadDetLevel2D(source,level,error),error);
        const auto id=editActorId(argv[3]);const auto x=editDecimal(argv[4]),y=editDecimal(argv[5]);
        const auto found=std::find_if(level.actors.begin(),level.actors.end(),[id](const auto& actor){return actor.id==id;});
        require(found!=level.actors.end(),"Actor identity is not authored in this level");
        if(command=="place")found->position={x,y};
        else {
            require(x>ayt::math::DetFloat32{} && y>ayt::math::DetFloat32{},"Body half extents must be strictly positive");
            found->body.half={x,y};
        }
        require(validateDetLevel2D(level,error),error);const auto canonical=encodeDetLevel2D(level);
        const std::filesystem::path destination=argv[6];writeFresh(destination,canonical);
        inspect(level,destination);return 0;
    }
    // Place this branch before the existing generic create handler.
    if(command=="create" && argc>=3 && std::string_view(argv[2])=="terrain") {
        require(argc==4,"create terrain requires a fresh output path");
        auto level=terrainStarter();std::string error;
        const bool valid=validateDetLevel2D(level,error);require(valid,error);
        const auto canonical=encodeDetLevel2D(level);
        const std::filesystem::path destination=argv[3];writeFresh(destination,canonical);
        inspect(level,destination);return 0;
    }
    if(command=="add-solid") {
        // argv: program, command, source, ID, shape, X, Y, halfX, halfY,
        // layer, mask, destination. There are exactly twelve arguments.
        require(argc==12,"add-solid requires source, new actor ID, shape, position, half extents, layer, mask and fresh output");
        const std::filesystem::path source=argv[2];ordinary(source);
        DetLevel2D level;std::string error;
        const bool loaded=loadDetLevel2D(source,level,error);require(loaded,error);
        const auto id=editActorId(argv[3]);const auto shape=solidShape(argv[4]);
        const auto x=editDecimal(argv[5]),y=editDecimal(argv[6]);
        const auto halfX=editDecimal(argv[7]),halfY=editDecimal(argv[8]);
        const auto layer=solidWord(argv[9]),mask=solidWord(argv[10]);
        require(halfX>ayt::math::DetFloat32{} && halfY>ayt::math::DetFloat32{},
            "Solid half extents must be strictly positive");
        require(level.actors.size()<64,"Level actor count limit");
        if(shape!=DetCollisionShape2D::Aabb)
            require(level.character.collision.staticSlopes,
                "Slope solid requires the source's explicit staticSlopes policy; use create terrain for a new starter");
        const auto insertion=std::lower_bound(level.actors.begin(),level.actors.end(),id,
            [](const DetLevelActor2D& actor,SimEntityId identity){return actor.id<identity;});
        require(insertion==level.actors.end() || insertion->id!=id,"Actor identity is already authored in this level");
        DetLevelActor2D actor;actor.id=id;actor.position={x,y};
        actor.body.mode=DetBodyMode2D::Static;actor.body.shape=shape;
        actor.body.half={halfX,halfY};actor.body.layer=layer;actor.body.mask=mask;
        // Fresh ordinary solid: defaults keep offset/velocity/ignoredOneWay/path
        // zero, character/trigger/oneWay false. No authority table or policy edit.
        level.actors.insert(insertion,std::move(actor));
        // Layer0, represented bounds/area, configured maxBodies, actual complete
        // policy/role/reference/namespace budgets and initial matching penetration
        // remain the existing real Level pipeline's decision. Mask0 is legal.
        const bool valid=validateDetLevel2D(level,error);require(valid,error);
        const auto canonical=encodeDetLevel2D(level);
        const std::filesystem::path destination=argv[11];writeFresh(destination,canonical);
        inspect(level,destination);return 0;
    }

    if(command=="add-path") {
        // argv0, command, source, actor, new path, dx, dy, ticks, playback, output.
        // Level forbids orphan policies, so creation and binding are one edit.
        require(argc==10,"add-path requires source, actor ID, new path ID, relative offset, travel ticks, playback and fresh output");
        const std::filesystem::path source=argv[2];ordinary(source);
        DetLevel2D level;std::string error;
        const bool loaded=loadDetLevel2D(source,level,error);require(loaded,error);
        const auto actor=editActorId(argv[3]);
        const auto id=authorPathWord(argv[4],1,UINT32_MAX,"Path identity");
        const auto dx=editDecimal(argv[5]),dy=editDecimal(argv[6]);
        const auto ticks=authorPathWord(argv[7],1,65535,"Travel ticks");
        const auto playback=authorPathPlayback(argv[8]);
        require(level.paths.size()<8,"Level path count limit");
        const auto insertion=std::lower_bound(level.paths.begin(),level.paths.end(),id,
            [](const DetLevelPath2D& path,std::uint32_t identity){return path.id<identity;});
        require(insertion==level.paths.end() || insertion->id!=id,"Path identity is already authored in this level");
        DetLevelPath2D path;path.id=id;path.playback=playback;
        DetPathWaypoint2D first,last;first.travelTicks=ticks;last.travelTicks=ticks;
        last.offset={dx,dy};path.waypoints={first,last};
        // Both travel durations are explicit and equal; dwell defaults to0.
        // first.offset is canonical positive zero, not a parsed signed zero.
        authorBindOrdinaryPath(level,actor,path);
        level.paths.insert(insertion,std::move(path));
        const bool valid=validateDetLevel2D(level,error);require(valid,error);
        const auto canonical=encodeDetLevel2D(level);
        const std::filesystem::path destination=argv[9];writeFresh(destination,canonical);
        inspect(level,destination);return 0;
    }
    if(command=="bind-path") {
        require(argc==6,"bind-path requires source, actor ID, existing path ID and fresh output");
        const std::filesystem::path source=argv[2];ordinary(source);
        DetLevel2D level;std::string error;
        const bool loaded=loadDetLevel2D(source,level,error);require(loaded,error);
        const auto actor=editActorId(argv[3]);
        const auto path=authorPathWord(argv[4],1,UINT32_MAX,"Path identity");
        authorBindOrdinaryPath(level,actor,authorExistingPath(level,path));
        const bool valid=validateDetLevel2D(level,error);require(valid,error);
        const auto canonical=encodeDetLevel2D(level);
        const std::filesystem::path destination=argv[5];writeFresh(destination,canonical);
        inspect(level,destination);return 0;
    }
    if(command=="edit-waypoint") {
        // Index is zero-based. No insertion/removal, role switch or cursor import.
        require(argc==10,"edit-waypoint requires source, path ID, point index, relative offset, travel ticks, dwell ticks and fresh output");
        const std::filesystem::path source=argv[2];ordinary(source);
        DetLevel2D level;std::string error;
        const bool loaded=loadDetLevel2D(source,level,error);require(loaded,error);
        const auto id=authorPathWord(argv[3],1,UINT32_MAX,"Path identity");
        const auto index=authorPathWord(argv[4],0,23,"Waypoint index");
        const auto dx=editDecimal(argv[5]),dy=editDecimal(argv[6]);
        const auto ticks=authorPathWord(argv[7],1,65535,"Travel ticks");
        const auto dwell=authorPathWord(argv[8],0,65535,"Dwell ticks");
        auto& path=authorExistingPath(level,id);
        require(index<path.waypoints.size(),"Waypoint index is not authored in this path");
        if(index==0)require(dx.bits()==0 && dy.bits()==0,
            "The first waypoint offset must remain canonical positive zero");
        path.waypoints[index]={{dx,dy},ticks,dwell};
        // The existing movingTrigger role, actor bindings and every permission
        // table are retained. Shared path users all receive the edited policy.
        const bool valid=validateDetLevel2D(level,error);require(valid,error);
        const auto canonical=encodeDetLevel2D(level);
        const std::filesystem::path destination=argv[9];writeFresh(destination,canonical);
        inspect(level,destination);return 0;
    }

    if(command=="stamp-solids") {
        // argv0/command/donor/target/shiftX/shiftY/map/fresh = exactly8.
        require(argc==8,"stamp-solids requires donor, target, two finite shifts, old:new comma mappings and fresh output");
        const std::filesystem::path donorPath=argv[2],targetPath=argv[3];
        ordinary(donorPath);ordinary(targetPath);
        DetLevel2D donor,target;std::string error;
        const bool donorLoaded=loadDetLevel2D(donorPath,donor,error);require(donorLoaded,error);
        const bool targetLoaded=loadDetLevel2D(targetPath,target,error);require(targetLoaded,error);
        const ayt::math::DetVec2 shift{editDecimal(argv[4]),editDecimal(argv[5])};
        const auto mappings=stampMap(argv[6]);
        // Resolve/check every reference, role, new ID, dependency and software
        // translation before mutating even the local full target value.
        const auto additions=stampActors(donor,target,mappings,shift);
        target.actors.insert(target.actors.end(),additions.begin(),additions.end());
        std::sort(target.actors.begin(),target.actors.end(),[](const auto& first,const auto& second){return first.id<second.id;});
        // Donor policy is never transferred. Actual full destination factories
        // decide represented bounds/penetration/configured and literal budgets.
        const bool valid=validateDetLevel2D(target,error);require(valid,error);
        const auto canonical=encodeDetLevel2D(target);
        const std::filesystem::path destination=argv[7];writeFresh(destination,canonical);
        inspect(target,destination);return 0;
    }

    if(command=="create") {
        require(argc==4,"create requires a starter name and a fresh output path");
        auto level=starter(argv[2]);std::string error;
        require(validateDetLevel2D(level,error),error);
        const auto canonical=encodeDetLevel2D(level);
        const std::filesystem::path destination=argv[3];writeFresh(destination,canonical);
        inspect(level,destination);return 0;
    }
    if(command=="float-bits") {
        require(argc==3,"float-bits requires one finite binary32 bit token");const auto value=finiteBits(argv[2]);
        std::cout<<"{\"schema\":1,\"status\":\"passed\",\"command\":\"float-bits\",\"textProfile\":"
            <<ayt::math::kDetFloatTextProfileVersion<<",\"bits\":"<<json(bits(value.bits()))
            <<",\"exactDecimal\":"<<json(ayt::math::detFloat32ExactDecimal(value))<<"}\n";return 0;
    }
    if(command=="float"){
        require(argc==3,"float requires one finite decimal token");ayt::math::DetFloat32 value;
        require(ayt::math::parseDetFloat32Decimal(argv[2],value),"Invalid, overflowing or over-budget finite decimal");
        std::cout<<"{\"schema\":1,\"status\":\"passed\",\"command\":\"float\",\"textProfile\":"
            <<ayt::math::kDetFloatTextProfileVersion<<",\"bits\":"<<json(bits(value.bits()))
            <<",\"exactDecimal\":"<<json(ayt::math::detFloat32ExactDecimal(value))<<"}\n";return 0;
    }
    require((command=="validate" || command=="inspect")?argc==3:command=="canonicalize" && argc==4,
        "Unknown command or argument count; use --help");
    const std::filesystem::path source=argv[2];ordinary(source);
    DetLevel2D level;std::string error;require(loadDetLevel2D(source,level,error),error);
    if(command=="canonicalize") {
        const std::filesystem::path destination=argv[3];
        const auto canonical=encodeDetLevel2D(level);writeFresh(destination,canonical);
    }
    inspect(level,source);return 0;
}
}
int main(int argc,char** argv) {
    try{return run(argc,argv);}catch(const std::exception& error) {
        std::cerr<<"{\"schema\":1,\"status\":\"failed\",\"error\":"<<json(error.what())<<"}\n";return 1;
    }
}
