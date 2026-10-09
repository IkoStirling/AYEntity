#pragma once
// Internal ayreplay runner-protocol validation. This checks a bounded report,
// not the identity or honesty of application code that produced it.
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <utility>
#include <initializer_list>
#include <filesystem>
#include <fstream>

namespace ayt::entity::replay_tool {
struct RunnerReportValidation {
    bool valid=false,success=false;
    std::string result,error;
};
namespace runner_report {
inline std::size_t utf8Sequence(std::string_view text,std::size_t position) {
    const auto lead=static_cast<unsigned char>(text[position]);if(lead<0x80)return 1;
    unsigned count=0;std::uint32_t cp=0,minimum=0;
    if(lead>=0xc2 && lead<=0xdf){count=1;cp=lead&31;minimum=0x80;}
    else if(lead>=0xe0 && lead<=0xef){count=2;cp=lead&15;minimum=0x800;}
    else if(lead>=0xf0 && lead<=0xf4){count=3;cp=lead&7;minimum=0x10000;}
    else return 0;
    if(text.size()-position-1<count)return 0;
    for(unsigned n=1;n<=count;++n){const auto c=static_cast<unsigned char>(text[position+n]);if((c&0xc0)!=0x80)return 0;cp=(cp<<6)|(c&63);}
    return cp<minimum || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)?0:count+1;
}
enum class Kind {Null,Boolean,Number,String,Object,Array};
struct Value {
    Kind kind=Kind::Null;
    bool boolean=false;
    std::string text;
    std::map<std::string,Value> object;
    std::vector<Value> array;
};
class Parser {
    std::string_view input;
    std::size_t position=0,nodes=0;
    [[noreturn]] void fail(const char* why) const {
        throw std::runtime_error(std::string(why)+" at byte "+std::to_string(position));
    }
    void whitespace() {while(position<input.size() && (input[position]==' ' || input[position]=='\t' || input[position]=='\r' || input[position]=='\n'))++position;}
    bool consume(char c) {if(position<input.size() && input[position]==c){++position;return true;}return false;}
    static bool digit(char c) {return c>='0' && c<='9';}
    std::uint32_t hex4() {
        if(input.size()-position<4)fail("Truncated JSON Unicode escape");
        std::uint32_t value=0;
        for(unsigned i=0;i<4;++i){const char c=input[position++];value<<=4;
            if(c>='0' && c<='9')value+=c-'0';else if(c>='a' && c<='f')value+=c-'a'+10;
            else if(c>='A' && c<='F')value+=c-'A'+10;else fail("Invalid JSON Unicode escape");}
        return value;
    }
    static void utf8(std::string& out,std::uint32_t cp) {
        if(cp<=0x7f)out.push_back(static_cast<char>(cp));
        else if(cp<=0x7ff){out.push_back(static_cast<char>(0xc0|(cp>>6)));out.push_back(static_cast<char>(0x80|(cp&63)));}
        else if(cp<=0xffff){out.push_back(static_cast<char>(0xe0|(cp>>12)));out.push_back(static_cast<char>(0x80|((cp>>6)&63)));out.push_back(static_cast<char>(0x80|(cp&63)));}
        else {out.push_back(static_cast<char>(0xf0|(cp>>18)));out.push_back(static_cast<char>(0x80|((cp>>12)&63)));out.push_back(static_cast<char>(0x80|((cp>>6)&63)));out.push_back(static_cast<char>(0x80|(cp&63)));}
    }
    std::string string() {
        if(!consume('"'))fail("Expected JSON string");std::string out;
        while(position<input.size()) {
            const auto c=static_cast<unsigned char>(input[position++]);
            if(c=='"')return out;
            if(c<32)fail("Unescaped JSON control character");
            if(c!='\\'){out.push_back(static_cast<char>(c));continue;}
            if(position==input.size())fail("Truncated JSON escape");
            switch(input[position++]) {
            case '"':out.push_back('"');break;case '\\':out.push_back('\\');break;case '/':out.push_back('/');break;
            case 'b':out.push_back('\b');break;case 'f':out.push_back('\f');break;case 'n':out.push_back('\n');break;
            case 'r':out.push_back('\r');break;case 't':out.push_back('\t');break;
            case 'u':{
                auto cp=hex4();
                if(cp>=0xd800 && cp<=0xdbff){if(!consume('\\') || !consume('u'))fail("Unpaired JSON high surrogate");
                    const auto low=hex4();if(low<0xdc00 || low>0xdfff)fail("Invalid JSON surrogate pair");cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);}
                else if(cp>=0xdc00 && cp<=0xdfff)fail("Unpaired JSON low surrogate");
                utf8(out,cp);break;
            }
            default:fail("Invalid JSON escape");
            }
        }
        fail("Unterminated JSON string");
    }
    Value value(unsigned depth) {
        if(depth>16 || ++nodes>8192)fail("JSON depth/node budget");whitespace();
        if(position==input.size())fail("Missing JSON value");Value out;
        const char c=input[position];
        if(c=='{') {
            out.kind=Kind::Object;++position;whitespace();if(consume('}'))return out;
            for(;;){whitespace();const auto key=string();whitespace();if(!consume(':'))fail("Missing JSON object colon");
                auto child=value(depth+1);if(!out.object.emplace(key,std::move(child)).second)fail("Duplicate JSON object key");
                whitespace();if(consume('}'))return out;if(!consume(','))fail("Missing JSON object comma");}
        }
        if(c=='[') {
            out.kind=Kind::Array;++position;whitespace();if(consume(']'))return out;
            for(;;){out.array.push_back(value(depth+1));whitespace();if(consume(']'))return out;if(!consume(','))fail("Missing JSON array comma");}
        }
        if(c=='"'){out.kind=Kind::String;out.text=string();return out;}
        auto literal=[&](std::string_view text){if(input.substr(position,text.size())!=text)fail("Invalid JSON literal");position+=text.size();};
        if(c=='n'){literal("null");return out;}
        if(c=='t' || c=='f'){out.kind=Kind::Boolean;out.boolean=c=='t';literal(c=='t'?"true":"false");return out;}
        if(c=='-' || digit(c)) {
            out.kind=Kind::Number;const auto start=position;consume('-');
            if(consume('0')){if(position<input.size() && digit(input[position]))fail("Leading zero in JSON number");}
            else {if(position==input.size() || !digit(input[position]))fail("Invalid JSON number");while(position<input.size() && digit(input[position]))++position;}
            if(consume('.')){if(position==input.size() || !digit(input[position]))fail("Invalid JSON fraction");while(position<input.size() && digit(input[position]))++position;}
            if(consume('e') || consume('E')){if(!consume('+'))consume('-');if(position==input.size() || !digit(input[position]))fail("Invalid JSON exponent");while(position<input.size() && digit(input[position]))++position;}
            if(position-start>32)fail("JSON number token budget");out.text=std::string(input.substr(start,position-start));return out;
        }
        fail("Invalid JSON value");
    }
    void validateUtf8() const {
        for(std::size_t i=0;i<input.size();) {
            const auto length=utf8Sequence(input,i);if(!length)throw std::runtime_error("Invalid UTF-8 JSON report");i+=length;
        }
    }
public:
    explicit Parser(std::string_view text):input(text){}
    Value parse() {
        if(input.empty() || input.size()>65536)throw std::runtime_error("Runner report byte budget (1..65536)");
        validateUtf8();auto result=value(1);whitespace();if(position!=input.size())fail("Trailing JSON content");return result;
    }
};
inline const Value& field(const Value& object,std::string_view key,Kind kind) {
    if(object.kind!=Kind::Object)throw std::runtime_error("Expected runner report object");
    const auto it=object.object.find(std::string(key));if(it==object.object.end())throw std::runtime_error("Missing runner report field: "+std::string(key));
    if(it->second.kind!=kind)throw std::runtime_error("Invalid runner report type: "+std::string(key));return it->second;
}
inline const Value& nullable(const Value& object,std::string_view key,Kind kind) {
    if(object.kind!=Kind::Object)throw std::runtime_error("Expected runner report object");
    const auto it=object.object.find(std::string(key));if(it==object.object.end())throw std::runtime_error("Missing runner report field: "+std::string(key));
    if(it->second.kind!=Kind::Null && it->second.kind!=kind)throw std::runtime_error("Invalid nullable runner report type: "+std::string(key));return it->second;
}
inline std::uint64_t decimal(std::string_view text) {
    if(text.empty() || text.size()>20 || (text.size()>1 && text.front()=='0'))throw std::runtime_error("Expected canonical unsigned decimal integer");
    std::uint64_t result=0;
    for(char c:text){if(c<'0' || c>'9' || result>(std::numeric_limits<std::uint64_t>::max()-static_cast<unsigned>(c-'0'))/10)throw std::runtime_error("Unsigned decimal integer overflow/type");result=result*10+static_cast<unsigned>(c-'0');}
    return result;
}
inline std::uint64_t integer(const Value& object,std::string_view key,std::uint64_t maximum=std::numeric_limits<std::uint64_t>::max()) {
    const auto result=decimal(field(object,key,Kind::Number).text);if(result>maximum)throw std::runtime_error("Runner report integer out of range: "+std::string(key));return result;
}
inline std::uint64_t hash(const Value& object,std::string_view key) {return decimal(field(object,key,Kind::String).text);}
inline void recovery(const Value& value) {
    if(value.kind==Kind::Null)return;
    const auto reason=integer(value,"reason",4),ordinal=integer(value,"stopOrdinal",65536);
    if(!reason || !ordinal)throw std::runtime_error("Invalid recovered-prefix report marker");
}
inline void details(const Value& object) {
    const auto& difference=nullable(object,"difference",Kind::Object);
    if(difference.kind!=Kind::Null){integer(difference,"tick");hash(difference,"entity");integer(difference,"component",UINT32_MAX);integer(difference,"field",UINT32_MAX);integer(difference,"lane",UINT32_MAX);
        if(field(difference,"section",Kind::String).text.empty())throw std::runtime_error("Empty state difference section");hash(difference,"expected");hash(difference,"actual");}
    const auto& input=nullable(object,"inputDifference",Kind::Object);
    if(input.kind!=Kind::Null){integer(input,"source",UINT32_MAX);integer(input,"sequence",UINT32_MAX);hash(input,"expected");hash(input,"actual");
        if(field(input,"field",Kind::String).text.empty())throw std::runtime_error("Empty input difference field");
        const auto& offset=nullable(input,"payloadOffset",Kind::Number);if(offset.kind!=Kind::Null)decimal(offset.text);}
    const auto& issue=nullable(object,"issue",Kind::Object);
    if(issue.kind!=Kind::Null){field(issue,"path",Kind::String);const auto& offset=nullable(issue,"offset",Kind::Number);if(offset.kind!=Kind::Null)decimal(offset.text);}
    recovery(nullable(object,"leftRecovery",Kind::Object));recovery(nullable(object,"rightRecovery",Kind::Object));
}
} // namespace runner_report
/// Captured child output is a byte stream. Preserve valid UTF-8, replace each
/// invalid byte with U+FFFD, then quote it as a valid JSON string.
inline std::string quoteRunnerBytes(std::string_view bytes) {
    static constexpr char hex[]="0123456789abcdef";std::string out="\"";
    for(std::size_t i=0;i<bytes.size();) {
        const auto c=static_cast<unsigned char>(bytes[i]);
        if(c=='"' || c=='\\'){out+='\\';out+=static_cast<char>(c);++i;}
        else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];++i;}
        else {const auto length=runner_report::utf8Sequence(bytes,i);if(length){out.append(bytes.substr(i,length));i+=length;}else{out+="\xef\xbf\xbd";++i;}}
    }
    out+='"';return out;
}
inline RunnerReportValidation validateRunnerReport(std::string_view json,std::string_view expectedSource,int exitCode) {
    using namespace runner_report;RunnerReportValidation result;
    try {
        const auto root=Parser(json).parse();if(integer(root,"schema")!=1)throw std::runtime_error("Unsupported runner report schema");
        result.success=field(root,"success",Kind::Boolean).boolean;result.result=field(root,"result",Kind::String).text;
        const auto& error=field(root,"error",Kind::String).text;
        if(field(root,"left",Kind::String).text!=expectedSource)throw std::runtime_error("Runner report source does not match requested source");
        if(!field(root,"right",Kind::String).text.empty())throw std::runtime_error("Runner execution report has a comparison source");
        integer(root,"epoch",UINT32_MAX);for(const auto* key:{"tick","nextTick","record","ticksExecuted","eventsChecked","trustedRecoveryEvents"})integer(root,key);
        const auto records=integer(root,"recordsChecked"),skipped=integer(root,"skippedTicks");
        for(const auto* key:{"manifestHash","rightManifestHash","leftRecordHash","rightRecordHash","leftBeforeRecordHash","rightBeforeRecordHash"})hash(root,key);
        const bool leftPresent=field(root,"leftRecordPresent",Kind::Boolean).boolean;
        if(field(root,"rightRecordPresent",Kind::Boolean).boolean || hash(root,"rightManifestHash") || hash(root,"rightRecordHash") || hash(root,"rightBeforeRecordHash"))throw std::runtime_error("Runner execution report has right-side record identity");
        details(root);
        if(nullable(root,"rightRecovery",Kind::Object).kind!=Kind::Null)throw std::runtime_error("Runner execution report has right-side recovery identity");
        const bool successful=result.result=="verified" || result.result=="verified-with-gaps";
        const bool failure=result.result=="manifest-mismatch" || result.result=="initial-state-mismatch" || result.result=="input-mismatch" || result.result=="state-mismatch" || result.result=="event-mismatch" || result.result=="recovery-mismatch" || result.result=="range-mismatch" || result.result=="invalid-file" || result.result=="execution-failed" || result.result=="budget-exceeded";
        if(!successful && !failure)throw std::runtime_error("Unknown/non-executing runner report result");
        if(result.success!=successful)throw std::runtime_error("Runner report success/result disagreement");
        if(successful){if(!records || !leftPresent || !error.empty() || nullable(root,"difference",Kind::Object).kind!=Kind::Null || nullable(root,"inputDifference",Kind::Object).kind!=Kind::Null || nullable(root,"issue",Kind::Object).kind!=Kind::Null)throw std::runtime_error("Incomplete or contradictory successful runner report");
            if((result.result=="verified-with-gaps")!=(skipped!=0))throw std::runtime_error("Runner report gap/result disagreement");}
        if(exitCode!=0 && exitCode!=1)throw std::runtime_error("Runner exit code is not an execution report result (0/1)");
        if((exitCode==0)!=result.success)throw std::runtime_error("Runner exit code/report success disagreement");
        result.valid=true;
    }catch(const std::exception& e){result.error=e.what();}
    return result;
}
struct RunnerReportRead {
    bool present=false;
    RunnerReportValidation protocol;
};
/// File metadata/read failures are protocol diagnostics after child completion.
/// This does not promise recovery from allocation failure or concurrent mutation.
inline RunnerReportRead readRunnerReport(const std::filesystem::path& path,std::string_view expectedSource,int exitCode) {
    RunnerReportRead result;
    try {
        namespace fs=std::filesystem;std::error_code error;
        const auto status=fs::symlink_status(path,error);
        if(error){result.protocol.error="Runner report metadata failed: "+error.message();return result;}
        if(fs::is_symlink(status) || !fs::is_regular_file(status)){result.protocol.error="Runner has no ordinary non-symlink report.json";return result;}
        const auto bytes=fs::file_size(path,error);
        if(error){result.protocol.error="Runner report size failed: "+error.message();return result;}
        if(bytes>65536){result.protocol.error="Runner report exceeded byte budget";return result;}
        result.present=true;
        std::ifstream input(path,std::ios::binary);std::string json(65537,'\0');
        input.read(json.data(),static_cast<std::streamsize>(json.size()));json.resize(static_cast<std::size_t>(input.gcount()));
        if(input.bad() || !input.eof())result.protocol.error="Runner report read failed/exceeded byte budget";
        else result.protocol=validateRunnerReport(json,expectedSource,exitCode);
    }catch(const std::exception& e){result.protocol.valid=false;result.protocol.error="Runner report access failed: "+std::string(e.what());}
    return result;
}
} // namespace ayt::entity::replay_tool
