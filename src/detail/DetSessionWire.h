#pragma once
#include <AYEntity/DeterministicSession.h>
#include <AYReplay/ReplayHash.h>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace ayt::entity::detwire {
inline constexpr std::size_t maxBytes=8*1024*1024, maxInputBytes=64*1024;
inline constexpr std::uint32_t maxActors=1024, maxSchemas=64, maxFields=128, maxCommands=1024;
struct Writer {
    std::vector<std::uint8_t> bytes;
    void u64(std::uint64_t v,unsigned count=8) {
        if (bytes.size()+count>maxBytes) throw std::length_error("Session wire size limit");
        for (unsigned i=0;i<count;++i) bytes.push_back(static_cast<std::uint8_t>(v>>(8*i)));
    }
    void u32(std::uint32_t v) { u64(v,4); }
    void blob(std::span<const std::uint8_t> v) {
        if (v.size()>maxBytes || bytes.size()+v.size()+4>maxBytes) throw std::length_error("Session blob limit");
        u32(static_cast<std::uint32_t>(v.size()));bytes.insert(bytes.end(),v.begin(),v.end());
    }
    std::vector<std::uint8_t> finish() { u64(replay::fnv1a64(bytes.data(),bytes.size()));return std::move(bytes); }
};
struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t position=0;
    std::uint64_t u64(unsigned count=8) {
        if (count>bytes.size()-position) throw std::runtime_error("Truncated session record");
        std::uint64_t v=0;for(unsigned i=0;i<count;++i) v|=std::uint64_t{bytes[position++]}<<(8*i);return v;
    }
    std::uint32_t u32() { return static_cast<std::uint32_t>(u64(4)); }
    std::uint32_t count(std::uint32_t bound) {
        const auto n=u32();if(n>bound) throw std::runtime_error("Session record count limit");return n;
    }
    bool boolean() { const auto v=u64(1);if(v>1)throw std::runtime_error("Invalid boolean");return v!=0; }
    std::vector<std::uint8_t> blob(std::uint32_t bound=maxBytes) {
        const auto n=count(bound);if(n>bytes.size()-position)throw std::runtime_error("Truncated session blob");
        std::vector<std::uint8_t> v(bytes.begin()+position,bytes.begin()+position+n);position+=n;return v;
    }
    void end() { if(position!=bytes.size())throw std::runtime_error("Trailing session bytes"); }
};
inline Reader checked(std::span<const std::uint8_t> bytes,std::uint32_t magic) {
    if(bytes.size()<16 || bytes.size()>maxBytes)throw std::runtime_error("Session record size limit");
    Reader trailer{bytes.last(8)};
    if(trailer.u64()!=replay::fnv1a64(bytes.data(),bytes.size()-8))throw std::runtime_error("Session checksum mismatch");
    Reader r{bytes.first(bytes.size()-8)};
    if(r.u32()!=magic || r.u32()!=1)throw std::runtime_error("Unknown session record version");return r;
}
inline auto commandKey(const DetTickCommand& c) { return std::pair{c.source,c.sequence}; }
inline void command(Writer& w,const DetTickCommand& c) { w.u32(c.source);w.u32(c.sequence);w.u32(c.type);w.blob(c.payload); }
inline DetTickCommand command(Reader& r) { DetTickCommand c;c.source=r.u32();c.sequence=r.u32();c.type=r.u32();c.payload=r.blob(maxInputBytes);return c; }
inline void blocks(Writer& w,const DetStateBlocks& b) {
    w.u32(static_cast<std::uint32_t>(b.size()));
    for(const auto& [id,words]:b) { w.u32(id);w.u32(static_cast<std::uint32_t>(words.size()));for(auto v:words)w.u64(v); }
}
inline DetStateBlocks blocks(Reader& r) {
    DetStateBlocks b;auto n=r.count(maxSchemas);
    for(unsigned i=0;i<n;++i) {
        auto id=r.u32(),count=r.count(maxFields);std::vector<std::uint64_t> words;words.reserve(count);
        for(unsigned j=0;j<count;++j)words.push_back(r.u64());
        if(!b.emplace(id,std::move(words)).second)throw std::runtime_error("Duplicate state schema");
    }return b;
}
inline constexpr std::uint32_t checkpointMagic=0x43534441,inputMagic=0x49534441,manifestMagic=0x4d534441;
} // namespace ayt::entity::detwire
