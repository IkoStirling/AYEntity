#include <AYEntity/DeterministicState.h>
#include <cfenv>
#include <iostream>
#include <limits>
#include <stdexcept>
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif
using namespace ayt::entity;
using D=ayt::math::DetFloat32;
using V=ayt::math::DetVec3;
void require(bool ok){if(!ok)throw std::runtime_error("Typed state oracle mismatch");}
template<class F> void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}require(rejected);}
int main() {
    try {
        require(std::fesetround(FE_UPWARD)==0);
#if defined(_M_X64) || defined(__x86_64__)
        _mm_setcsr(_mm_getcsr()|0x8040u);
#endif
        const DetTypedStateSchema schema{2,1,{{10,D{}},{20,V{}},{30,ayt::math::DetQuaternion{}},
            {40,std::uint64_t{0}},{50,std::int32_t{0}},{60,false},{70,DetEntityRef{}}}};
        const DetStateLayout layout(schema);auto compiled=layout.defaults();auto words=detStateDefaults(schema);const auto dt=D::fromInt(1)/D::fromInt(64);
        for(std::uint64_t tick=0;tick<10000;++tick) {
            writeDetState(schema,words,10,readDetState<D>(schema,words,10)+D::fromBits(0x3f000000u)*dt);
            writeDetState(schema,words,20,readDetState<V>(schema,words,20)+V{D::fromBits(1),D::fromInt(-1),D{}});
            writeDetState(schema,words,40,tick+1);writeDetState(schema,words,50,-static_cast<std::int32_t>(tick+1));
            writeDetState(schema,words,60,tick%2==0);writeDetState(schema,words,70,DetEntityRef{tick+2});
        }
        // Independently exact dyadic arithmetic: 10000 / 128 = 78.125;
        // 10000 minimum subnormals = binary32 bits 10000, y = -10000.
        for(std::uint64_t tick=0;tick<10000;++tick){
            layout.write(compiled,10,std::get<D>(layout.read(compiled,10))+D::fromBits(0x3f000000u)*dt);
            layout.write(compiled,20,std::get<V>(layout.read(compiled,20))+V{D::fromBits(1),D::fromInt(-1),D{}});
            layout.write(compiled,40,tick+1);layout.write(compiled,50,-static_cast<std::int32_t>(tick+1));
            layout.write(compiled,60,tick%2==0);layout.write(compiled,70,DetEntityRef{tick+2});}
        require(compiled==words && layout.valid(compiled));
        rejects([&]{layout.write(compiled,10,D::fromBits(0x7f800000u));});
        rejects([&]{layout.write(compiled,50,std::uint32_t{1});});
        rejects([&]{(void)layout.read(std::span(compiled).first(1),10);});
        rejects([&]{(void)layout.read(compiled,99);});require(compiled==words);
        auto corrupt=compiled;corrupt[0]=0x7fc00000u;require(!layout.valid(corrupt));
        const std::vector<std::uint64_t> expected={0x429c4000u,10000,0xc61c4000u,0,0,0,0,0x3f800000u,
            10000,0xffffd8f0u,0,10001};require(words==expected);
        const std::vector<DetStateValue> integers={std::int8_t{-128},std::uint8_t{255},std::int16_t{-32768},std::uint16_t{65535},
            std::numeric_limits<std::int32_t>::min(),UINT32_MAX,std::numeric_limits<std::int64_t>::min(),UINT64_MAX};
        for(const auto& value:integers){const auto encoded=encodeDetStateValue(value);
            require(encodeDetStateValue(decodeDetStateValue(detStateType(value),encoded))==encoded);}
        const auto before=words;
        rejects([&]{writeDetState(schema,words,10,D::fromBits(0x7f800000u));});
        rejects([&]{writeDetState(schema,words,50,std::uint32_t{1});});
        rejects([]{(void)decodeDetStateValue(DetStateType::Bool,std::vector<std::uint64_t>{2});});
        rejects([]{(void)decodeDetStateValue(DetStateType::UInt8,std::vector<std::uint64_t>{256});});
        require(words==before);
        std::cout<<"PASS typed state: 10000 ticks, exact dyadic/subnormal oracle, fixed integers and rejection atomicity\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
