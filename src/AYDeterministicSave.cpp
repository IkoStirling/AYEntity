// design reference: AYEntity/design.md; Stage38 bounded persistent checkpoint utility.
#include <AYEntity/DeterministicSave.h>
#include "detail/DetSessionWire.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace ayt::entity {
namespace {
static_assert(kDetSaveMaxCheckpointBytes==detwire::maxBytes,
    "Save envelope must follow the existing checkpoint bound");
constexpr std::array<std::uint8_t,8> magic{'A','Y','D','S','A','V','E','1'};
constexpr std::size_t minCheckpointBytes=16;
bool fail(std::string& error,const std::string& text){error=text;return false;}
std::uint32_t u32(const std::uint8_t* p){return std::uint32_t{p[0]} | (std::uint32_t{p[1]}<<8)
    | (std::uint32_t{p[2]}<<16) | (std::uint32_t{p[3]}<<24);}
void put32(std::uint8_t* p,std::uint32_t value){for(unsigned i=0;i<4;++i)p[i]=static_cast<std::uint8_t>(value>>(8*i));}
bool ordinary(const std::filesystem::path& path,std::string& error){
    std::error_code ec;const auto status=std::filesystem::symlink_status(path,ec);
    if(ec)return fail(error,"Save file status failed: "+ec.message());
    if(!std::filesystem::is_regular_file(status))return fail(error,"Save source must be an ordinary non-symlink file");
    return true;
}
bool fresh(const std::filesystem::path& path,std::string& error){
    std::error_code ec;const auto status=std::filesystem::symlink_status(path,ec);
    if(ec && ec!=std::errc::no_such_file_or_directory)return fail(error,"Save destination status failed: "+ec.message());
    if(std::filesystem::exists(status) || std::filesystem::is_symlink(status))
        return fail(error,"Save destination already exists; choose a fresh path");
    return true;
}
} // namespace

bool readDetSave(const std::filesystem::path& path,DetSessionCheckpoint& output,std::string& error){
    try {
        if(!ordinary(path,error))return false;
        std::error_code ec;const auto size=std::filesystem::file_size(path,ec);
        if(ec)return fail(error,"Save file size failed: "+ec.message());
        if(size<kDetSaveHeaderBytes+minCheckpointBytes || size>kDetSaveMaxFileBytes)
            return fail(error,"Save file size limit");
        std::ifstream input(path,std::ios::binary);
        if(!input)return fail(error,"Cannot open save file");
        std::array<std::uint8_t,kDetSaveHeaderBytes> header{};
        input.read(reinterpret_cast<char*>(header.data()),static_cast<std::streamsize>(header.size()));
        if(input.gcount()!=static_cast<std::streamsize>(header.size()) || !input)
            return fail(error,"Truncated save header");
        if(!std::equal(magic.begin(),magic.end(),header.begin()))return fail(error,"Unknown save magic");
        if(u32(header.data()+8)!=kDetSaveFormatVersion)return fail(error,"Unknown save envelope version");
        const auto length=u32(header.data()+12);
        if(length<minCheckpointBytes || length>kDetSaveMaxCheckpointBytes
            || std::uintmax_t{length}+kDetSaveHeaderBytes!=size)
            return fail(error,"Save payload length/size mismatch");
        std::vector<std::uint8_t> bytes(length);
        input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        if(input.gcount()!=static_cast<std::streamsize>(bytes.size()) || !input)
            return fail(error,"Truncated save payload");
        char trailing=0;input.read(&trailing,1);
        if(input.gcount()!=0 || !input.eof() || input.bad())return fail(error,"Trailing or unreadable save bytes");
        input.clear();input.close();if(input.fail())return fail(error,"Save read close failed");
        DetSessionCheckpoint staged;
        if(!decodeDetCheckpoint(bytes,staged,error)){error="Save checkpoint decode: "+error;return false;}
        output=std::move(staged);error.clear();return true;
    }catch(const std::exception& e){return fail(error,std::string{"Save read failed: "}+e.what());}
    catch(...){return fail(error,"Unknown save read failure");}
}

bool writeDetSave(const std::filesystem::path& path,const DetSessionCheckpoint& checkpoint,std::string& error){
    try {
        const auto payload=encodeDetCheckpoint(checkpoint);DetSessionCheckpoint checked;
        if(!decodeDetCheckpoint(payload,checked,error)){error="Save checkpoint encode validation: "+error;return false;}
        if(!fresh(path,error))return false;
        std::array<std::uint8_t,kDetSaveHeaderBytes> header{};
        std::copy(magic.begin(),magic.end(),header.begin());put32(header.data()+8,kDetSaveFormatVersion);
        put32(header.data()+12,static_cast<std::uint32_t>(payload.size()));
        std::ofstream output(path,std::ios::binary|std::ios::out|std::ios::noreplace);
        if(!output)return fail(error,"Cannot exclusively create save destination; choose a fresh path");
        output.write(reinterpret_cast<const char*>(header.data()),static_cast<std::streamsize>(header.size()));
        output.write(reinterpret_cast<const char*>(payload.data()),static_cast<std::streamsize>(payload.size()));
        output.flush();const bool written=static_cast<bool>(output);
        output.close();
        if(!written || output.fail())return fail(error,
            "Save write/flush/close failed; partial new file may remain, choose a fresh path or handle it explicitly");
        error.clear();return true;
    }catch(const std::exception& e){return fail(error,std::string{"Save write failed (partial new file may remain): "}+e.what());}
    catch(...){return fail(error,"Unknown save write failure; partial new file may remain");}
}

bool saveDetSession(const std::filesystem::path& path,const DeterministicSession& session,std::string& error){
    try {const auto checkpoint=session.checkpoint();if(!checkpoint)return fail(error,"Save capture: "+session.error());
        return writeDetSave(path,*checkpoint,error);
    }catch(const std::exception& e){return fail(error,std::string{"Save capture failed: "}+e.what());}
    catch(...){return fail(error,"Unknown save capture failure");}
}
bool loadDetSession(const std::filesystem::path& path,DeterministicSession& session,std::string& error){
    try {DetSessionCheckpoint checkpoint;if(!readDetSave(path,checkpoint,error))return false;
        if(!session.restore(checkpoint))return fail(error,"Save restore: "+session.error());
        error.clear();return true;
    }catch(const std::exception& e){return fail(error,std::string{"Save restore failed: "}+e.what());}
    catch(...){return fail(error,"Unknown save restore failure");}
}
} // namespace ayt::entity
