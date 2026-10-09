// A deliberately broken application protocol producer. No Session is executed.
// Invalid report modes must be rejected despite exit0. fake-output supplies a
// valid-shaped report only to exercise byte-safe wrapper JSON serialization.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <stdexcept>
namespace {
std::string quote(std::string_view text) {
    std::string result="\"";
    for(unsigned char c:text){if(c=='"' || c=='\\')result+='\\';result+=static_cast<char>(c);}return result+'"';
}
std::string replace(std::string text,std::string_view old,std::string_view value) {
    const auto where=text.find(old);if(where==text.npos)throw std::runtime_error("Fake report replacement missing");text.replace(where,old.size(),value);return text;
}
std::string shaped(std::string_view source) {
    return "{\"schema\":1,\"success\":true,\"result\":\"verified\",\"left\":"+quote(source)+R"(,"right":"","error":"","epoch":1,"tick":0,"nextTick":1,"record":1,"recordsChecked":2,"ticksExecuted":1,"eventsChecked":0,"skippedTicks":0,"trustedRecoveryEvents":0,"manifestHash":"0","rightManifestHash":"0","leftRecordHash":"0","rightRecordHash":"0","leftBeforeRecordHash":"0","rightBeforeRecordHash":"0","leftRecordPresent":true,"rightRecordPresent":false,"difference":null,"inputDifference":null,"issue":null,"leftRecovery":null,"rightRecovery":null})";
}
}
int main(int argc,char** argv){try {
    if(argc!=4 || std::string(argv[1])!="--ayreplay-verify")return 2;
    const std::filesystem::path output=argv[3];const auto mode=output.filename().string();std::string json;
    if(mode=="fake-empty")json="";
    else if(mode=="fake-object")json="{}";
    else if(mode=="fake-schema")json=replace(shaped(argv[2]),"\"schema\":1","\"schema\":2");
    else if(mode=="fake-source")json=shaped("unrelated-recording.rpi");
    else if(mode=="fake-equal")json=replace(shaped(argv[2]),"\"verified\"","\"equal\"");
    else if(mode=="fake-failure")json=replace(replace(shaped(argv[2]),"\"success\":true","\"success\":false"),"\"verified\"","\"state-mismatch\"");
    else if(mode=="fake-duplicate"){json=shaped(argv[2]);json.insert(json.size()-1,R"(,"\u0073chema":1)");}
    else if(mode=="fake-trailing")json=shaped(argv[2])+"{}";
    else if(mode=="fake-utf8"){json=shaped(argv[2]);json.insert(json.size()-1,std::string(",\"unknown\":\"")+std::string("\xc0\xaf",2)+"\"");}
    else if(mode=="fake-oversize")json=std::string(65537,' ');
    else if(mode=="fake-output")json=shaped(argv[2]);
    else if(mode=="fake-directory")json="";
    else throw std::runtime_error("Unknown fake runner output mode");
    if(!std::filesystem::create_directory(output))throw std::runtime_error("Fake runner needs a new directory");
    if(mode=="fake-directory"){std::filesystem::create_directory(output/"report.json");return 0;}
    std::ofstream file(output/"report.json",std::ios::binary);file.write(json.data(),static_cast<std::streamsize>(json.size()));file.close();
    if(!file)throw std::runtime_error("Fake runner report write failed");
    if(mode=="fake-output"){const char invalid=static_cast<char>(0xff);std::cout.write(&invalid,1);std::cout.flush();}
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 3;}}
