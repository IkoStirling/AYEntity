// Content authoring boundary: no file access or native float conversion in Sim.
#include <AYEntity/DeterministicLevel2D.h>
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
    std::cout<<"]}\n";
}
int run(int argc,char** argv) {
    if(argc==2 && std::string_view(argv[1])=="--help") {
        std::cout<<"aydetlevel validate <level.dlt>\naydetlevel inspect <level.dlt>\n"
            "aydetlevel canonicalize <level.dlt> <fresh.dlt>\n"
            "Validates the real initial Session pipeline. Canonicalize preserves exact Float32 bits.\n"
            "No input overwrite, arbitrary Scene import, runtime hot reload or cook is performed.\n";
        return 0;
    }
    require(argc>=3,"Use aydetlevel --help for commands");
    const std::string_view command=argv[1];
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
