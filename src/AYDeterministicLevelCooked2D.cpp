// design reference: AYEntity/design.md Stage45 bounded cooked level envelope.
#include <AYEntity/DeterministicLevelCooked2D.h>
#include <algorithm>
#include <stdexcept>

namespace ayt::entity {
namespace {
constexpr std::string_view magic="AYDLC001";
[[noreturn]] void invalid(const char* reason){throw std::invalid_argument(reason);}
std::uint64_t hash(std::span<const std::uint8_t> bytes){std::uint64_t value=UINT64_C(14695981039346656037);
    for(auto byte:bytes){value^=byte;value*=UINT64_C(1099511628211);}return value;}
void put(std::vector<std::uint8_t>& output,std::uint64_t value,unsigned width){
    for(unsigned n=0;n<width;++n)output.push_back(static_cast<std::uint8_t>(value>>(8*n)));}
std::uint64_t get(std::span<const std::uint8_t> bytes,std::size_t at,unsigned width){std::uint64_t value=0;
    for(unsigned n=0;n<width;++n)value|=std::uint64_t{bytes[at+n]}<<(8*n);return value;}
}
std::vector<std::uint8_t> encodeDetLevelCooked2D(const DetLevel2D& level){
    const auto text=encodeDetLevel2D(level);
    if(text.empty() || text.size()>kDetLevel2DMaxTextBytes)invalid("Cooked level payload size limit");
    const auto payload=std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(text.data()),text.size()};
    std::vector<std::uint8_t> result;result.reserve(kDetLevelCooked2DHeaderBytes+payload.size());
    for(auto ch:magic)result.push_back(static_cast<std::uint8_t>(ch));
    put(result,kDetLevelCooked2DFormatVersion,4);put(result,payload.size(),4);
    put(result,detLevelSessionConfig(level).contentHash,8);put(result,hash(payload),8);
    result.insert(result.end(),payload.begin(),payload.end());return result;
}
bool decodeDetLevelCooked2D(std::span<const std::uint8_t> bytes,DetLevel2D& output,std::string& error){try{
    if(bytes.size()<kDetLevelCooked2DHeaderBytes || bytes.size()>kDetLevelCooked2DMaxBytes)invalid("Cooked level envelope size limit");
    if(!std::equal(magic.begin(),magic.end(),bytes.begin()))invalid("Cooked level magic mismatch");
    if(get(bytes,8,4)!=kDetLevelCooked2DFormatVersion)invalid("Cooked level format version mismatch");
    const auto length=get(bytes,12,4);
    if(!length || length>kDetLevel2DMaxTextBytes)invalid("Cooked level payload size limit");
    if(bytes.size()!=kDetLevelCooked2DHeaderBytes+length)invalid("Cooked level exact length/EOF mismatch");
    const auto payload=bytes.subspan(kDetLevelCooked2DHeaderBytes);
    if(hash(payload)!=get(bytes,24,8))invalid("Cooked level payload checksum mismatch");
    const std::string_view text{reinterpret_cast<const char*>(payload.data()),payload.size()};
    DetLevel2D candidate;std::string reason;
    if(!decodeDetLevel2D(text,candidate,reason))throw std::invalid_argument("Cooked level source preflight: "+reason);
    const auto canonical=encodeDetLevel2D(candidate);
    if(canonical!=text)invalid("Cooked level payload is not canonical text");
    if(detLevelSessionConfig(candidate).contentHash!=get(bytes,16,8))invalid("Cooked level semantic content hash mismatch");
    output=std::move(candidate);error.clear();return true;
}catch(const std::exception& exception){error=exception.what();return false;}}
} // namespace ayt::entity
