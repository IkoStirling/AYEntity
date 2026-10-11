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
    std::cout<<"]}\n";
}
int run(int argc,char** argv) {
    if(argc==2 && std::string_view(argv[1])=="--help") {
        std::cout<<"aydetlevel validate <level.dlt>\naydetlevel inspect <level.dlt>\n"
            "aydetlevel canonicalize <level.dlt> <fresh.dlt>\n"
            "aydetlevel float <finite-decimal>\n"
            "Validates the real initial Session pipeline. Canonicalize preserves exact Float32 bits.\n"
            "No input overwrite, arbitrary Scene import, runtime hot reload or cook is performed.\n";
        return 0;
    }
    require(argc>=3,"Use aydetlevel --help for commands");
    const std::string_view command=argv[1];
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
        require(!std::filesystem::exists(destination) && !std::filesystem::is_symlink(destination),"Output must be fresh");
        const auto canonical=encodeDetLevel2D(level);
        std::ofstream output(destination,std::ios::binary|std::ios::out|std::ios::noreplace);
        require(bool(output),"Cannot exclusively create output");
        output.write(canonical.data(),static_cast<std::streamsize>(canonical.size()));output.flush();
        const bool wrote=bool(output);output.close();require(wrote && bool(output),"Canonical output failed; partial fresh file may remain");
    }
    inspect(level,source);return 0;
}
}
int main(int argc,char** argv) {
    try{return run(argc,argv);}catch(const std::exception& error) {
        std::cerr<<"{\"schema\":1,\"status\":\"failed\",\"error\":"<<json(error.what())<<"}\n";return 1;
    }
}
