#include <AYEntity/DeterministicState.h>
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace ayt::entity {
namespace {
constexpr DetStateType types[]={DetStateType::Bool,DetStateType::Int8,DetStateType::UInt8,
    DetStateType::Int16,DetStateType::UInt16,DetStateType::Int32,DetStateType::UInt32,
    DetStateType::Int64,DetStateType::UInt64,DetStateType::Float32,DetStateType::Vec2,
    DetStateType::Vec3,DetStateType::Quaternion,DetStateType::EntityRef};
template<class T> T integer(std::span<const std::uint64_t> words) {
    using U=std::make_unsigned_t<T>;
    if(words.size()!=1 || (sizeof(T)<8 && words[0]>std::numeric_limits<U>::max()))
        throw std::invalid_argument("Non-canonical integer state");
    const auto bits=static_cast<U>(words[0]);
    if constexpr(std::is_signed_v<T>)return std::bit_cast<T>(bits);
    else return bits;
}
struct FieldLocation {std::size_t offset,width;const DetTypedStateField* field;};
FieldLocation locate(const DetTypedStateSchema& schema,std::size_t size,std::uint32_t field) {
    const auto defaults=detStateDefaults(schema);
    if(size!=defaults.size())throw std::invalid_argument("Typed state shape mismatch");
    std::size_t offset=0;
    for(const auto& f:schema.fields){const auto width=encodeDetStateValue(f.initial).size();
        if(f.id==field)return {offset,width,&f};offset+=width;}
    throw std::out_of_range("Unknown typed state field");
}
}
DetStateType detStateType(const DetStateValue& value) {return types[value.index()];}
std::vector<std::uint64_t> encodeDetStateValue(const DetStateValue& value) {
    return std::visit([](auto v)->std::vector<std::uint64_t>{
        using T=decltype(v);
        if constexpr(std::same_as<T,bool>)return {v?1ull:0ull};
        else if constexpr(std::is_integral_v<T>){using U=std::make_unsigned_t<T>;return {static_cast<U>(v)};}
        else if constexpr(std::same_as<T,DetEntityRef>)return {v.value};
        else {
            if(!v.isFinite())throw std::invalid_argument("Non-finite typed state");
            if constexpr(std::same_as<T,math::DetFloat32>)return {v.bits()};
            else {const auto bits=v.bits();return {bits.begin(),bits.end()};}
        }
    },value);
}
DetStateValue decodeDetStateValue(DetStateType type,std::span<const std::uint64_t> words) {
    switch(type) {
    case DetStateType::Bool:if(words.size()!=1 || words[0]>1)throw std::invalid_argument("Non-canonical bool state");return words[0]!=0;
    case DetStateType::Int8:return integer<std::int8_t>(words);
    case DetStateType::UInt8:return integer<std::uint8_t>(words);
    case DetStateType::Int16:return integer<std::int16_t>(words);
    case DetStateType::UInt16:return integer<std::uint16_t>(words);
    case DetStateType::Int32:return integer<std::int32_t>(words);
    case DetStateType::UInt32:return integer<std::uint32_t>(words);
    case DetStateType::Int64:return integer<std::int64_t>(words);
    case DetStateType::UInt64:return integer<std::uint64_t>(words);
    case DetStateType::EntityRef:if(words.size()!=1)throw std::invalid_argument("Entity reference shape");return DetEntityRef{words[0]};
    default:break;
    }
    const std::size_t width=type==DetStateType::Float32?1:type==DetStateType::Vec2?2:
        type==DetStateType::Vec3?3:type==DetStateType::Quaternion?4:0;
    if(!width || words.size()!=width)throw std::invalid_argument("Unknown typed state type/width");
    std::array<std::uint32_t,4> bits{};
    for(std::size_t i=0;i<width;++i) {
        if(words[i]>UINT32_MAX)throw std::invalid_argument("Non-canonical binary32 lane");
        bits[i]=static_cast<std::uint32_t>(words[i]);
        if(!math::DetFloat32::fromBits(bits[i]).isFinite())throw std::invalid_argument("Non-finite typed state lane");
    }
    switch(type) {
    case DetStateType::Float32:return math::DetFloat32::fromBits(bits[0]);
    case DetStateType::Vec2:return math::DetVec2::fromBits({bits[0],bits[1]});
    case DetStateType::Vec3:return math::DetVec3::fromBits({bits[0],bits[1],bits[2]});
    default:return math::DetQuaternion::fromBits(bits);
    }
}
std::vector<std::uint64_t> detStateDefaults(const DetTypedStateSchema& schema) {
    if(schema.id<2 || schema.id==UINT32_MAX || !schema.version || schema.fields.empty() || schema.fields.size()>128)
        throw std::invalid_argument("Invalid typed state schema");
    std::uint32_t previous=0;std::vector<std::uint64_t> words;
    for(const auto& f:schema.fields) {
        if(f.id<=previous)throw std::invalid_argument("Field IDs must be unique increasing nonzero values");previous=f.id;
        const auto encoded=encodeDetStateValue(f.initial);words.insert(words.end(),encoded.begin(),encoded.end());
    }
    return words;
}
DetStateValue readDetStateValue(const DetTypedStateSchema& schema,std::span<const std::uint64_t> words,std::uint32_t field) {
    const auto f=locate(schema,words.size(),field);
    return decodeDetStateValue(detStateType(f.field->initial),words.subspan(f.offset,f.width));
}
void writeDetStateValue(const DetTypedStateSchema& schema,std::span<std::uint64_t> words,std::uint32_t field,const DetStateValue& value) {
    const auto f=locate(schema,words.size(),field);
    if(detStateType(f.field->initial)!=detStateType(value))throw std::invalid_argument("Typed state field type mismatch");
    const auto encoded=encodeDetStateValue(value);
    std::copy(encoded.begin(),encoded.end(),words.begin()+f.offset);
}
} // namespace ayt::entity
