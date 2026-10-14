#include <AYEntity/DeterministicRigidBody3D.h>
#include <algorithm>
#include <set>
#include <tuple>

namespace ayt::entity {
namespace {
using D=math::DetFloat32;using V=math::DetVec3;using Q=math::DetQuaternion;using M=math::DetMat3;
constexpr std::uint64_t logicHash=0x5249474944334432ULL;
[[noreturn]]void invalid(){throw std::invalid_argument("deterministic rigid3D: invalid policy/state, geometry or numeric budget");}
D one(){return D::fromInt(1);}D half(){return D::fromBits(0x3f000000u);}D abs(D x){return x.signBit()?-x:x;}
D min(D a,D b){return a<b?a:b;}D max(D a,D b){return a>b?a:b;}D clamp(D x,D a,D b){return max(a,min(x,b));}
D component(V v,unsigned i){return i==0?v.x:i==1?v.y:v.z;}
V vector(std::array<D,3> a){return {a[0],a[1],a[2]};}
V mul(const M& m,V v){return vector(m.transformed({v.x,v.y,v.z}));}
V axis(unsigned i){return i==0?V::fromInts(1,0,0):i==1?V::fromInts(0,1,0):V::fromInts(0,0,1);}
V unit(V v){auto result=v.normalized();if(!result)invalid();return *result;}
bool bounded(D v){return v.isFinite() && abs(v)<=D::fromInt(1000000);}
bool bounded(V v){return bounded(v.x)&&bounded(v.y)&&bounded(v.z);}
bool validConfig(const DetRigidBody3DConfig& c){
    std::set<std::uint32_t> ids{c.bodySchema,c.policySchema,c.jointSchema,c.historySchema};
    if(ids.size()!=4 || *ids.begin()<2 || *ids.rbegin()==UINT32_MAX || !c.systemId||!c.validatorId||!c.logicProfileId||!c.eventType
       || c.maxBodies<1||c.maxBodies>32||c.maxContacts<1||c.maxContacts>512||c.maxTriggerPairs<1||c.maxTriggerPairs>32
       || c.substeps<1||c.substeps>16||c.velocityIterations<1||c.velocityIterations>32||c.positionIterations<1||c.positionIterations>16
       || !bounded(c.gravity)||!bounded(c.penetrationSlop)||c.penetrationSlop<D{}||!bounded(c.correction)||c.correction<=D{}||c.correction>one()
       || !bounded(c.restitutionThreshold)||c.restitutionThreshold<D{}||c.joints.size()>8)return false;
    if(c.maxContinuousHits<1||c.maxContinuousHits>32||c.maxSweepIterations<1||c.maxSweepIterations>64||!bounded(c.continuousSlop)||c.continuousSlop<=D{}||c.continuousSlop>D::fromBits(0x3c23d70au))return false;
    std::uint32_t previous=0;
    for(const auto& j:c.joints){if(j.id<=previous||static_cast<std::uint32_t>(j.kind)>4||!j.first||j.first==j.second||!j.second
        || !bounded(j.anchorFirst)||!bounded(j.anchorSecond)||!bounded(j.restLength)||j.restLength<D{}||!j.referenceRotation.normalized()
        || (j.kind!=DetJointKind3D::Distance&&!j.restLength.isZero()))return false;previous=j.id;}
    for(const auto& j:c.joints){if(!bounded(j.axisFirst)||!bounded(j.axisSecond)||!j.axisFirst.normalized()||!j.axisSecond.normalized()
        ||abs(j.referenceRotation.dot(j.referenceRotation)-one())>D::fromBits(0x3a800000u)||!bounded(j.lower)||!bounded(j.upper)||!bounded(j.targetSpeed)||!bounded(j.maxMotorForce))return false;
        const bool free=j.kind==DetJointKind3D::Hinge||j.kind==DetJointKind3D::Slider;
        if((j.limits||j.motor)&&!free)return false;
        if(j.limits&&(j.lower>j.upper||(j.kind==DetJointKind3D::Hinge&&(j.lower< -math::kDetPi||j.upper>math::kDetPi))))return false;
        if(!j.limits&&(!j.lower.isZero()||!j.upper.isZero()))return false;
        if(j.motor?j.maxMotorForce<=D{}:(!j.targetSpeed.isZero()||!j.maxMotorForce.isZero()))return false;
        if(j.kind==DetJointKind3D::Hinge){auto second=j.referenceRotation.rotated(j.axisSecond);if(!second)return false;auto first=j.axisFirst.normalized(),other=second->normalized();if(!other||(*first-*other).length()>D::fromBits(0x3a800000u))return false;}
    }
    return true;
}
void validateBody(const DetRigidBody3D& b){
    if(static_cast<std::uint32_t>(b.mode)>3||static_cast<std::uint32_t>(b.shape)>2||!bounded(b.half)||!bounded(b.mass)
       ||!bounded(b.velocity)||!bounded(b.angularVelocity)||!bounded(b.force)||!bounded(b.torque)||!bounded(b.restitution)||!bounded(b.friction)
       || b.restitution<D{}||b.restitution>one()||b.friction<D{}||!bounded(b.linearDamping)||!bounded(b.angularDamping)
       ||b.linearDamping<D{}||b.linearDamping>one()||b.angularDamping<D{}||b.angularDamping>one())invalid();
    if(b.mode==DetRigidMode3D::Disabled){if(b.continuous||!b.mass.isZero()||b.velocity!=V{}||b.angularVelocity!=V{}||b.force!=V{}||b.torque!=V{})invalid();return;}
    if(!b.layer||b.half.x<=D{}||b.half.z<=D{}||(b.shape==DetRigidShape3D::Capsule?b.half.y<D{}:b.half.y<=D{})||
       (b.shape==DetRigidShape3D::Sphere&&(b.half.x!=b.half.y||b.half.x!=b.half.z)))invalid();
    if(b.shape==DetRigidShape3D::Capsule&&b.half.x!=b.half.z)invalid();
    if(b.continuous&&(b.mode!=DetRigidMode3D::Dynamic||b.shape!=DetRigidShape3D::Sphere||b.trigger))invalid();
    if(b.mode==DetRigidMode3D::Dynamic){if(b.mass<=D{})invalid();}
    else if(!b.mass.isZero()||b.force!=V{}||b.torque!=V{}||!b.linearDamping.isZero()||!b.angularDamping.isZero())invalid();
    if(b.mode==DetRigidMode3D::Static&&(b.velocity!=V{}||b.angularVelocity!=V{}))invalid();
}
DetTypedStateSchema bodySchema(const DetRigidBody3DConfig& c){return {c.bodySchema,2,{{1,std::uint32_t{0}},{2,std::uint32_t{1}},{3,V{}},{4,D{}},{5,V{}},{6,V{}},{7,V{}},{8,V{}},{9,D{}},{10,D{}},{11,std::uint32_t{1}},{12,UINT32_MAX},{13,false},{14,D{}},{15,D{}},{16,false}}};}
DetTypedStateSchema policySchema(const DetRigidBody3DConfig& c){return {c.policySchema,2,{{1,kDetRigidBody3DProfileVersion},{2,M::kProfileVersion},{3,c.maxBodies},{4,c.maxContacts},{5,c.maxTriggerPairs},{6,c.substeps},{7,c.velocityIterations},{8,c.positionIterations},{9,c.gravity},{10,c.penetrationSlop},{11,c.correction},{12,c.restitutionThreshold},{13,c.eventType},{14,c.systemId},{15,c.priority},{16,c.validatorId},{17,c.logicProfileId},{18,c.maxContinuousHits},{19,c.maxSweepIterations},{20,c.continuousSlop}}};}
DetTypedStateSchema jointSchema(const DetRigidBody3DConfig& c){DetTypedStateSchema s{c.jointSchema,2,{{1,kDetRigidBody3DProfileVersion},{2,static_cast<std::uint32_t>(c.joints.size())}}};
    for(unsigned i=0;i<c.joints.size();++i){const auto& j=c.joints[i];auto f=10+i*15;s.fields.push_back({f,j.id});s.fields.push_back({f+1,static_cast<std::uint32_t>(j.kind)});s.fields.push_back({f+2,DetEntityRef{j.first}});s.fields.push_back({f+3,DetEntityRef{j.second}});s.fields.push_back({f+4,j.anchorFirst});s.fields.push_back({f+5,j.anchorSecond});s.fields.push_back({f+6,j.restLength});s.fields.push_back({f+7,j.referenceRotation});s.fields.push_back({f+8,j.axisFirst});s.fields.push_back({f+9,j.axisSecond});s.fields.push_back({f+10,j.lower});s.fields.push_back({f+11,j.upper});s.fields.push_back({f+12,std::uint32_t((j.limits?1:0)|(j.motor?2:0))});s.fields.push_back({f+13,j.targetSpeed});s.fields.push_back({f+14,j.maxMotorForce});}return s;}
DetTypedStateSchema historySchema(const DetRigidBody3DConfig& c){DetTypedStateSchema s{c.historySchema,1,{{1,std::uint32_t{1}},{2,std::uint32_t{0}},{3,std::uint64_t{0}}}};
    for(unsigned i=0;i<c.maxTriggerPairs;++i){s.fields.push_back({10+2*i,DetEntityRef{}});s.fields.push_back({11+2*i,DetEntityRef{}});}return s;}
struct Layouts {DetStateLayout body,policy,joints,history;explicit Layouts(const DetRigidBody3DConfig& c):body(bodySchema(c)),policy(policySchema(c)),joints(jointSchema(c)),history(historySchema(c)) {}};
DetRigidBody3D readBody(const DetStateLayout& l,std::span<const std::uint64_t> words){DetRigidBody3D b;
    b.mode=static_cast<DetRigidMode3D>(std::get<std::uint32_t>(l.read(words,1)));b.shape=static_cast<DetRigidShape3D>(std::get<std::uint32_t>(l.read(words,2)));b.half=std::get<V>(l.read(words,3));b.mass=std::get<D>(l.read(words,4));
    b.velocity=std::get<V>(l.read(words,5));b.angularVelocity=std::get<V>(l.read(words,6));b.force=std::get<V>(l.read(words,7));b.torque=std::get<V>(l.read(words,8));
    b.restitution=std::get<D>(l.read(words,9));b.friction=std::get<D>(l.read(words,10));b.layer=std::get<std::uint32_t>(l.read(words,11));b.mask=std::get<std::uint32_t>(l.read(words,12));b.trigger=std::get<bool>(l.read(words,13));b.linearDamping=std::get<D>(l.read(words,14));b.angularDamping=std::get<D>(l.read(words,15));b.continuous=std::get<bool>(l.read(words,16));validateBody(b);return b;}
struct Body {SimEntityId id;DetRigidBody3D data;V p;Q q;D inverseMass{};V inertia{},inverseInertia{};M rotation{},worldInverse{};std::array<V,3> axes{};};
void refresh(Body& b){auto m=math::detRotationMatrix(b.q);if(!m)invalid();b.rotation=*m;for(unsigned i=0;i<3;++i)b.axes[i]={(*m)(0,i),(*m)(1,i),(*m)(2,i)};
    auto diagonal=M::zero();diagonal(0,0)=b.inverseInertia.x;diagonal(1,1)=b.inverseInertia.y;diagonal(2,2)=b.inverseInertia.z;b.worldInverse=b.rotation*diagonal*b.rotation.transposed();if(!b.worldInverse.isFinite())invalid();}
Body makeBody(SimEntityId id,DetRigidBody3D data,V p,Q q){if(!bounded(p)||!q.isFinite()||abs(q.dot(q)-one())>D::fromBits(0x3a800000u))invalid();Body b{id,data,p,q};
    if(data.mode==DetRigidMode3D::Dynamic){b.inverseMass=one()/data.mass;auto h=data.half;
        if(data.shape==DetRigidShape3D::Sphere){auto i=(D::fromInt(2)/D::fromInt(5))*data.mass*h.x*h.x;b.inertia={i,i,i};}
        else if(data.shape==DetRigidShape3D::Capsule){auto r=h.x,segment=h.y;auto cylinder=data.mass*(D::fromInt(2)*segment)/(D::fromInt(2)*segment+D::fromInt(4)*r/D::fromInt(3));auto caps=data.mass-cylinder;
            auto radial=r*r,axial=cylinder*radial*half()+(D::fromInt(2)/D::fromInt(5))*caps*radial;
            auto transverse=cylinder*(radial/D::fromInt(4)+segment*segment/D::fromInt(3))+caps*((D::fromInt(2)/D::fromInt(5))*radial+segment*segment+(D::fromInt(3)/D::fromInt(4))*segment*r);
            b.inertia={transverse,axial,transverse};}
        else {auto third=data.mass/D::fromInt(3);b.inertia={third*(h.y*h.y+h.z*h.z),third*(h.x*h.x+h.z*h.z),third*(h.x*h.x+h.y*h.y)};}
        if(!b.inertia.isFinite()||b.inertia.x<=D{}||b.inertia.y<=D{}||b.inertia.z<=D{})invalid();
        b.inverseInertia={one()/b.inertia.x,one()/b.inertia.y,one()/b.inertia.z};if(!b.inverseMass.isFinite()||!b.inverseInertia.isFinite())invalid();}
    refresh(b);return b;}
V extent(const Body& b){if(b.data.shape==DetRigidShape3D::Sphere)return b.data.half;auto h=b.data.half;if(b.data.shape==DetRigidShape3D::Capsule)return {h.x+abs(b.axes[1].x)*h.y,h.x+abs(b.axes[1].y)*h.y,h.x+abs(b.axes[1].z)*h.y};return {abs(b.axes[0].x)*h.x+abs(b.axes[1].x)*h.y+abs(b.axes[2].x)*h.z,abs(b.axes[0].y)*h.x+abs(b.axes[1].y)*h.y+abs(b.axes[2].y)*h.z,abs(b.axes[0].z)*h.x+abs(b.axes[1].z)*h.y+abs(b.axes[2].z)*h.z};}
void representable(const Body& b){auto e=extent(b);if(!e.isFinite())invalid();auto lo=b.p-e,hi=b.p+e;if(!lo.isFinite()||!hi.isFinite()||!(lo.x<hi.x&&lo.y<hi.y&&lo.z<hi.z))invalid();}
bool broad(const Body& a,const Body& b){auto delta=b.p-a.p;auto e=extent(a)+extent(b);return abs(delta.x)<=e.x&&abs(delta.y)<=e.y&&abs(delta.z)<=e.z;}
std::vector<V> clip(std::vector<V> polygon,V center,V direction,D limit){std::vector<V> out;if(polygon.empty())return out;V previous=polygon.back();D pd=(previous-center).dot(direction)-limit;
    for(auto current:polygon){auto cd=(current-center).dot(direction)-limit;bool inside=cd<=D{},priorInside=pd<=D{};
        if(inside!=priorInside){auto t=pd/(pd-cd);auto point=previous+(current-previous)*t;if(!point.isFinite())invalid();out.push_back(point);}if(inside)out.push_back(current);previous=current;pd=cd;}
    if(out.size()>12)invalid();return out;}
V support(const Body& b,V n){V p=b.p;for(unsigned i=0;i<3;++i)p+=b.axes[i]*(component(b.data.half,i)*(b.axes[i].dot(n)<D{}?-one():one()));return p;}
std::pair<V,V> closestSegments(V a0,V a1,V b0,V b1){auto u=a1-a0,v=b1-b0,w=a0-b0;auto a=u.dot(u),b=u.dot(v),c=v.dot(v),d=u.dot(w),e=v.dot(w);auto denom=a*c-b*b;
    if(!denom.isFinite())invalid();
    if(a.isZero()||c.isZero()){if((a.isZero()&&u!=V{})||(c.isZero()&&v!=V{}))invalid();
        if(a.isZero()&&c.isZero())return {a0,b0};if(a.isZero())return {a0,b0+v*clamp(e/c,D{},one())};return {a0+u*clamp(-d/a,D{},one()),b0};}
    auto s=denom>D{}?clamp((b*e-c*d)/denom,D{},one()):D{};auto t=(b*s+e)/c;
    if(t<D{}){t=D{};s=clamp(-d/a,D{},one());}else if(t>one()){t=one();s=clamp((b-d)/a,D{},one());}
    return {a0+u*s,b0+v*t};}
std::pair<V,V> segment(const Body& b){auto delta=b.data.shape==DetRigidShape3D::Capsule?b.axes[1]*b.data.half.y:V{};return {b.p-delta,b.p+delta};}
V closestPoint(V p,V a,V b){auto delta=b-a;auto square=delta.dot(delta);if(square.isZero()){if(delta!=V{})invalid();return a;}return a+delta*clamp((p-a).dot(delta)/square,D{},one());}
// Exact piecewise-quadratic minimization of segment distance to a local AABB.
// Every interval has a fixed set of exterior axes; ties retain earliest segment t.
std::pair<V,V> segmentBox(V a,V b,const Body& box){auto p=mul(box.rotation.transposed(),a-box.p),d=mul(box.rotation.transposed(),b-a);auto h=box.data.half;
    std::vector<D> cuts{D{},one()};for(unsigned i=0;i<3;++i){auto v=component(d,i);if(v.isZero())continue;for(auto side:{-one(),one()}){auto t=(side*component(h,i)-component(p,i))/v;if(t>D{}&&t<one())cuts.push_back(t);}}
    std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end()),cuts.end());D best{};V bestPoint{},bestBox{};bool first=true;
    auto evaluate=[&](D t){auto point=p+d*t;V q{clamp(point.x,-h.x,h.x),clamp(point.y,-h.y,h.y),clamp(point.z,-h.z,h.z)};auto dist=(point-q).dot(point-q);
        if(!dist.isFinite())invalid();if(first||dist<best){first=false;best=dist;bestPoint=point;bestBox=q;}};
    for(unsigned i=0;i+1<cuts.size();++i){auto left=cuts[i],right=cuts[i+1],mid=(left+right)*half();auto point=p+d*mid;D aa{},bb{};
        for(unsigned k=0;k<3;++k){auto x=component(point,k),bound=component(h,k);if(x< -bound||x>bound){auto dk=component(d,k),offset=component(p,k)-(x<D{}?-bound:bound);aa+=dk*dk;bb+=dk*offset;}}
        evaluate(left);if(aa>D{})evaluate(clamp(-bb/aa,left,right));evaluate(right);
    }return {box.p+mul(box.rotation,bestPoint),box.p+mul(box.rotation,bestBox)};
}
std::vector<DetRigidContact3D> capsuleContact(const Body& a,const Body& b){
    if(a.data.shape!=DetRigidShape3D::Box&&b.data.shape!=DetRigidShape3D::Box){auto as=segment(a),bs=segment(b);auto points=closestSegments(as.first,as.second,bs.first,bs.second);
        auto delta=points.second-points.first;auto distance=delta.length();auto penetration=a.data.half.x+b.data.half.x-distance;if(!distance.isFinite()||penetration<D{})return {};
        auto n=distance.isZero()?axis(0):delta/distance;return {{a.id,b.id,points.first+n*(a.data.half.x-penetration*half()),n,penetration}};}
    bool capsuleA=a.data.shape!=DetRigidShape3D::Box;const auto& cap=capsuleA?a:b;const auto& box=capsuleA?b:a;auto ends=segment(cap),points=segmentBox(ends.first,ends.second,box);
    auto delta=points.first-points.second;auto distance=delta.length();V n{},capPoint{},boxPoint{};D penetration;
    if(!distance.isZero()){penetration=cap.data.half.x-distance;if(penetration<D{})return {};n=delta/distance;capPoint=points.first-n*cap.data.half.x;boxPoint=points.second;}
    else {unsigned best=0;auto center=cap.p-box.p;D depth{};for(unsigned i=0;i<3;++i){auto projected=abs(cap.axes[1].dot(box.axes[i]))*cap.data.half.y+cap.data.half.x;auto overlap=component(box.data.half,i)+projected-abs(center.dot(box.axes[i]));if(overlap<D{})return {};if(i==0||overlap<depth){best=i;depth=overlap;}}
        n=box.axes[best]*(center.dot(box.axes[best])<D{}?-one():one());penetration=depth;auto endpoint=cap.axes[1].dot(n)>D{}?ends.first:ends.second;capPoint=endpoint-n*cap.data.half.x;
        auto local=mul(box.rotation.transposed(),capPoint-box.p);std::array<D,3> coordinates{};for(unsigned i=0;i<3;++i)coordinates[i]=i==best?component(box.data.half,i)*(center.dot(box.axes[best])<D{}?-one():one()):clamp(component(local,i),-component(box.data.half,i),component(box.data.half,i));boxPoint=box.p+mul(box.rotation,vector(coordinates));}
    return {{a.id,b.id,(capPoint+boxPoint)*half(),capsuleA?-n:n,penetration}};
}
std::vector<DetRigidContact3D> boxBox(const Body& a,const Body& b){auto delta=b.p-a.p;V normal{};D penetration{};unsigned feature=0;bool first=true;
    for(unsigned k=0;k<15;++k){V n=k<3?a.axes[k]:k<6?b.axes[k-3]:a.axes[(k-6)/3].cross(b.axes[(k-6)%3]);auto square=n.dot(n);
        if(square<=D::fromBits(0x2b800000u))continue;n=n/square.sqrt();D ra{},rb{};for(unsigned i=0;i<3;++i){ra+=component(a.data.half,i)*abs(a.axes[i].dot(n));rb+=component(b.data.half,i)*abs(b.axes[i].dot(n));}
        auto overlap=ra+rb-abs(delta.dot(n));if(overlap<D{})return {};if(first||overlap<penetration){first=false;penetration=overlap;normal=delta.dot(n)<D{}?-n:n;feature=k;}}
    if(first)invalid();std::vector<DetRigidContact3D> out;
    if(feature<6){bool referenceA=feature<3;const Body& r=referenceA?a:b;const Body& incident=referenceA?b:a;unsigned face=feature%3;auto outward=referenceA?normal:-normal;
        auto center=r.p+outward*component(r.data.half,face);unsigned incidentFace=0;for(unsigned i=1;i<3;++i)if(abs(incident.axes[i].dot(outward))>abs(incident.axes[incidentFace].dot(outward)))incidentFace=i;
        auto incidentNormal=incident.axes[incidentFace]*(incident.axes[incidentFace].dot(outward)>D{}?-one():one());auto ic=incident.p+incidentNormal*component(incident.data.half,incidentFace);
        unsigned i=(incidentFace+1)%3,j=(incidentFace+2)%3;auto u=incident.axes[i]*component(incident.data.half,i),v=incident.axes[j]*component(incident.data.half,j);
        std::vector<V> polygon{ic-u-v,ic+u-v,ic+u+v,ic-u+v};
        for(unsigned k=0;k<3;++k)if(k!=face){polygon=clip(std::move(polygon),center,r.axes[k],component(r.data.half,k));polygon=clip(std::move(polygon),center,-r.axes[k],component(r.data.half,k));}
        for(auto p:polygon){auto depth=-(p-center).dot(outward);if(depth<D{})continue;auto point=p+outward*(depth*half());
            if(std::none_of(out.begin(),out.end(),[&](const auto& c){return c.point==point;}))out.push_back({a.id,b.id,point,normal,depth});if(out.size()==4)break;}
    }else {unsigned i=(feature-6)/3,j=(feature-6)%3;auto ac=a.p,bc=b.p;auto ae=a.axes[i]*component(a.data.half,i),be=b.axes[j]*component(b.data.half,j);
        for(unsigned k=0;k<3;++k){if(k!=i)ac+=a.axes[k]*(component(a.data.half,k)*(a.axes[k].dot(normal)<D{}?-one():one()));
            if(k!=j)bc+=b.axes[k]*(component(b.data.half,k)*(b.axes[k].dot(-normal)<D{}?-one():one()));}
        auto points=closestSegments(ac-ae,ac+ae,bc-be,bc+be);out.push_back({a.id,b.id,(points.first+points.second)*half(),normal,penetration});}
    if(out.empty())out.push_back({a.id,b.id,(support(a,normal)+support(b,-normal))*half(),normal,penetration});return out;
}
std::vector<DetRigidContact3D> contacts(const Body& a,const Body& b){if(!broad(a,b))return {};
    if(a.data.shape==DetRigidShape3D::Capsule||b.data.shape==DetRigidShape3D::Capsule)return capsuleContact(a,b);
    if(a.data.shape==DetRigidShape3D::Box&&b.data.shape==DetRigidShape3D::Box)return boxBox(a,b);
    if(a.data.shape==DetRigidShape3D::Sphere&&b.data.shape==DetRigidShape3D::Sphere){auto d=b.p-a.p;auto length=d.length();auto penetration=a.data.half.x+b.data.half.x-length;if(!length.isFinite()||penetration<D{})return {};auto n=length.isZero()?axis(0):d/length;
        return {{a.id,b.id,a.p+n*(a.data.half.x-penetration*half()),n,penetration}};}
    bool sphereA=a.data.shape==DetRigidShape3D::Sphere;const Body& s=sphereA?a:b;const Body& box=sphereA?b:a;auto local=mul(box.rotation.transposed(),s.p-box.p);std::array<D,3> closest{};
    for(unsigned i=0;i<3;++i)closest[i]=clamp(component(local,i),-component(box.data.half,i),component(box.data.half,i));
    auto point=box.p+mul(box.rotation,vector(closest));auto delta=s.p-point;auto distance=delta.length();V n;D penetration;
    if(!distance.isZero()){penetration=s.data.half.x-distance;if(penetration<D{})return {};n=delta/distance;}
    else {unsigned k=0;auto gap=box.data.half.x-abs(local.x);for(unsigned i=1;i<3;++i){auto g=component(box.data.half,i)-abs(component(local,i));if(g<gap){gap=g;k=i;}}
        n=box.axes[k]*(component(local,k)<D{}?-one():one());point=s.p+n*gap;penetration=s.data.half.x+gap;}
    auto midpoint=(point+s.p-n*s.data.half.x)*half();return {{a.id,b.id,midpoint,sphereA?-n:n,penetration}};
}
using Pair=std::pair<SimEntityId,SimEntityId>;
bool filter(const Body& a,const Body& b){return (a.data.layer&b.data.mask)&&(b.data.layer&a.data.mask);}
bool continuousScene(const std::vector<Body>& bodies,const DetRigidBody3DConfig& c){const bool enabled=std::any_of(bodies.begin(),bodies.end(),[](const Body& b){return b.data.continuous;});
    if(enabled&&!c.joints.empty())invalid();
    if(enabled)for(const auto& b:bodies){if(b.data.mode!=DetRigidMode3D::Static&&(b.data.mode!=DetRigidMode3D::Dynamic||b.data.shape!=DetRigidShape3D::Sphere))invalid();if(b.data.continuous&&b.data.half.x<D::fromInt(2)*c.continuousSlop)invalid();}return enabled;}
struct SphereGap {D distance;V outward,targetPoint;};
SphereGap sphereGap(V center,D radius,const Body& target){
    if(target.data.shape!=DetRigidShape3D::Box){auto ends=segment(target);auto closest=closestPoint(center,ends.first,ends.second);auto delta=center-closest;auto distance=delta.length();auto n=distance.isZero()?axis(0):delta/distance;
        return {distance-radius-target.data.half.x,n,closest+n*target.data.half.x};}
    auto local=mul(target.rotation.transposed(),center-target.p);auto h=target.data.half;V closest{clamp(local.x,-h.x,h.x),clamp(local.y,-h.y,h.y),clamp(local.z,-h.z,h.z)};
    auto point=target.p+mul(target.rotation,closest),delta=center-point;auto distance=delta.length();if(!distance.isZero())return {distance-radius,delta/distance,point};
    unsigned k=0;auto gap=h.x-abs(local.x);for(unsigned i=1;i<3;++i){auto g=component(h,i)-abs(component(local,i));if(g<gap){gap=g;k=i;}}
    auto n=target.axes[k]*(component(local,k)<D{}?-one():one());return {-radius-gap,n,center+n*gap};
}
std::optional<DetRigidSweepHit3D> sphereSweep(const Body& sphere,const Body& target,V displacement,const DetRigidBody3DConfig& c,bool skipSeparatingInitial){
    if(!bounded(displacement))invalid();D fraction{};
    for(unsigned i=0;i<c.maxSweepIterations;++i){auto center=sphere.p+displacement*fraction;auto gap=sphereGap(center,sphere.data.half.x,target);auto closing=-gap.outward.dot(displacement);
        if(!gap.distance.isFinite()||!gap.outward.isFinite()||!gap.targetPoint.isFinite()||!closing.isFinite())invalid();
        if(gap.distance<=c.continuousSlop){if(skipSeparatingInitial&&fraction.isZero()&&closing<=c.continuousSlop)return {};
            auto point=(center-gap.outward*sphere.data.half.x+gap.targetPoint)*half();return DetRigidSweepHit3D{sphere.id,target.id,fraction,point,-gap.outward};}
        if(closing<=D{})return {};auto next=fraction+(gap.distance-c.continuousSlop)/closing;
        if(next>one())return {};if(next<=fraction)throw std::invalid_argument("rigid3D sphere sweep: numeric resolution exhausted");fraction=next;
    }throw std::invalid_argument("rigid3D sphere sweep: iteration budget exhausted");
}
std::vector<DetRigidContact3D> allContacts(const std::vector<Body>& bodies,const DetRigidBody3DConfig& c,bool sensors=false,bool solver=true){std::vector<DetRigidContact3D> out;
    for(unsigned i=0;i<bodies.size();++i)for(unsigned j=i+1;j<bodies.size();++j){const auto& a=bodies[i];const auto& b=bodies[j];if(!filter(a,b)||((a.data.trigger||b.data.trigger)!=sensors))continue;
        if(solver&&!sensors&&a.inverseMass.isZero()&&b.inverseMass.isZero())continue;auto rows=contacts(a,b);
        for(const auto& row:rows)if(!row.point.isFinite()||!row.normal.isFinite()||!row.penetration.isFinite()||row.penetration<D{})invalid();
        out.insert(out.end(),rows.begin(),rows.end());if(out.size()>c.maxContacts)invalid();}return out;}
Body& find(std::vector<Body>& bodies,SimEntityId id){auto i=std::lower_bound(bodies.begin(),bodies.end(),id,[](const Body& b,auto key){return b.id<key;});if(i==bodies.end()||i->id!=id)invalid();return *i;}
V velocity(const Body& b,V point){return b.data.velocity+b.data.angularVelocity.cross(point-b.p);}
D effective(const Body& a,const Body& b,V pointA,V pointB,V n){auto ra=pointA-a.p,rb=pointB-b.p;auto value=a.inverseMass+b.inverseMass+n.dot(mul(a.worldInverse,ra.cross(n)).cross(ra)+mul(b.worldInverse,rb.cross(n)).cross(rb));if(!value.isFinite()||value<D{})invalid();return value;}
void impulse(Body& b,V point,V value){if(b.inverseMass.isZero())return;b.data.velocity+=value*b.inverseMass;b.data.angularVelocity+=mul(b.worldInverse,(point-b.p).cross(value));}
void angularImpulse(Body& b,V value){if(!b.inverseMass.isZero())b.data.angularVelocity+=mul(b.worldInverse,value);}
void rotate(Body& b,V delta){if(b.inverseMass.isZero()||delta==V{})return;auto length=delta.length();if(!length.isFinite())invalid();if(length>half())delta*=half()/length;auto q=b.q.integratedAngularVelocityWorld(delta,one());if(!q)invalid();b.q=*q;refresh(b);}
void positionImpulse(Body& b,V point,V value){if(b.inverseMass.isZero())return;auto r=point-b.p;auto angular=mul(b.worldInverse,r.cross(value));b.p+=value*b.inverseMass;rotate(b,angular);}
std::pair<V,V> tangentBasis(V n){unsigned k=0;for(unsigned i=1;i<3;++i)if(abs(component(n,i))<abs(component(n,k)))k=i;auto first=n.cross(axis(k)).normalized();if(!first)invalid();return {*first,n.cross(*first)};}
struct Row {DetRigidContact3D contact;D accumulated{},tangentA{},tangentB{},target{};V firstTangent{},secondTangent{};};
std::vector<Row> rows(std::vector<Body>& bodies,const DetRigidBody3DConfig& c){std::vector<Row> result;for(auto ct:allContacts(bodies,c)){auto& a=find(bodies,ct.first);auto& b=find(bodies,ct.second);auto n=ct.normal;auto vn=(velocity(b,ct.point)-velocity(a,ct.point)).dot(n);
        unsigned k=0;for(unsigned i=1;i<3;++i)if(abs(component(n,i))<abs(component(n,k)))k=i;auto tangent=n.cross(axis(k)).normalized();if(!tangent)invalid();
        result.push_back({ct,D{},D{},D{},vn< -c.restitutionThreshold?-max(a.data.restitution,b.data.restitution)*vn:D{},*tangent,n.cross(*tangent)});}return result;}
void solveContacts(std::vector<Body>& bodies,std::vector<Row>& rows){for(auto& r:rows){auto& a=find(bodies,r.contact.first);auto& b=find(bodies,r.contact.second);auto p=r.contact.point,n=r.contact.normal;auto denominator=effective(a,b,p,p,n);if(denominator<=D{})continue;
        auto delta=(r.target-(velocity(b,p)-velocity(a,p)).dot(n))/denominator;auto total=max(r.accumulated+delta,D{});delta=total-r.accumulated;r.accumulated=total;impulse(a,p,-n*delta);impulse(b,p,n*delta);
        auto limit=(a.data.friction*b.data.friction).sqrt()*total;
        for(unsigned k=0;k<2;++k){auto t=k==0?r.firstTangent:r.secondTangent;auto& sum=k==0?r.tangentA:r.tangentB;auto den=effective(a,b,p,p,t);if(den<=D{})continue;
            auto next=clamp(sum-(velocity(b,p)-velocity(a,p)).dot(t)/den,-limit,limit);auto d=next-sum;sum=next;impulse(a,p,-t*d);impulse(b,p,t*d);}
        auto tangentLength=math::DetVec2{r.tangentA,r.tangentB}.length();if(!tangentLength.isFinite())invalid();
        if(tangentLength>limit){auto scale=limit/tangentLength;auto nextA=r.tangentA*scale,nextB=r.tangentB*scale;auto correction=r.firstTangent*(nextA-r.tangentA)+r.secondTangent*(nextB-r.tangentB);
            impulse(a,p,-correction);impulse(b,p,correction);r.tangentA=nextA;r.tangentB=nextB;}
    }}
void translateContinuous(std::vector<Body>& bodies,const DetRigidBody3DConfig& c,D dt){D remaining=dt;unsigned hits=0;
    while(remaining>D{}){std::optional<DetRigidSweepHit3D> earliest;
        for(unsigned i=0;i<bodies.size();++i)for(unsigned j=i+1;j<bodies.size();++j){auto& a=bodies[i];auto& b=bodies[j];if((!a.data.continuous&&!b.data.continuous)||a.data.trigger||b.data.trigger||!filter(a,b))continue;
            const Body& moving=a.data.continuous?a:b;const Body& target=a.data.continuous?b:a;auto relative=(moving.data.velocity-target.data.velocity)*remaining;
            auto hit=sphereSweep(moving,target,relative,c,true);if(!hit)continue;hit->point+=target.data.velocity*(remaining*hit->fraction);
            if(!earliest||std::tuple{hit->fraction,hit->actor,hit->target}<std::tuple{earliest->fraction,earliest->actor,earliest->target})earliest=hit;
        }
        auto fraction=earliest?earliest->fraction:one();auto step=remaining*fraction;
        for(auto& b:bodies)if(b.data.mode!=DetRigidMode3D::Static)b.p+=b.data.velocity*step;
        if(!earliest)return;
        if(hits++>=c.maxContinuousHits)throw std::invalid_argument("rigid3D CCD: impact budget exhausted");
        auto& a=find(bodies,earliest->actor);auto& b=find(bodies,earliest->target);auto n=earliest->normal;
        auto vn=(velocity(b,earliest->point)-velocity(a,earliest->point)).dot(n);auto tangent=tangentBasis(n);
        std::vector<Row> contact{{{a.id,b.id,earliest->point,n,D{}},D{},D{},D{},vn< -c.restitutionThreshold?-max(a.data.restitution,b.data.restitution)*vn:D{},tangent.first,tangent.second}};
        for(unsigned i=0;i<c.velocityIterations;++i)solveContacts(bodies,contact);
        remaining-=step;
    }
}
void solveJoints(std::vector<Body>& bodies,const DetRigidBody3DConfig& c,D dt,bool position,std::array<D,8>& motors){
    for(unsigned index=0;index<c.joints.size();++index){const auto& j=c.joints[index];auto& a=find(bodies,j.first);auto& b=find(bodies,j.second);
        auto anchors=[&]{return std::pair{a.p+mul(a.rotation,j.anchorFirst),b.p+mul(b.rotation,j.anchorSecond)};};
        auto linear=[&](V n,D error){auto points=anchors();auto denominator=effective(a,b,points.first,points.second,n);if(denominator<=D{})return;
            auto value=position?-c.correction*error/denominator:(-c.correction*error/dt-(velocity(b,points.second)-velocity(a,points.first)).dot(n))/denominator;
            if(position){positionImpulse(a,points.first,-n*value);positionImpulse(b,points.second,n*value);}else{impulse(a,points.first,-n*value);impulse(b,points.second,n*value);}};
        auto angular=[&](V n,D error){auto denominator=n.dot(mul(a.worldInverse,n)+mul(b.worldInverse,n));if(denominator<=D{})return;
            auto value=position?-c.correction*error/denominator:(-c.correction*error/dt-(b.data.angularVelocity-a.data.angularVelocity).dot(n))/denominator;
            if(position){rotate(a,mul(a.worldInverse,-n*value));rotate(b,mul(b.worldInverse,n*value));}else{angularImpulse(a,-n*value);angularImpulse(b,n*value);}};
        auto n=unit(mul(a.rotation,unit(j.axisFirst)));auto tangent=tangentBasis(n);
        if(j.kind==DetJointKind3D::Slider){for(auto t:{tangent.first,tangent.second}){auto points=anchors();linear(t,(points.second-points.first).dot(t));}}
        else if(j.kind==DetJointKind3D::Distance){auto points=anchors();auto difference=points.second-points.first;auto length=difference.length();linear(length.isZero()?axis(0):difference/length,length-j.restLength);}
        else for(unsigned k=0;k<3;++k){auto points=anchors();linear(axis(k),(points.second-points.first).dot(axis(k)));}
        if(j.kind==DetJointKind3D::Fixed||j.kind==DetJointKind3D::Slider){auto desired=a.q*j.referenceRotation;auto error=b.q*desired.conjugate();if(error.w<D{})error=-error;V rotationError{error.x*D::fromInt(2),error.y*D::fromInt(2),error.z*D::fromInt(2)};
            for(unsigned k=0;k<3;++k)angular(axis(k),rotationError.dot(axis(k)));}
        else if(j.kind==DetJointKind3D::Hinge){n=unit(mul(a.rotation,unit(j.axisFirst)));auto other=unit(mul(b.rotation,unit(j.axisSecond)));tangent=tangentBasis(n);auto cross=n.cross(other);auto length=cross.length();auto cosine=clamp(n.dot(other),-one(),one());
            V rotationError=length.isZero()?(cosine<D{}?tangent.first*math::kDetPi:V{}):cross*(math::detAtan2(length,cosine)/length);
            angular(tangent.first,rotationError.dot(tangent.first));angular(tangent.second,rotationError.dot(tangent.second));}
        if(j.kind!=DetJointKind3D::Hinge&&j.kind!=DetJointKind3D::Slider)continue;
        n=unit(mul(a.rotation,unit(j.axisFirst)));auto points=anchors();const bool hinge=j.kind==DetJointKind3D::Hinge;
        auto error=b.q*(a.q*j.referenceRotation).conjugate();if(error.w<D{})error=-error;
        const D coordinate=hinge?D::fromInt(2)*math::detAtan2(V{error.x,error.y,error.z}.dot(n),error.w):(points.second-points.first).dot(n);
        auto relative=[&]{return hinge?(b.data.angularVelocity-a.data.angularVelocity).dot(n):(velocity(b,points.second)-velocity(a,points.first)).dot(n);};
        auto denominator=hinge?n.dot(mul(a.worldInverse,n)+mul(b.worldInverse,n)):effective(a,b,points.first,points.second,n);if(denominator<=D{})continue;
        auto apply=[&](D value){if(hinge){angularImpulse(a,-n*value);angularImpulse(b,n*value);}else{impulse(a,points.first,-n*value);impulse(b,points.second,n*value);}};
        if(position){if(j.limits){auto deviation=coordinate-clamp(coordinate,j.lower,j.upper);if(hinge)angular(n,deviation);else linear(n,deviation);}continue;}
        if(j.motor){auto delta=(j.targetSpeed-relative())/denominator;auto limit=j.maxMotorForce*dt;auto total=clamp(motors[index]+delta,-limit,limit);apply(total-motors[index]);motors[index]=total;}
        if(j.limits){auto lower=(j.lower-coordinate)/dt,upper=(j.upper-coordinate)/dt;if(coordinate<j.lower)lower*=c.correction;if(coordinate>j.upper)upper*=c.correction;
            auto current=relative();auto desired=clamp(current,lower,upper);if(desired!=current)apply((desired-current)/denominator);}
    }
}
void solvePositions(std::vector<Body>& bodies,const DetRigidBody3DConfig& c){for(auto ct:allContacts(bodies,c)){auto& a=find(bodies,ct.first);auto& b=find(bodies,ct.second);auto denominator=effective(a,b,ct.point,ct.point,ct.normal);if(denominator<=D{})continue;
    auto depth=max(ct.penetration-c.penetrationSlop,D{});auto value=c.correction*depth/denominator;positionImpulse(a,ct.point,-ct.normal*value);positionImpulse(b,ct.point,ct.normal*value);}}
std::vector<Body> checkpointBodies(const DetSessionCheckpoint& cp,const DetRigidBody3DConfig& c,const Layouts& l){if(cp.globals.at(c.policySchema)!=l.policy.defaults()||cp.globals.at(c.jointSchema)!=l.joints.defaults())invalid();std::vector<Body> out;
    for(const auto& [id,state]:cp.actors){if(state.blocks.at(c.policySchema)!=l.policy.defaults()||state.blocks.at(c.jointSchema)!=l.joints.defaults()||state.blocks.at(c.historySchema)!=l.history.defaults())invalid();auto b=readBody(l.body,state.blocks.at(c.bodySchema));
        if(b.mode==DetRigidMode3D::Disabled)continue;if(!state.pose.rotationEnabled)invalid();auto body=makeBody(id,b,V::fromBits(state.pose.position),Q::fromBits(state.pose.rotation));representable(body);out.push_back(body);if(out.size()>c.maxBodies)invalid();}
    (void)continuousScene(out,c);for(const auto& j:c.joints){auto& a=find(out,j.first);auto& b=find(out,j.second);if(a.inverseMass.isZero()&&b.inverseMass.isZero())invalid();}return out;}
std::vector<Pair> history(const DetSessionCheckpoint& cp,const DetRigidBody3DConfig& c,const Layouts& l){const auto& words=cp.globals.at(c.historySchema);auto count=std::get<std::uint32_t>(l.history.read(words,2));auto tick=std::get<std::uint64_t>(l.history.read(words,3));
    if(count>c.maxTriggerPairs||tick!=cp.nextTick||std::get<std::uint32_t>(l.history.read(words,1))!=1)invalid();std::vector<Pair> out;
    for(unsigned i=0;i<c.maxTriggerPairs;++i){Pair p{std::get<DetEntityRef>(l.history.read(words,10+2*i)).value,std::get<DetEntityRef>(l.history.read(words,11+2*i)).value};if(i>=count){if(p!=Pair{})invalid();continue;}
        auto known=[&](auto id){return cp.actors.contains(id)||std::binary_search(cp.retiredIds.begin(),cp.retiredIds.end(),id);};
        if(!p.first||p.first>=p.second||!known(p.first)||!known(p.second)||(!out.empty()&&p<=out.back()))invalid();out.push_back(p);}return out;}
void tick(DetTickContext& context,const DetRigidBody3DConfig& c,const Layouts&){std::vector<Body> bodies;
    for(auto id:context.entities()){auto data=detRigidBodyState3D(context,c,id);if(data.mode==DetRigidMode3D::Disabled)continue;auto& pose=context.pose(id);if(!pose.rotationEnabled)invalid();bodies.push_back(makeBody(id,data,pose.position,pose.rotation));if(bodies.size()>c.maxBodies)invalid();}
    for(const auto& schema:{policySchema(c),jointSchema(c)})for(const auto& field:schema.fields)
        std::visit([&](auto value){using T=decltype(value);if(encodeDetStateValue(context.readGlobal<T>(schema.id,field.id))!=encodeDetStateValue(value))invalid();},field.initial);
    const bool continuous=continuousScene(bodies,c);auto dt=context.dt()/D::fromUInt(c.substeps);if(!dt.isFinite()||dt<=D{})invalid();
    for(unsigned sub=0;sub<c.substeps;++sub){
        for(auto& b:bodies){if(b.data.mode==DetRigidMode3D::Dynamic){b.data.velocity+=(c.gravity+b.data.force*b.inverseMass)*dt;
                auto localOmega=mul(b.rotation.transposed(),b.data.angularVelocity);auto momentum=mul(b.rotation,{b.inertia.x*localOmega.x,b.inertia.y*localOmega.y,b.inertia.z*localOmega.z});
                b.data.angularVelocity+=mul(b.worldInverse,b.data.torque-b.data.angularVelocity.cross(momentum))*dt;
                b.data.velocity*=max(one()-b.data.linearDamping*dt,D{});b.data.angularVelocity*=max(one()-b.data.angularDamping*dt,D{});}
            if(b.data.mode!=DetRigidMode3D::Static){if(!continuous)b.p+=b.data.velocity*dt;auto angle=b.data.angularVelocity.length()*dt;if(!angle.isFinite()||angle>one())invalid();auto q=b.q.integratedAngularVelocityWorld(b.data.angularVelocity,dt);if(!q)invalid();b.q=*q;refresh(b);}}
        if(continuous)translateContinuous(bodies,c,dt);std::array<D,8> motors{};auto contactRows=rows(bodies,c);for(unsigned i=0;i<c.velocityIterations;++i){solveJoints(bodies,c,dt,false,motors);solveContacts(bodies,contactRows);}
        for(unsigned i=0;i<c.positionIterations;++i){solveJoints(bodies,c,dt,true,motors);solvePositions(bodies,c);}
    for(auto& b:bodies){validateBody(b.data);if(!bounded(b.p))invalid();representable(b);}
    }
    // Keep precisely the solver's rotation bits; a setter's extra normalization
    // could change final geometry after trigger pairs were computed. Preflight
    // complete snapshots before publication, retaining two logical pose revisions.
    std::vector<std::pair<SimEntityId,DetSimTransformComponent::Snapshot>> poses;
    for(const auto& b:bodies){auto snapshot=context.pose(b.id).snapshot();snapshot.position=b.p.bits();snapshot.rotation=b.q.bits();snapshot.revision+=2;
        DetSimTransformComponent check;if(!check.restore(snapshot))invalid();poses.emplace_back(b.id,snapshot);}
    std::vector<Pair> present;for(auto ct:allContacts(bodies,c,true)){Pair pair{ct.first,ct.second};if(present.empty()||present.back()!=pair)present.push_back(pair);}if(present.size()>c.maxTriggerPairs)invalid();
    auto oldCount=context.readGlobal<std::uint32_t>(c.historySchema,2);if(oldCount>c.maxTriggerPairs)invalid();std::vector<Pair> previous;for(unsigned i=0;i<oldCount;++i)previous.emplace_back(context.readGlobal<DetEntityRef>(c.historySchema,10+2*i).value,context.readGlobal<DetEntityRef>(c.historySchema,11+2*i).value);
    std::vector<DetRigidTrigger3D> events;for(auto p:present)events.push_back({std::binary_search(previous.begin(),previous.end(),p)?DetRigidTriggerPhase3D::Stay:DetRigidTriggerPhase3D::Enter,p.first,p.second});
    for(auto p:previous)if(!std::binary_search(present.begin(),present.end(),p))events.push_back({DetRigidTriggerPhase3D::Exit,p.first,p.second});
    std::sort(events.begin(),events.end(),[](auto a,auto b){return std::tuple{a.first,a.second,a.phase}<std::tuple{b.first,b.second,b.phase};});if(!context.canEmitEvents(events.size(),events.size()*24))invalid();
    // All geometry, state, contact and event budgets are checked before any commit.
    for(unsigned i=0;i<bodies.size();++i){auto& b=bodies[i];if(poses[i].first!=b.id||!context.pose(b.id).restore(poses[i].second))invalid();context.write(b.id,c.bodySchema,5,b.data.velocity);context.write(b.id,c.bodySchema,6,b.data.angularVelocity);context.write(b.id,c.bodySchema,7,V{});context.write(b.id,c.bodySchema,8,V{});}
    context.writeGlobal(c.historySchema,2,static_cast<std::uint32_t>(present.size()));context.writeGlobal(c.historySchema,3,context.tick()+1);
    for(unsigned i=0;i<c.maxTriggerPairs;++i){auto p=i<present.size()?present[i]:Pair{};context.writeGlobal(c.historySchema,10+2*i,DetEntityRef{p.first});context.writeGlobal(c.historySchema,11+2*i,DetEntityRef{p.second});}
    for(auto e:events){auto bytes=encodeDetRigidTrigger3D(e);context.emit(c.eventType,bytes);}
}
}
DetTypedStateSchema detRigidBodySchema3D(const DetRigidBody3DConfig& c){if(!validConfig(c))invalid();return bodySchema(c);}
DetActorState detRigidBodyActor3D(const DetRigidBody3DConfig& c,const DetRigidBody3D& b,V p,Q q){if(!validConfig(c)||!bounded(p))invalid();validateBody(b);auto unit=q.normalized();if(!unit)invalid();if(b.mode!=DetRigidMode3D::Disabled)representable(makeBody(1,b,p,*unit));
    DetActorState state;state.pose.position=p.bits();state.pose.previousPosition=p.bits();state.pose.rotation=unit->bits();state.pose.previousRotation=unit->bits();state.pose.rotationEnabled=true;
    DetStateLayout l(bodySchema(c));auto words=l.defaults();l.write(words,1,static_cast<std::uint32_t>(b.mode));l.write(words,2,static_cast<std::uint32_t>(b.shape));l.write(words,3,b.half);l.write(words,4,b.mass);l.write(words,5,b.velocity);l.write(words,6,b.angularVelocity);l.write(words,7,b.force);l.write(words,8,b.torque);l.write(words,9,b.restitution);l.write(words,10,b.friction);l.write(words,11,b.layer);l.write(words,12,b.mask);l.write(words,13,b.trigger);l.write(words,14,b.linearDamping);l.write(words,15,b.angularDamping);l.write(words,16,b.continuous);state.blocks[c.bodySchema]=std::move(words);return state;}
bool installDetRigidBody3D(DeterministicSession& session,DetRigidBody3DConfig c){if(!validConfig(c))return false;Layouts l(c);
    if(!(session.registerTypedSchema(bodySchema(c))&&session.registerTypedSchema(policySchema(c))&&session.registerGlobalState(c.policySchema)&&session.registerTypedSchema(jointSchema(c))&&session.registerGlobalState(c.jointSchema)&&session.registerTypedSchema(historySchema(c))&&session.registerGlobalState(c.historySchema)))return false;
    return session.registerLogicProfile(c.logicProfileId,kDetRigidBody3DProfileVersion,logicHash)&&session.registerValidator(c.validatorId,1,[c,l](const auto& cp,std::string& error){try{(void)checkpointBodies(cp,c,l);(void)history(cp,c,l);return true;}catch(const std::exception& e){error=e.what();return false;}})
        &&session.registerSystem(c.systemId,c.priority,[c,l](DetTickContext& context){tick(context,c,l);return true;});}
DetRigidBody3D detRigidBodyState3D(DetTickContext& context,const DetRigidBody3DConfig& c,SimEntityId id){if(!validConfig(c))invalid();DetRigidBody3D b;
    b.mode=static_cast<DetRigidMode3D>(context.read<std::uint32_t>(id,c.bodySchema,1));b.shape=static_cast<DetRigidShape3D>(context.read<std::uint32_t>(id,c.bodySchema,2));b.half=context.read<V>(id,c.bodySchema,3);b.mass=context.read<D>(id,c.bodySchema,4);b.velocity=context.read<V>(id,c.bodySchema,5);b.angularVelocity=context.read<V>(id,c.bodySchema,6);b.force=context.read<V>(id,c.bodySchema,7);b.torque=context.read<V>(id,c.bodySchema,8);b.restitution=context.read<D>(id,c.bodySchema,9);b.friction=context.read<D>(id,c.bodySchema,10);b.layer=context.read<std::uint32_t>(id,c.bodySchema,11);b.mask=context.read<std::uint32_t>(id,c.bodySchema,12);b.trigger=context.read<bool>(id,c.bodySchema,13);b.linearDamping=context.read<D>(id,c.bodySchema,14);b.angularDamping=context.read<D>(id,c.bodySchema,15);b.continuous=context.read<bool>(id,c.bodySchema,16);validateBody(b);return b;}
void detRigidBodyForce3D(DetTickContext& ctx,const DetRigidBody3DConfig& c,SimEntityId id,V force,V torque){auto b=detRigidBodyState3D(ctx,c,id);if(b.mode!=DetRigidMode3D::Dynamic||!bounded(force)||!bounded(torque))invalid();auto f=b.force+force,t=b.torque+torque;if(!bounded(f)||!bounded(t))invalid();ctx.write(id,c.bodySchema,7,f);ctx.write(id,c.bodySchema,8,t);}
std::vector<DetRigidContact3D> detRigidContacts3D(const DetSessionCheckpoint& cp,const DetRigidBody3DConfig& c){if(!validConfig(c))invalid();Layouts l(c);auto bodies=checkpointBodies(cp,c,l);return allContacts(bodies,c,false,false);}
std::optional<DetRigidSweepHit3D> detRigidSphereSweep3D(const DetSessionCheckpoint& cp,const DetRigidBody3DConfig& c,SimEntityId actor,V displacement,bool includeTriggers){
    if(!validConfig(c)||!bounded(displacement))invalid();Layouts l(c);auto bodies=checkpointBodies(cp,c,l);const auto& sphere=find(bodies,actor);if(sphere.data.shape!=DetRigidShape3D::Sphere)invalid();std::optional<DetRigidSweepHit3D> first;
    for(const auto& target:bodies){if(target.id==actor||!filter(sphere,target)||(!includeTriggers&&(sphere.data.trigger||target.data.trigger)))continue;auto hit=sphereSweep(sphere,target,displacement,c,false);
        if(hit&&(!first||std::pair{hit->fraction,hit->target}<std::pair{first->fraction,first->target}))first=hit;}return first;
}
std::vector<std::uint8_t> encodeDetRigidTrigger3D(DetRigidTrigger3D e){auto phase=static_cast<std::uint32_t>(e.phase);if(phase<1||phase>3||!e.first||e.first>=e.second)invalid();std::vector<std::uint8_t> out;auto put=[&](auto v,unsigned count){for(unsigned i=0;i<count;++i)out.push_back(static_cast<std::uint8_t>(v>>(8*i)));};put(std::uint64_t{1},4);put(std::uint64_t{phase},4);put(e.first,8);put(e.second,8);return out;}
bool decodeDetRigidTrigger3D(std::span<const std::uint8_t> bytes,DetRigidTrigger3D& out){if(bytes.size()!=24)return false;auto get=[&](unsigned start,unsigned count){std::uint64_t v=0;for(unsigned i=0;i<count;++i)v|=std::uint64_t{bytes[start+i]}<<(8*i);return v;};auto phase=get(4,4);DetRigidTrigger3D e{static_cast<DetRigidTriggerPhase3D>(phase),get(8,8),get(16,8)};if(get(0,4)!=1||phase<1||phase>3||!e.first||e.first>=e.second)return false;out=e;return true;}
}
