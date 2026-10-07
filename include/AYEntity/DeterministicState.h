#pragma once
#include <AYMath/DetQuaternion.h>
#include <concepts>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace ayt::entity {
/// Stable Sim identity value, not a runtime handle. Zero means no reference;
/// references may name absent/retired actors and do not confer ownership.
struct DetEntityRef {
    std::uint64_t value=0;
    friend bool operator==(DetEntityRef,DetEntityRef)=default;
};
/// Persistent type codes; never inferred from native sizeof, RTTI or variant index.
enum class DetStateType : std::uint32_t {
    Word=0, Bool=1, Int8=2, UInt8=3, Int16=4, UInt16=5,
    Int32=6, UInt32=7, Int64=8, UInt64=9, Float32=10,
    Vec2=11, Vec3=12, Quaternion=13, EntityRef=14
};
using DetStateValue=std::variant<bool,std::int8_t,std::uint8_t,std::int16_t,std::uint16_t,
    std::int32_t,std::uint32_t,std::int64_t,std::uint64_t,math::DetFloat32,
    math::DetVec2,math::DetVec3,math::DetQuaternion,DetEntityRef>;
template<class T> concept DetStateScalar =
    std::same_as<T,bool> || std::same_as<T,std::int8_t> || std::same_as<T,std::uint8_t> ||
    std::same_as<T,std::int16_t> || std::same_as<T,std::uint16_t> ||
    std::same_as<T,std::int32_t> || std::same_as<T,std::uint32_t> ||
    std::same_as<T,std::int64_t> || std::same_as<T,std::uint64_t> ||
    std::same_as<T,math::DetFloat32> || std::same_as<T,math::DetVec2> ||
    std::same_as<T,math::DetVec3> || std::same_as<T,math::DetQuaternion> || std::same_as<T,DetEntityRef>;
struct DetTypedStateField {
    std::uint32_t id=0;
    DetStateValue initial=false;
};
/** @brief Fixed typed state layout, ordered by increasing nonzero stable field ID.
 * @note Schema IDs >=2 share the word-schema namespace. Version/default/type/field
 * changes invalidate the manifest; there is no implicit migration. Float/vector/
 * quaternion fields require finite binary32 values, preserving signed zero and
 * subnormals. Raw quaternion state need not be a unit rotation. Native float,
 * pointers, arbitrary structs and variable-size containers are unsupported.
 */
struct DetTypedStateSchema {
    std::uint32_t id=0,version=1;
    std::vector<DetTypedStateField> fields;
};
/// Return the persistent type of this explicitly supported value.
DetStateType detStateType(const DetStateValue& value);
/// Canonical lanes: one zero-extended integer bit pattern or binary32 per word;
/// vectors use x,y[,z], quaternions x,y,z,w. Throws on invalid/nonfinite input.
std::vector<std::uint64_t> encodeDetStateValue(const DetStateValue& value);
/// Reject unknown types, incorrect widths, high unused bits and nonfinite lanes.
DetStateValue decodeDetStateValue(DetStateType type,std::span<const std::uint64_t> words);
/// Validate the bounded layout and return canonical defaults for actor/global creation.
std::vector<std::uint64_t> detStateDefaults(const DetTypedStateSchema& schema);
/// Read by stable logical field ID, validating the layout/shape and target value.
DetStateValue readDetStateValue(const DetTypedStateSchema& schema,std::span<const std::uint64_t> words,std::uint32_t field);
/// All checks precede writes; type/shape/ID/value errors throw without changing words.
void writeDetStateValue(const DetTypedStateSchema& schema,std::span<std::uint64_t> words,std::uint32_t field,const DetStateValue& value);
/** @brief Immutable validated layout for repeated access; owns its metadata/defaults.
 * @note Constructor validates the complete schema once. Each access still checks
 * block size, field/type and canonical target value. Copying/changing the original
 * schema cannot change this layout. Wire bytes are identical to schema helpers.
 */
class DetStateLayout {
public:
    explicit DetStateLayout(const DetTypedStateSchema& schema);
    const std::vector<std::uint64_t>& defaults() const {return _defaults;}
    DetStateValue read(std::span<const std::uint64_t> words,std::uint32_t field) const;
    void write(std::span<std::uint64_t> words,std::uint32_t field,const DetStateValue& value) const;
    bool valid(std::span<const std::uint64_t> words) const;
private:
    struct Field {std::uint32_t id;DetStateType type;std::size_t offset,width;};
    const Field& locate(std::size_t size,std::uint32_t field) const;
    std::vector<Field> _fields;
    std::vector<std::uint64_t> _defaults;
};
template<DetStateScalar T> T readDetState(const DetTypedStateSchema& schema,std::span<const std::uint64_t> words,std::uint32_t field) {
    return std::get<T>(readDetStateValue(schema,words,field));
}
template<DetStateScalar T> void writeDetState(const DetTypedStateSchema& schema,std::span<std::uint64_t> words,std::uint32_t field,T value) {
    writeDetStateValue(schema,words,field,DetStateValue{value});
}
} // namespace ayt::entity
