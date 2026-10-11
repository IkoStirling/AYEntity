// Content authoring boundary: no file access or native float conversion in Sim.
#include <AYEntity/DeterministicLevel2D.h>
#include <AYMath/DetFloatText.h>
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
            "aydetlevel create <character|dash> <fresh.dlt>\n"
            "Validates the real initial Session pipeline. Canonicalize preserves exact Float32 bits.\n"
            "Starters use application/input1, fixed1/64 and require a matching application input recipe.\n"
            "Output must be fresh; failed physical writes may leave a partial new file.\n";
        return 0;
    }
    require(argc>=3,"Use aydetlevel --help for commands");
    const std::string_view command=argv[1];
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
