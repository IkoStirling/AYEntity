#include <AYEntity/DeterministicState.h>
#include <chrono>
#include <iostream>
using namespace ayt::entity;
int main() {
    for(unsigned fields:{8u,64u,128u}) {
        DetTypedStateSchema schema{2,1,{}};
        for(unsigned i=1;i<=fields;++i)schema.fields.push_back({i,std::uint64_t{i}});
        const DetStateLayout layout(schema);auto words=layout.defaults();std::uint64_t sum=0;
        auto measure=[&](bool compiled){auto start=std::chrono::steady_clock::now();
            for(unsigned i=0;i<100000;++i){const auto value=compiled?layout.read(words,fields):readDetStateValue(schema,words,fields);
                sum+=std::get<std::uint64_t>(value);}
            return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();};
        const auto before=measure(false),after=measure(true);
        if(sum!=200000ull*fields)return 1;
        std::cout<<fields<<" fields, 100000 last-field reads: schema="<<before<<" us, layout="<<after<<" us\n";
    }
}
