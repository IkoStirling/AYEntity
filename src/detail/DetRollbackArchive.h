#pragma once
#include "DetSessionWire.h"
#include <AYEntity/DeterministicRollbackReplay.h>
#include <AYReplay/FileReplayRecorder.h>
#include <filesystem>
#include <fstream>
#include <array>
#include <cstdio>
#include <algorithm>

namespace ayt::entity::detarchive {
using namespace detwire;
inline constexpr std::uint32_t magic=0x49524441,maxSegments=65536;
inline constexpr std::uint64_t indexLimit=16*1024*1024,fileLimit=256*1024*1024;
struct FileError : std::runtime_error {
    DetReplayFileIssue issue;
    FileError(std::string path,std::optional<std::uint64_t> offset,std::string message)
        :std::runtime_error(std::move(message)),issue{std::move(path),offset}{}
};
inline std::string sourceIndexPath(std::string source) {
    auto p=std::filesystem::path(source);if(p.extension()==".partial")p.replace_extension();
    if(p.extension()!=".rpi")throw std::runtime_error("Expected .rpi or .rpi.partial source name");return p.string();
}
inline std::string partBase(const std::string& index,std::uint32_t ordinal) {
    auto p=std::filesystem::path(index);char suffix[32];std::snprintf(suffix,sizeof(suffix),".part%06u.rpl",ordinal);
    return (p.parent_path()/(p.stem().string()+suffix)).string();
}
inline std::string partPath(const std::string& index,std::uint32_t ordinal) {
    return replay::FileReplayRecorder::rotationPathFor(partBase(index,ordinal),0);
}
inline std::uint64_t hashFile(const std::string& path,std::uint64_t expectedSize) {
    if(expectedSize>fileLimit || std::filesystem::file_size(path)!=expectedSize)throw std::runtime_error("Archive segment size changed/missing");
    std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Archive segment cannot open");
    std::array<char,65536> buffer{};std::uint64_t hash=14695981039346656037ull,read=0;
    while(in.read(buffer.data(),buffer.size()) || in.gcount()) {
        const auto count=in.gcount();read+=static_cast<std::uint64_t>(count);
        if(read>expectedSize)throw std::runtime_error("Archive segment grew while reading");
        for(std::streamsize i=0;i<count;++i)hash=(hash^static_cast<unsigned char>(buffer[static_cast<std::size_t>(i)]))*1099511628211ull;
    }
    if(in.bad() || read!=expectedSize)throw std::runtime_error("Archive segment short read");return hash;
}
inline void checkChildPath(const std::string& index,const std::string& child) {
    if(std::filesystem::is_symlink(child) || !std::filesystem::is_regular_file(child)
        || std::filesystem::canonical(child).parent_path()!=std::filesystem::canonical(std::filesystem::path(index).parent_path().empty()
            ? std::filesystem::path("."):std::filesystem::path(index).parent_path()))
        throw std::runtime_error("Archive segment is not a local regular file");
}
inline Writer header(unsigned kind,std::uint64_t previous,unsigned version=1) {Writer w;w.u32(magic);w.u32(version);w.u32(kind);w.u64(previous);return w;}
inline void describeFile(Writer& w,const DetReplayFileSegment& f,unsigned ordinal) {
    w.u32(ordinal);w.u64(f.storedBytes);w.u64(f.fileHash);w.u32(f.firstEpoch);w.u64(f.firstTick);w.u64(f.initialHash);
    w.u32(f.lastEpoch);w.u64(f.endTick);w.u64(f.finalHash);w.u64(f.records);
}
struct Index {
    std::vector<std::uint8_t> manifest;
    std::vector<DetReplayFileSegment> files;
    std::vector<DetReplaySegment> epochs;
    std::uint64_t head=0;
    std::optional<DetReplayArchiveRecovery> recovery;
};
inline bool looksLikeIndex(const std::string& path) {
    std::ifstream f(path,std::ios::binary);std::array<unsigned char,8> b{};f.read(reinterpret_cast<char*>(b.data()),b.size());
    std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::uint32_t(b[i+4])<<(8*i);
    return v==magic || std::filesystem::path(path).extension()==".rpi";
}
inline Index readIndex(const std::string& path,const std::string& childIndex={}) {
    const auto size=std::filesystem::file_size(path);if(size<32 || size>indexLimit)throw std::runtime_error("Archive index size budget");
    std::ifstream f(path,std::ios::binary);std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if(!f.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(size)))throw std::runtime_error("Archive index short read");
    Reader stream{bytes};Index index;std::uint64_t chain=0;bool sealed=false;unsigned version=0;
    for(unsigned record=0;stream.position<stream.bytes.size() && record<2*maxSegments+3;++record) {
        const auto offset=stream.position;
        try {
        const auto length=stream.count(maxBytes);if(length>stream.bytes.size()-stream.position)throw std::runtime_error("Truncated archive index record");
        const auto data=stream.bytes.subspan(stream.position,length);stream.position+=length;
        auto r=checked(data,magic,2);if(record==0)version=r.version;
        if(r.version!=version)throw std::runtime_error("Mixed archive index profiles");
        const auto kind=r.u32();if(r.u64()!=chain)throw std::runtime_error("Archive index chain mismatch");
        Reader trailer{data.last(8)};chain=trailer.u64();
        if(kind==0) {
            if(record!=0)throw std::runtime_error("Duplicate archive index header");index.manifest=r.blob();
            if(index.manifest.empty())throw std::runtime_error("Empty archive manifest");
            if(version==2) {const auto reason=r.u32(),ordinal=r.u32();
                if(reason<1 || reason>4 || !ordinal || ordinal>maxSegments)throw std::runtime_error("Invalid recovered prefix marker");
                index.recovery=DetReplayArchiveRecovery{static_cast<DetReplayArchiveStop>(reason),ordinal};}
        }else if(index.manifest.empty())throw std::runtime_error("Archive missing index header");
        else if(kind==1) {
            const auto ordinal=r.u32();DetReplayFileSegment e;
            e.storedBytes=r.u64();e.fileHash=r.u64();e.firstEpoch=r.u32();e.firstTick=r.u64();e.initialHash=r.u64();
            e.lastEpoch=r.u32();e.endTick=r.u64();e.finalHash=r.u64();e.records=r.u64();
            if(ordinal!=index.files.size() || ordinal>=maxSegments || !e.firstEpoch || e.lastEpoch<e.firstEpoch || e.endTick<e.firstTick
                || e.storedBytes<sizeof(replay::ReplayFileHeader)+2*sizeof(replay::ReplayEventHeader) || e.storedBytes>fileLimit
                || !e.records || e.records>99996)throw std::runtime_error("Invalid archive file index entry");
            if(!index.files.empty()) {const auto& prior=index.files.back();
                if(e.firstEpoch!=prior.lastEpoch || e.firstTick!=prior.endTick || e.initialHash!=prior.finalHash)
                    throw std::runtime_error("Archive file continuity mismatch");}
            const auto& root=childIndex.empty()?path:childIndex;e.path=partPath(root,ordinal);
            try {checkChildPath(root,e.path);
                if(std::filesystem::file_size(e.path)!=e.storedBytes)throw std::runtime_error("Archive segment size mismatch");
            }catch(const std::exception& ex){throw FileError(e.path,std::nullopt,ex.what());}
            index.files.push_back(std::move(e));
        }else if(kind==2) {
            DetReplaySegment e;e.epoch=r.u32();e.firstTick=r.u64();e.endTick=r.u64();e.skippedTicks=r.u64();
            if(!e.epoch || e.endTick<e.firstTick || index.epochs.size()>=maxSegments)throw std::runtime_error("Invalid archive epoch entry");
            if(index.epochs.empty()) {if(e.skippedTicks)throw std::runtime_error("Initial archive gap");}
            else {const auto& prior=index.epochs.back();if(e.epoch<=prior.epoch || e.firstTick<prior.endTick || e.skippedTicks!=e.firstTick-prior.endTick)
                throw std::runtime_error("Archive epoch continuity mismatch");}
            index.epochs.push_back(e);
        }else if(kind==3) {
            const auto files=r.u32(),epochs=r.u32(),epoch=r.u32();const auto end=r.u64(),head=r.u64(),hash=r.u64();
            if(files!=index.files.size() || epochs!=index.epochs.size() || !files || !epochs || head<end
                || epoch!=index.files.back().lastEpoch || end!=index.files.back().endTick || hash!=index.files.back().finalHash
                || epoch!=index.epochs.back().epoch || end!=index.epochs.back().endTick || stream.position!=bytes.size())
                throw std::runtime_error("Archive completion seal mismatch");
            index.head=head;sealed=true;
        }else throw std::runtime_error("Unknown archive index record");
        r.end();if(sealed)break;
        }catch(const FileError&){throw;}
        catch(const std::exception& e){throw FileError(path,offset,e.what());}
    }
    if(!sealed || stream.position!=bytes.size())throw std::runtime_error("Archive index missing completion seal");
    if(index.recovery && index.recovery->stopOrdinal!=index.files.size())throw std::runtime_error("Recovered prefix length mismatch");
    const auto& first=index.files.front();
    if(first.firstEpoch!=index.epochs.front().epoch || first.firstTick!=index.epochs.front().firstTick)
        throw std::runtime_error("Archive initial boundary mismatch");
    for(const auto& file:index.files)for(auto key:{std::pair(file.firstEpoch,file.firstTick),std::pair(file.lastEpoch,file.endTick)}) {
        auto epoch=std::lower_bound(index.epochs.begin(),index.epochs.end(),key.first,[](const auto& e,auto id){return e.epoch<id;});
        if(epoch==index.epochs.end() || epoch->epoch!=key.first || key.second<epoch->firstTick || key.second>epoch->endTick)
            throw std::runtime_error("Archive file boundary outside epoch index");
    }
    return index;
}
} // namespace ayt::entity::detarchive
