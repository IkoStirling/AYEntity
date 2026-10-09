// Internal protocol tests are independent of Session execution and JSON libraries.
#include "../../tools/detreplay/detail/RunnerReport.h"
#include <iostream>
#include <stdexcept>
#include <chrono>
using ayt::entity::replay_tool::validateRunnerReport;
using ayt::entity::replay_tool::quoteRunnerBytes;
namespace {
std::string good() {return R"({"schema":1,"success":true,"result":"verified","left":"source.rpi","right":"","error":"","epoch":1,"tick":0,"nextTick":1,"record":1,"recordsChecked":2,"ticksExecuted":1,"eventsChecked":0,"skippedTicks":0,"trustedRecoveryEvents":0,"manifestHash":"0","rightManifestHash":"0","leftRecordHash":"0","rightRecordHash":"0","leftBeforeRecordHash":"0","rightBeforeRecordHash":"0","leftRecordPresent":true,"rightRecordPresent":false,"difference":null,"inputDifference":null,"issue":null,"leftRecovery":null,"rightRecovery":null})";}
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string change(std::string text,const std::string& before,const std::string& after) {
    const auto position=text.find(before);require(position!=text.npos,"test replacement exists");text.replace(position,before.size(),after);return text;
}
std::string extra(std::string text,const std::string& fields){text.insert(text.size()-1,","+fields);return text;}
void reject(const std::string& json,const char* reason,int exit=0,const std::string& source="source.rpi") {
    const auto report=validateRunnerReport(json,source,exit);require(!report.valid && !report.error.empty(),reason);
}
}
int main(int argc,char** argv){try {
    const auto baseline=good();require(validateRunnerReport(baseline,"source.rpi",0).valid,"complete report accepted, hash0 present");
    require(validateRunnerReport(" \n"+baseline+"\t ","source.rpi",0).valid,"JSON whitespace accepted");
    require(validateRunnerReport(extra(baseline,R"("future":{"array":[null,true,false,-1.25e+2,"\u4e2d\u6587\ud83d\ude03"]})"),"source.rpi",0).valid,"unknown bounded fields and paired Unicode accepted");
    auto gap=change(change(baseline,"\"verified\"","\"verified-with-gaps\""),"\"skippedTicks\":0","\"skippedTicks\":3");
    gap=change(gap,"\"leftRecovery\":null",R"("leftRecovery":{"reason":1,"stopOrdinal":5})");require(validateRunnerReport(gap,"source.rpi",0).valid,"trusted gap/recovered prefix success accepted");
    auto failed=change(change(change(baseline,"\"success\":true","\"success\":false"),"\"verified\"","\"invalid-file\""),"\"error\":\"\"","\"error\":\"Missing input\"");
    failed=change(change(failed,"\"recordsChecked\":2","\"recordsChecked\":0"),"\"leftRecordPresent\":true","\"leftRecordPresent\":false");
    failed=change(failed,"\"issue\":null",R"("issue":{"path":"","offset":null})");require(validateRunnerReport(failed,"source.rpi",1).valid,"early failure with zero checked records/unknown issue accepted");
    auto state=change(change(change(baseline,"\"success\":true","\"success\":false"),"\"verified\"","\"state-mismatch\""),"\"error\":\"\"","\"error\":\"Different state\"");
    state=change(state,"\"difference\":null",R"("difference":{"tick":1,"entity":"18446744073709551615","component":3,"field":5,"lane":0,"section":"state","expected":"0","actual":"1"})");
    require(validateRunnerReport(state,"source.rpi",1).valid,"full state difference accepted");
    require(validateRunnerReport(change(baseline,"\"manifestHash\":\"0\"","\"manifestHash\":\"18446744073709551615\""),"source.rpi",0).valid,"full u64 hash accepted");
    const std::string unicodeSource="\xe4\xb8\xad\xe6\x96\x87";
    require(validateRunnerReport(change(baseline,"source.rpi",R"(\u4e2d\u6587)"),unicodeSource,0).valid,"escaped source identity decoded before comparison");
    const auto validLog=std::string("D:/safe & spaced/")+unicodeSource+"/\xf0\x9f\x98\x83\n\"\\\t";
    const auto quoted=quoteRunnerBytes(validLog);
    require(ayt::entity::replay_tool::runner_report::Parser(quoted).parse().text==validLog,"quoted ASCII paths/control bytes and valid Unicode preserved");
    require(quoted.find(unicodeSource)!=quoted.npos && quoted.find("\xf0\x9f\x98\x83")!=quoted.npos,"valid UTF8 stays unchanged in wrapper JSON");
    const auto invalidLog=std::string("before ")+std::string("\xff\xc0\xaf",3)+" after";
    require(ayt::entity::replay_tool::runner_report::Parser(quoteRunnerBytes(invalidLog)).parse().text=="before \xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd after","invalid captured bytes replaced with U+FFFD and wrapper remains JSON");
    reject("","empty report rejected");reject("{}","empty object rejected");reject("[]","root array rejected");reject("null","root null rejected");
    reject(baseline+"{}","trailing object rejected");reject(change(baseline,"\"schema\":1","\"schema\":2"),"future schema rejected");
    reject(change(baseline,"\"schema\":1","\"schema\":1.0"),"schema must be integer");reject(change(baseline,"\"schema\":1","\"schema\":1e0"),"exponential schema rejected");
    reject(change(baseline,"\"schema\":1","\"schema\":01"),"leading number zero rejected");reject(change(baseline,"\"schema\":1","\"schema\":+1"),"leading plus rejected");
    reject(extra(baseline,"\"unknown\":1."),"missing fraction digit rejected");reject(extra(baseline,"\"unknown\":1e+"),"missing exponent digit rejected");
    reject(extra(baseline,"\"unknown\":123456789012345678901234567890123"),"number token budget enforced");
    reject(change(baseline,"\"epoch\":1","\"epoch\":4294967296"),"u32 overflow rejected");reject(change(baseline,"\"tick\":0","\"tick\":18446744073709551616"),"u64 counter overflow rejected");
    reject(change(baseline,"\"tick\":0","\"tick\":-1"),"negative counter rejected");reject(change(baseline,"\"manifestHash\":\"0\"","\"manifestHash\":0"),"hash must be string");
    reject(change(baseline,"\"manifestHash\":\"0\"","\"manifestHash\":\"18446744073709551616\""),"u64 string overflow rejected");
    reject(change(baseline,"\"manifestHash\":\"0\"","\"manifestHash\":\"00\""),"noncanonical decimal string rejected");
    reject(change(baseline,"source.rpi","other.rpi"),"foreign source report rejected");reject(change(baseline,"\"right\":\"\"","\"right\":\"other.rpi\""),"comparison source rejected");
    reject(change(baseline,"\"verified\"","\"equal\""),"witness equality is not execution verification");
    reject(change(baseline,"\"success\":true","\"success\":false"),"success/result contradiction rejected");reject(baseline,"exit1/success contradiction rejected",1);reject(failed,"exit0/failure contradiction rejected");
    reject(baseline,"argument failure exit cannot claim execution",2);reject(baseline,"artifact failure exit cannot claim execution",3);
    reject(change(baseline,"\"verified\"","\"verified-with-gaps\""),"gaps require nonzero skipped ticks");reject(change(baseline,"\"skippedTicks\":0","\"skippedTicks\":1"),"plain verified cannot hide gap");
    reject(change(baseline,"\"recordsChecked\":2","\"recordsChecked\":0"),"success requires initial record");
    reject(change(baseline,"\"leftRecordPresent\":true","\"leftRecordPresent\":false"),"success requires source identity");
    reject(change(baseline,"\"rightRecordPresent\":false","\"rightRecordPresent\":true"),"execution cannot have right record");
    reject(extra(baseline,"\"schema\":1"),"duplicate known key rejected");reject(extra(baseline,R"("x":1,"\u0078":2)"),"duplicate decoded unknown key rejected");
    reject(extra(baseline,R"("unknown":{"a":1,"\u0061":2})"),"duplicate nested key rejected");
    reject(extra(baseline,R"("unknown":"\ud800")"),"unpaired high surrogate rejected");reject(extra(baseline,R"("unknown":"\udc00")"),"unpaired low surrogate rejected");
    reject(extra(baseline,R"("unknown":"\ud800\u0041")"),"invalid surrogate second half rejected");reject(extra(baseline,R"("unknown":"\u123z")"),"bad Unicode hex rejected");
    reject(extra(baseline,R"("unknown":"\q")"),"unknown escape rejected");reject(extra(baseline,"\"unknown\":\"line\nfeed\""),"raw control rejected");
    reject(extra(baseline,std::string("\"unknown\":\"")+std::string("\xc0\xaf",2)+"\""),"overlong UTF8 rejected");
    reject(extra(baseline,std::string("\"unknown\":\"")+std::string("\xed\xa0\x80",3)+"\""),"UTF8 surrogate rejected");
    reject(extra(baseline,std::string("\"unknown\":\"")+std::string("\xf4\x90\x80\x80",4)+"\""),"UTF8 above Unicode maximum rejected");
    auto nested=std::string("0");for(unsigned i=0;i<17;++i)nested="["+nested+"]";reject(extra(baseline,"\"unknown\":"+nested),"depth budget enforced");
    std::string many="[0";for(unsigned i=0;i<8192;++i)many+=",0";many+=']';reject(extra(baseline,"\"unknown\":"+many),"node budget enforced");
    reject(std::string(65537,' '),"input byte budget enforced");
    require(validateRunnerReport(extra(baseline,"\"unknown\":\""+std::string(60000,'a')+"\""),"source.rpi",0).valid,"large bounded string accepted");
    require(argc<=2,"at most one optional artifact directory");namespace fs=std::filesystem;
    const auto root=argc==2?fs::path(argv[1]):fs::temp_directory_path()/"ayreplay-runner-protocol";fs::create_directories(root);
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();fs::path work;
    for(unsigned attempt=0;attempt<32;++attempt){auto candidate=root/("report-read-"+std::to_string(stamp)+"-"+std::to_string(attempt));if(fs::create_directory(candidate)){work=std::move(candidate);break;}}
    require(!work.empty(),"fresh report read artifact directory");
    auto write=[&](const fs::path& path,const std::string& text){std::ofstream file(path,std::ios::binary);file.write(text.data(),static_cast<std::streamsize>(text.size()));file.close();require(bool(file),"report read fixture write");};
    auto read=[&](const fs::path& path){return ayt::entity::replay_tool::readRunnerReport(path,"source.rpi",0);};
    write(work/"report.json",baseline);const auto ordinary=read(work/"report.json");require(ordinary.present && ordinary.protocol.valid,"ordinary report file accepted");
    const auto missing=read(work/"missing.json");require(!missing.present && !missing.protocol.valid && !missing.protocol.error.empty(),"missing report becomes structured diagnostic");
    fs::create_directory(work/"directory.json");const auto directory=read(work/"directory.json");require(!directory.present && !directory.protocol.valid && !directory.protocol.error.empty(),"directory report becomes structured diagnostic");
    write(work/"empty.json","");const auto empty=read(work/"empty.json");require(empty.present && !empty.protocol.valid && !empty.protocol.error.empty(),"ordinary empty report has presence but invalid protocol");
    write(work/"oversize.json",std::string(65537,' '));const auto oversize=read(work/"oversize.json");require(!oversize.present && !oversize.protocol.valid && !oversize.protocol.error.empty(),"oversize report rejected before parsing");
    std::error_code fileError;fs::create_symlink("report.json",work/"link.json",fileError);
    if(!fileError){const auto link=read(work/"link.json");require(!link.present && !link.protocol.valid && !link.protocol.error.empty(),"symlink report becomes structured diagnostic");}
    else std::cout<<"SKIP report symlink fixture: "<<fileError.message()<<'\n';
#if !defined(_WIN32)
    write(work/"denied.json",baseline);fs::permissions(work/"denied.json",fs::perms::none,fs::perm_options::replace,fileError);
    if(!fileError){std::ifstream probe(work/"denied.json",std::ios::binary);
        if(!probe){const auto denied=read(work/"denied.json");require(denied.present && !denied.protocol.valid && !denied.protocol.error.empty(),"unreadable ordinary report becomes structured read diagnostic");}
        else std::cout<<"SKIP report permission denial: current identity can read mode000\n";
        fs::permissions(work/"denied.json",fs::perms::owner_read|fs::perms::owner_write,fs::perm_options::replace,fileError);
    }else std::cout<<"SKIP report permission fixture: "<<fileError.message()<<'\n';
#else
    std::cout<<"SKIP POSIX report mode000 permission fixture on Windows\n";
#endif
    std::cout<<"PASS strict runner report: execution identity, exit/result agreement, early failures, unknown fields, duplicate decoded keys, UTF8/UTF16 escapes, integer/depth/node/byte budgets and arbitrary-output JSON serialization\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
