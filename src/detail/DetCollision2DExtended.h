// Included inside the collision implementation's private namespace.
// design reference: AYEntity/design.md Stage26/28-31 whole-world trials, motion conflicts and translating terrain.
struct TraceStep {std::vector<Body> before,after;};
struct SolidContact {SimEntityId first,second;std::uint32_t axis;};
struct StepTrialRejected {};
struct ContactLimitExceeded : std::runtime_error {
    ContactLimitExceeded():std::runtime_error("deterministic collision 2D: contact iteration limit / crushing"){}
};
struct MotionStop {SimEntityId id,other;std::uint32_t axis;DetCollisionMotionResult2D result;};
struct PrescribedMotionConflict : std::runtime_error {
    std::vector<MotionStop> movers,actors;
    PrescribedMotionConflict():std::runtime_error("deterministic collision 2D: prescribed motion conflict"){}
};
void markMotion(Body& body,DetCollisionMotionResult2D result,SimEntityId other,std::uint32_t n) {
    // First contact wins within one severity. A proved pinch dominates a clip.
    if(static_cast<std::uint32_t>(result)>static_cast<std::uint32_t>(body.motionResult)){
        body.motionResult=result;body.motionOther=other;body.motionAxis=n;
    }
}
bool prescribedPair(const Body& a,const Body& b) {
    const auto prescribed=[](const Body& body){return body.data.mode==DetBodyMode2D::MovingObstacle;};
    return !a.data.oneWay && !b.data.oneWay && (prescribed(a) || prescribed(b))
        && a.data.mode!=DetBodyMode2D::Kinematic && b.data.mode!=DetBodyMode2D::Kinematic;
}
bool solidPair(const Body& a,const Body& b,bool stopPrescribed=false) {
    return !a.data.trigger && !b.data.trigger && (a.data.mode==DetBodyMode2D::Kinematic || b.data.mode==DetBodyMode2D::Kinematic
        || (stopPrescribed && prescribedPair(a,b)))
        && masks(a.data.layer,a.data.mask,b.data.layer,b.data.mask);
}
void validateSolids(const std::vector<Body>& bodies,bool stopPrescribed=false) {
    for(std::size_t i=0;i<bodies.size();++i)for(std::size_t j=i+1;j<bodies.size();++j)
        if(solidPair(bodies[i],bodies[j],stopPrescribed) && !bodies[i].data.oneWay && !bodies[j].data.oneWay
            && interior(bodies[i].box,bodies[i].data.shape,bodies[j].box,bodies[j].data.shape))invalid();
}
PrescribedMotionConflict prescribedBlock(const Body& a,const Body& b,std::uint32_t n) {
    PrescribedMotionConflict result;
    if(a.data.mode==DetBodyMode2D::MovingObstacle)result.movers.push_back({a.id,b.id,n,DetCollisionMotionResult2D::Blocked});
    if(b.data.mode==DetBodyMode2D::MovingObstacle)result.movers.push_back({b.id,a.id,n,DetCollisionMotionResult2D::Blocked});
    return result;
}
// Only a direct, represented opposed-face contradiction is recoverable. A
// kinematic chain, nearby gap, ordinary pass exhaustion or one-way passage does
// not constitute proof. Displacement bounds use this actual residual interval.
std::optional<PrescribedMotionConflict> opposedMotion(const std::vector<Body>& bodies,const std::vector<V>& remaining) {
    for(std::size_t i=0;i<bodies.size();++i){const auto& actor=bodies[i];
        if(actor.data.mode!=DetBodyMode2D::Kinematic || actor.data.trigger || actor.data.oneWay)continue;
        for(std::uint32_t n=0;n<2;++n){
            std::optional<std::size_t> lower,upper;
            for(std::size_t j=0;j<bodies.size();++j){const auto& wall=bodies[j];
                if(i==j || wall.data.trigger || wall.data.oneWay
                    || (wall.data.shape!=DetCollisionShape2D::Aabb && !wall.movingSlopeProfile)
                    || (wall.data.mode!=DetBodyMode2D::Static && wall.data.mode!=DetBodyMode2D::MovingObstacle)
                    || !masks(actor.data.layer,actor.data.mask,wall.data.layer,wall.data.mask))continue;
                const auto tangent=1u-n;
                const auto edge=[](const V& v,std::uint32_t axis){return axis==0?v.x:v.y;};
                if(!(edge(actor.box.min,tangent)<edge(wall.box.max,tangent) && edge(wall.box.min,tangent)<edge(actor.box.max,tangent)))continue;
                const bool wedge=wall.data.shape!=DetCollisionShape2D::Aabb;
                const bool right=wall.data.shape==DetCollisionShape2D::SlopeRisesRight;
                const auto relative=remaining[i]-remaining[j];
                const bool cap=!wedge || (right?(actor.box.max.x>wall.box.max.x || (actor.box.max.x==wall.box.max.x && relative.x>=D{}))
                    :(actor.box.min.x<wall.box.min.x || (actor.box.min.x==wall.box.min.x && relative.x<=D{})));
                const bool lowerFace=!wedge || (n==0?right:cap);
                const bool upperFace=!wedge || (n==0?!right:true);
                if(lowerFace && edge(wall.box.max,n)==edge(actor.box.min,n)
                    && (!lower || edge(remaining[*lower],n)<edge(remaining[j],n)))lower=j;
                if(upperFace && edge(wall.box.min,n)==edge(actor.box.max,n)
                    && (!upper || edge(remaining[j],n)<edge(remaining[*upper],n)))upper=j;
            }
            if(!lower || !upper)continue;
            const auto value=[&](std::size_t index){return n==0?remaining[index].x:remaining[index].y;};
            if(!(value(*lower)>value(*upper)))continue;
            PrescribedMotionConflict conflict;
            for(auto j:{*lower,*upper})if(bodies[j].data.mode==DetBodyMode2D::MovingObstacle)
                conflict.movers.push_back({bodies[j].id,actor.id,n,DetCollisionMotionResult2D::CrushPrevented});
            if(conflict.movers.empty())continue;
            std::sort(conflict.movers.begin(),conflict.movers.end(),[](const auto& a,const auto& b){return a.id<b.id;});
            conflict.actors.push_back({actor.id,conflict.movers.front().id,n,DetCollisionMotionResult2D::CrushPrevented});
            return conflict;
        }
    }
    return {};
}
// Profile3 top-plane sweep. Work in the platform frame; the ordinary full-box
// sweep cannot represent a transparent side/bottom or an intentional interior passage.
std::optional<math::DetHit2D> wedgePairHit(const Body& a,const Body& b,V relative) {
    const auto vertices=[](const Body& p){return std::array<V,3>{p.box.min,V{p.box.max.x,p.box.min.y},
        p.data.shape==DetCollisionShape2D::SlopeRisesRight?p.box.max:V{p.box.min.x,p.box.max.y}};};
    const auto av=vertices(a),bv=vertices(b);const auto one=D::fromInt(1);
    D enter=D::fromBits(0xff800000u),exit=D::fromBits(0x7f800000u);V normal{};std::uint32_t feature=0;bool selected=false;
    const std::array<V,4> normals{V{one,D{}},V{D{},one},slope(a.box,a.data.shape).surfaceNormal(),slope(b.box,b.data.shape).surfaceNormal()};
    for(unsigned k=0;k<normals.size();++k){const auto n=normals[k];
        const auto dot=[&](V p){const auto x=n.x*p.x,y=n.y*p.y,value=x+y;
            if(!x.isFinite() || !y.isFinite() || !value.isFinite())invalid();return value;};
        // Project relative to a local origin, avoiding unnecessary large world products.
        auto amin=dot(av[0]-a.box.min),amax=amin,bmin=dot(bv[0]-a.box.min),bmax=bmin;
        for(unsigned v=1;v<3;++v){const auto ap=dot(av[v]-a.box.min),bp=dot(bv[v]-a.box.min);
            amin=std::min(amin,ap);amax=std::max(amax,ap);bmin=std::min(bmin,bp);bmax=std::max(bmax,bp);}
        const auto speed=dot(relative);
        if(speed.isZero()){if(amax<=bmin || bmax<=amin)return {};continue;}
        const auto gapLow=bmin-amax,gapHigh=bmax-amin;
        if(!gapLow.isFinite() || !gapHigh.isFinite())invalid();
        const auto lo=(speed>D{}?gapLow:gapHigh)/speed;
        const auto hi=(speed>D{}?gapHigh:gapLow)/speed;
        if(lo.isNaN() || hi.isNaN())invalid();
        if(lo>enter || (!selected && lo==enter)){enter=lo;normal=speed>D{}?-n:n;feature=k<2?k:2;selected=true;}
        exit=std::min(exit,hi);if(enter>exit)return {};
    }
    if(interior(a.box,a.data.shape,b.box,b.data.shape))return math::DetHit2D{D{},V{},0,true};
    if(!selected || enter<D{} || enter>one || !(exit>enter))return {};
    return math::DetHit2D{enter,normal,feature,false};
}
std::optional<math::DetHit2D> geometricHit(const Body& a,const Body& b,V relative) {
    if(a.data.shape!=DetCollisionShape2D::Aabb && b.data.shape!=DetCollisionShape2D::Aabb)return wedgePairHit(a,b,relative);
    if(a.data.shape!=DetCollisionShape2D::Aabb || b.data.shape!=DetCollisionShape2D::Aabb){
        const bool platformFirst=a.data.shape!=DetCollisionShape2D::Aabb;
        const auto& wall=platformFirst?a:b;const auto& actor=platformFirst?b:a;
        const auto hit=math::detSweepAabbSlope2D(actor.box,platformFirst?-relative:relative,slope(wall.box,wall.data.shape));
        if(!hit)return {};
        return math::DetHit2D{hit->fraction,platformFirst?-hit->normal:hit->normal,hit->axis,hit->initialOverlap};
    }
    return math::detSweepAabb2D(a.box,relative,b.box);
}
std::optional<math::DetHit2D> solidHit(const Body& a,const Body& b,V relative) {
    if(a.data.shape!=DetCollisionShape2D::Aabb || b.data.shape!=DetCollisionShape2D::Aabb){
        auto hit=geometricHit(a,b,relative);
        const bool firstWall=a.data.shape!=DetCollisionShape2D::Aabb;
        const auto& wall=firstWall?a:b;const auto& actor=firstWall?b:a;
        if(hit && !hit->initialOverlap && hit->axis==0 && actor.terrain && actor.slopeGrounded
            && actor.data.mode==DetBodyMode2D::Kinematic && wall.data.shape!=DetCollisionShape2D::Aabb){
            const auto motion=firstWall?-relative:relative;const auto n=slope(wall.box,wall.data.shape).surfaceNormal();
            const auto rise=(n.x<D{}?-n.x:n.x)/n.y;
            const bool right=wall.data.shape==DetCollisionShape2D::SlopeRisesRight;
            const auto corner=(right?actor.box.max.x:actor.box.min.x)+motion.x*hit->fraction;
            const auto foot=actor.box.min.y+motion.y*hit->fraction;
            if(rise<=actor.maxSlopeRisePerRun && (right?motion.x>D{}:motion.x<D{})
                && corner==(right?wall.box.min.x:wall.box.max.x) && foot==wall.box.min.y){
                // The low endpoint has zero vertical side area. Only a proved
                // grounded, level seam promotes its X tie to the true walkable plane.
                hit->axis=2;hit->normal=firstWall?-n:n;
            }
        }
        return hit;
    }
    if(!a.data.oneWay && !b.data.oneWay)return math::detSweepAabb2D(a.box,relative,b.box);
    const bool platformFirst=a.data.oneWay;
    const auto& platform=platformFirst?a:b;
    const auto& actor=platformFirst?b:a;
    if(actor.data.mode!=DetBodyMode2D::Kinematic || actor.data.ignoredOneWay.value==platform.id)return {};
    const auto motion=platformFirst?-relative:relative;
    if(!(motion.y<D{}) || actor.box.min.y<platform.box.max.y)return {};
    const auto gap=actor.box.min.y-platform.box.max.y;
    if(!gap.isFinite())invalid();
    if(gap>-motion.y)return {};
    const auto fraction=gap/(-motion.y);
    const auto dx=motion.x*fraction;
    const auto minX=actor.box.min.x+dx,maxX=actor.box.max.x+dx;
    if(!fraction.isFinite() || !dx.isFinite() || !minX.isFinite() || !maxX.isFinite())invalid();
    if(!(minX<platform.box.max.x && platform.box.min.x<maxX))return {};
    return math::DetHit2D{fraction,V{D{},D::fromInt(platformFirst?-1:1)},1,false};
}
struct SlopeSurfaceChange {std::size_t actor,wall;D fraction;bool uphill;};
D terrainOwnX(const Body& actor){return std::clamp(actor.data.velocity.x-actor.slopeCarry.x,
    std::min(D{},actor.slopeOwnIntentX),std::max(D{},actor.slopeOwnIntentX));}
std::optional<SlopeSurfaceChange> surfaceChange(const std::vector<Body>& bodies,const std::vector<V>& remaining,SimEntityId trialActor) {
    std::optional<SlopeSurfaceChange> best;
    for(std::size_t i=0;i<bodies.size();++i){const auto& actor=bodies[i];
        if(actor.id==trialActor || !actor.slopeLimited || !actor.slopeGrounded || actor.data.trigger
            || actor.data.mode!=DetBodyMode2D::Kinematic)continue;
        for(std::size_t j=0;j<bodies.size();++j){const auto& wall=bodies[j];
            if((wall.data.mode!=DetBodyMode2D::Static && wall.data.mode!=DetBodyMode2D::MovingObstacle) || wall.data.shape==DetCollisionShape2D::Aabb
                || !masks(actor.data.layer,actor.data.mask,wall.data.layer,wall.data.mask))continue;
            const auto motion=remaining[i]-remaining[j];if(!motion.isFinite())invalid();if(motion.x.isZero())continue;
            const auto wedge=slope(wall.box,wall.data.shape);
            const auto normal=wedge.surfaceNormal();
            const auto rise=(normal.x<D{}?-normal.x:normal.x)/normal.y;if(!rise.isFinite())invalid();
            if(rise>actor.maxSlopeRisePerRun)continue;
            const bool right=wall.data.shape==DetCollisionShape2D::SlopeRisesRight;
            const auto high=right?wall.box.max.x:wall.box.min.x;
            const auto corner=right?actor.box.max.x:actor.box.min.x;
            const bool uphill=right?motion.x>D{}:motion.x<D{};
            D fraction;
            if(uphill){
                // This registered pre-pass support proof excludes jumps and
                // incidental airborne near-contact. A represented snap may sit
                // slightly outside the plane, so use the actual bound support.
                if(actor.slopeSupport!=wall.id || !(motion.y>D{}))continue;
                const auto height=wedge.surfaceHeightAtFootprint(actor.box);
                if(!height || actor.box.min.y<*height || actor.box.min.y>wall.box.max.y)continue;
                if(right?corner>=high:corner<=high){
                    // The wedge can still be the stable-ID support after the
                    // high corner crossed in the previous tick. Its top-flat
                    // configuration-space segment has zero vertical derivative.
                    if(actor.box.min.y!=wall.box.max.y)continue;
                    fraction={};
                }else {
                    const auto gap=high-corner;if(!gap.isFinite())invalid();fraction=gap/motion.x;
                }
            }else {
                // Highest footprint corner must first leave the top-flat part
                // of the configuration polygon before its downward slope begins.
                if(!motion.y.isZero() || actor.box.min.y!=wall.box.max.y || (right?corner<high:corner>high))continue;
                const auto gap=high-corner;if(!gap.isFinite())invalid();fraction=gap/motion.x;
            }
            if(!fraction.isFinite())invalid();if(fraction<D{} || fraction>D::fromInt(1))continue;
            // Stable actor/wedge iteration order resolves surface-event ties.
            if(!best || fraction<best->fraction)best=SlopeSurfaceChange{i,j,fraction,uphill};
        }
    }
    return best;
}
void moveTogether(std::vector<Body>& bodies,D dt,std::uint32_t iterations,std::vector<TraceStep>& trace,
    std::vector<SolidContact>* contacts=nullptr,SimEntityId trialActor=0,bool stopPrescribed=false,bool staticSlopes=false) {
    validateSolids(bodies,stopPrescribed);std::vector<V> remaining;
    for(const auto& b:bodies){const auto d=b.data.mode==DetBodyMode2D::Static?V{}:b.data.velocity*dt;if(!d.isFinite())invalid();remaining.push_back(d);}
    bool finished=false;D remainingTime=dt;const auto one=D::fromInt(1),half=D::fromBits(0x3f000000u);
    for(std::uint32_t pass=0;pass<iterations;++pass){
        if(stopPrescribed)if(auto conflict=opposedMotion(bodies,remaining))throw std::move(*conflict);
        std::optional<math::DetHit2D> best;std::optional<D> dynamicContactTime;std::size_t first=0,second=0;
        for(std::size_t i=0;i<bodies.size();++i)for(std::size_t j=i+1;j<bodies.size();++j)if(solidPair(bodies[i],bodies[j],stopPrescribed)){
            const auto relative=remaining[i]-remaining[j];if(!relative.isFinite())invalid();
            const auto hit=solidHit(bodies[i],bodies[j],relative);
            if(hit && hit->initialOverlap)invalid();
            if(hit && trialActor && (bodies[i].id==trialActor || bodies[j].id==trialActor)){
                const auto& other=bodies[i].id==trialActor?bodies[j]:bodies[i];
                if((other.data.mode!=DetBodyMode2D::Static || other.data.oneWay || other.data.shape!=DetCollisionShape2D::Aabb)
                    && (!dynamicContactTime || hit->fraction<*dynamicContactTime))dynamicContactTime=hit->fraction;
            }
            if(hit && (!best || hit->fraction<best->fraction || (hit->fraction==best->fraction && hit->axis<best->axis))){best=hit;first=i;second=j;}
            // Equal time/axis keeps the first lexicographic stable-ID pair.
        }
        const auto change=staticSlopes?surfaceChange(bodies,remaining,trialActor):std::optional<SlopeSurfaceChange>{};
        // Ordinary entity contacts win an exact tie. All bodies share the event
        // fraction; this is a solver path split, never another pose writer.
        const bool following=change && (!best || change->fraction<best->fraction);
        const auto chosenFraction=following?change->fraction:(best?best->fraction:one);
        // Reject a simultaneous unsupported trial contact even when another stable pair
        // wins the response tie and consumes the final remaining displacement.
        if(best && dynamicContactTime && *dynamicContactTime<=chosenFraction)throw StepTrialRejected{};
        if(best && !following && stopPrescribed && prescribedPair(bodies[first],bodies[second]))
            throw prescribedBlock(bodies[first],bodies[second],best->axis);
        if(best && !following){const auto& a=bodies[first];const auto& b=bodies[second];
            if(contacts)contacts->push_back({a.id,b.id,best->axis});
        }
        TraceStep step{bodies,{}};const auto fraction=chosenFraction;
        for(std::size_t i=0;i<bodies.size();++i){bodies[i].position=bodies[i].position+remaining[i]*fraction;bodies[i].box=bounds(bodies[i]);
            remaining[i]=remaining[i]*(one-fraction);if(!remaining[i].isFinite())invalid();}
        remainingTime=remainingTime*(one-fraction);if(!remainingTime.isFinite())invalid();
        if(following){auto& actor=bodies[change->actor];auto& rest=remaining[change->actor];
            const auto& wall=bodies[change->wall];const auto wallRest=remaining[change->wall];
            if(actor.terrain){const auto ownX=terrainOwnX(actor);
                actor.data.velocity.x=ownX+wall.data.velocity.x;rest.x=actor.data.velocity.x*remainingTime;
                actor.slopeCarry=wall.data.velocity;actor.slopeSupport=wall.id;}
            if(change->uphill){rest.y=wall.data.mode==DetBodyMode2D::Static?D{}:wallRest.y;
                actor.data.velocity.y=wall.data.mode==DetBodyMode2D::Static?D{}:wall.data.velocity.y;}
            else {const auto normal=slope(bodies[change->wall].box,bodies[change->wall].data.shape).surfaceNormal();
                const auto gradient=-normal.x/normal.y;
                if(wall.data.mode==DetBodyMode2D::Static){rest.y=gradient*rest.x;actor.data.velocity.y=gradient*actor.data.velocity.x;}
                else {rest.y=wallRest.y+gradient*(rest.x-wallRest.x);actor.data.velocity.y=wall.data.velocity.y+gradient*(actor.data.velocity.x-wall.data.velocity.x);}
                if(!rest.isFinite() || !actor.data.velocity.isFinite())invalid();}
            step.after=bodies;trace.push_back(std::move(step));
            if(std::all_of(remaining.begin(),remaining.end(),[](const auto& v){return v.x.isZero() && v.y.isZero();})){finished=true;break;}
            continue;
        }
        if(!best){finished=true;step.after=bodies;trace.push_back(std::move(step));break;}
        auto& a=bodies[first];auto& b=bodies[second];const auto n=best->axis;
        auto reverse=*best;reverse.normal=-reverse.normal;
        const bool ak=a.data.mode==DetBodyMode2D::Kinematic,bk=b.data.mode==DetBodyMode2D::Kinematic;
        const auto oldA=a.data.velocity,oldB=b.data.velocity;
        if(n==2){
            Body& actor=ak?a:b;const Body& wall=ak?b:a;auto& rest=remaining[ak?first:second];
            const auto normal=ak?best->normal:reverse.normal;
            if(!staticSlopes || ak==bk || (wall.data.mode!=DetBodyMode2D::Static && wall.data.mode!=DetBodyMode2D::MovingObstacle) || wall.data.shape==DetCollisionShape2D::Aabb || !(normal.y>D{}))invalid();
            const auto rise=(normal.x<D{}?-normal.x:normal.x)/normal.y;if(!rise.isFinite())invalid();
            auto relativeVelocity=wall.data.mode==DetBodyMode2D::Static?actor.data.velocity:actor.data.velocity-wall.data.velocity;
            auto relativeRest=wall.data.mode==DetBodyMode2D::Static?rest:rest-remaining[ak?second:first];
            if(actor.slopeLimited && rise>actor.maxSlopeRisePerRun){
                // Suppress uphill intent, then project inward gravity. Outward
                // vertical jump motion remains free rather than being reattached.
                const auto uphill=[&](D x){return normal.x<D{}?x>D{}:x<D{};};
                if(uphill(relativeVelocity.x))relativeVelocity.x={};
                if(uphill(relativeRest.x))relativeRest.x={};
            }
            const bool lowTouch=wall.data.shape==DetCollisionShape2D::SlopeRisesRight?
                actor.box.max.x==wall.box.min.x:actor.box.min.x==wall.box.max.x;
            if(actor.terrain && actor.slopeGrounded && lowTouch && remainingTime.isZero()){
                // At a terminal low endpoint there is no positive footprint yet.
                // Retain the old floor's valid terminal tangent until real entry.
            }else if(actor.terrain && (actor.slopeGrounded || actor.slopeLanding) && actor.id!=trialActor && rise<=actor.maxSlopeRisePerRun){
                // A real upward contact replaces the local support tangent while
                // preserving the character's already-solved own horizontal motion.
                // A descending, nonjumping actual landing has the same contact
                // proof; a nearby airborne body alone never enters this branch.
                const auto ownX=terrainOwnX(actor);
                actor.data.velocity.x=ownX+wall.data.velocity.x;
                actor.data.velocity.y=wall.data.velocity.y+(-normal.x/normal.y)*ownX;
                rest.x=actor.data.velocity.x*remainingTime;
                rest.y=remaining[ak?second:first].y+((-normal.x/normal.y)*ownX)*remainingTime;
                actor.slopeSupport=wall.id;actor.slopeCarry=wall.data.velocity;actor.slopeGrounded=true;
            }else {actor.data.velocity=slopeTangent(relativeVelocity,normal);rest=slopeTangent(relativeRest,normal);
                if(wall.data.mode!=DetBodyMode2D::Static){actor.data.velocity=actor.data.velocity+wall.data.velocity;
                    rest=rest+remaining[ak?second:first];}}
            if(!actor.data.velocity.isFinite() || !rest.isFinite())invalid();
            snapSlope(actor,wall);
        }else if(ak && bk){
            const auto shared=axis(remaining[first],n)*half+axis(remaining[second],n)*half;
            const auto velocity=axis(a.data.velocity,n)*half+axis(b.data.velocity,n)*half;
            if(!shared.isFinite() || !velocity.isFinite())invalid();
            axis(remaining[first],n)=axis(remaining[second],n)=shared;axis(a.data.velocity,n)=axis(b.data.velocity,n)=velocity;
            snap(b,a,reverse);
        }else if(ak){axis(remaining[first],n)=axis(remaining[second],n);axis(a.data.velocity,n)=axis(b.data.velocity,n);snap(a,b,*best);}
        else if(bk){axis(remaining[second],n)=axis(remaining[first],n);axis(b.data.velocity,n)=axis(a.data.velocity,n);snap(b,a,reverse);}
        else invalid();
        if(n==1 && ak!=bk){auto& actor=ak?a:b;const auto& wall=ak?b:a;const auto normal=ak?best->normal:reverse.normal;
            if(actor.terrain && actor.slopeGrounded && actor.id!=trialActor && normal.y>D{}
                && (wall.data.mode==DetBodyMode2D::Static || wall.data.mode==DetBodyMode2D::MovingObstacle)){
                const auto ownX=terrainOwnX(actor);actor.data.velocity.x=ownX+wall.data.velocity.x;
                remaining[ak?first:second].x=actor.data.velocity.x*remainingTime;
                actor.slopeSupport=wall.id;actor.slopeCarry=wall.data.velocity;
            }}
        if(stopPrescribed || staticSlopes){
            if(ak && (n==2?a.data.velocity!=oldA:axis(a.data.velocity,n)!=axis(oldA,n)))markMotion(a,DetCollisionMotionResult2D::Blocked,b.id,n);
            if(bk && (n==2?b.data.velocity!=oldB:axis(b.data.velocity,n)!=axis(oldB,n)))markMotion(b,DetCollisionMotionResult2D::Blocked,a.id,n);
        }
        step.after=bodies;trace.push_back(std::move(step));
        if(std::all_of(remaining.begin(),remaining.end(),[](const auto& v){return v.x.isZero() && v.y.isZero();})){finished=true;break;}
    }
    if(!finished)throw ContactLimitExceeded{};
    validateSolids(bodies,stopPrescribed);
}
struct StepCandidate {std::size_t actor,target;D height;};
std::vector<StepCandidate> stepCandidates(const std::vector<Body>& original,
    const std::vector<SolidContact>& contacts,D maximumHeight) {
    std::vector<StepCandidate> result;
    for(std::size_t i=0;i<original.size();++i){const auto& actor=original[i];
        if(!actor.stepRequested || actor.data.mode!=DetBodyMode2D::Kinematic || actor.data.trigger
            || actor.data.ignoredOneWay.value || actor.data.velocity.x.isZero() || actor.data.velocity.y>D{})continue;
        for(const auto& contact:contacts){
            if(contact.axis!=0 || (contact.first!=actor.id && contact.second!=actor.id))continue;
            const auto targetId=contact.first==actor.id?contact.second:contact.first;
            const auto target=std::lower_bound(original.begin(),original.end(),targetId,[](const Body& b,auto id){return b.id<id;});
            if(target==original.end() || target->id!=targetId || target->data.mode!=DetBodyMode2D::Static || target->data.oneWay
                || target->data.shape!=DetCollisionShape2D::Aabb)continue;
            // This first ordinary static side response defines the target. A
            // taller front wall must not be skipped in favour of a later stair.
            const auto height=target->box.max.y-actor.box.min.y;
            if(height.isFinite() && height>D{} && height<=maximumHeight)
                result.push_back({i,static_cast<std::size_t>(target-original.begin()),height});
            break;
        }
    }
    // readBodies is stable-ID sorted: no candidate state or hidden scheduler.
    return result;
}
bool tryStep(std::vector<Body>& trial,std::vector<TraceStep>& trace,const std::vector<Body>& baseline,
    const StepCandidate& candidate,D dt,std::uint32_t iterations,bool stopPrescribed=false,bool staticSlopes=false) {
    try {
        const auto quarter=dt/D::fromInt(4),half=dt/D::fromInt(2);
        if(!(quarter>D{}) || !(half>D{}) || (quarter+half+quarter).bits()!=dt.bits())return false;
        const auto actorId=trial[candidate.actor].id;
        const auto startFoot=trial[candidate.actor].box.min.y;
        const auto targetTop=trial[candidate.target].box.max.y;
        const auto intendedX=trial[candidate.actor].data.velocity.x;
        const auto rise=candidate.height/quarter,across=intendedX*D::fromInt(2);
        if(!rise.isFinite() || !across.isFinite())return false;
        // All bodies share the same real time phases. Only this actor receives
        // the opt-in path velocity; no frozen-obstacle query or pose teleport.
        trial[candidate.actor].data.velocity={D{},rise};
        moveTogether(trial,quarter,iterations,trace,nullptr,actorId,stopPrescribed,staticSlopes);
        if(trial[candidate.actor].box.min.y.bits()!=targetTop.bits())return false;
        trial[candidate.actor].data.velocity={across,D{}};
        moveTogether(trial,half,iterations,trace,nullptr,actorId,stopPrescribed,staticSlopes);
        const auto solvedAcross=trial[candidate.actor].data.velocity.x;
        trial[candidate.actor].data.velocity={D{},-rise};
        moveTogether(trial,quarter,iterations,trace,nullptr,actorId,stopPrescribed,staticSlopes);
        auto& actor=trial[candidate.actor];const auto& target=trial[candidate.target];
        const auto height=actor.box.min.y-startFoot;
        const auto improvement=actor.position.x-baseline[candidate.actor].position.x;
        if(!height.isFinite() || !(height>D{}) || height>candidate.height || !improvement.isFinite()
            || !(intendedX>D{}?improvement>D{}:improvement<D{})
            || actor.box.min.y.bits()!=target.box.max.y.bits()
            || !(actor.box.min.x<target.box.max.x && target.box.min.x<actor.box.max.x))return false;
        // Static-only responses cannot transfer a temporary path velocity. Keep
        // the horizontal wall response rather than restoring uncut input intent.
        actor.data.velocity={solvedAcross/D::fromInt(2),D{}};
        if(!actor.data.velocity.isFinite())return false;
        actor.stepped=true;return true;
    }catch(const StepTrialRejected&){return false;}
    catch(const PrescribedMotionConflict&){return false;}
    catch(const ContactLimitExceeded&){return false;}
    catch(const std::invalid_argument&){return false;}
    // Allocation/unknown errors propagate; only pure candidate geometry and
    // contact-budget rejection falls back to the already-validated baseline.
}
std::vector<DetCollisionPair2D> transientPairs(const std::vector<TraceStep>& trace,
    const std::vector<DetCollisionPair2D>& previous,const std::vector<DetCollisionPair2D>& current) {
    std::set<DetCollisionPair2D> crossing;
    for(const auto& step:trace)for(std::size_t i=0;i<step.before.size();++i)for(std::size_t j=i+1;j<step.before.size();++j){
        const auto& a=step.before[i];const auto& b=step.before[j];const DetCollisionPair2D pair{a.id,b.id};
        if((!a.data.trigger && !b.data.trigger) || !masks(a.data.layer,a.data.mask,b.data.layer,b.data.mask)
            || std::binary_search(previous.begin(),previous.end(),pair) || std::binary_search(current.begin(),current.end(),pair))continue;
        const auto relative=(step.after[i].position-a.position)-(step.after[j].position-b.position);
        const auto hit=geometricHit(a,b,relative);
        if(hit && (hit->initialOverlap || hit->fraction<D::fromInt(1)))crossing.insert(pair);
    }
    return {crossing.begin(),crossing.end()};
}
template<class Read> void validateMotionOutput(const DetSessionCheckpoint& state,SimEntityId id,
    const DetCollisionBody2D& data,const DetStateLayout& body,std::uint32_t schema,Read read,bool staticSlopes=false,bool recoverable=false,bool movingSlopes=false) {
    const auto result=std::get<std::uint32_t>(read(DetBodyMotionResult));
    const auto other=std::get<DetEntityRef>(read(DetBodyMotionOther)).value;
    const auto n=std::get<std::uint32_t>(read(DetBodyMotionAxis));
    const auto stamp=std::get<std::uint64_t>(read(DetBodyMotionResultTick));
    if(!result){if(other || n || stamp)invalid();return;}
    if(result>2 || (result==2 && !recoverable) || !other || other==id || n>(staticSlopes?2u:1u) || (result==2 && n>1) || !state.nextTick || stamp!=state.nextTick
        || data.trigger || (data.mode!=DetBodyMode2D::Kinematic && data.mode!=DetBodyMode2D::MovingObstacle))invalid();
    if(data.mode==DetBodyMode2D::MovingObstacle && !recoverable)invalid();
    const auto found=state.actors.find(other);
    if(found==state.actors.end()){
        if(!std::binary_search(state.retiredIds.begin(),state.retiredIds.end(),other))invalid();
    }else {
        const auto& words=found->second.blocks.at(schema);
        const auto otherMode=static_cast<DetBodyMode2D>(std::get<std::uint32_t>(body.read(words,DetBodyMode)));
        if(std::get<bool>(body.read(words,DetBodyTrigger)) || otherMode==DetBodyMode2D::Disabled)invalid();
        if(n==2){const bool otherWedge=std::get<std::uint32_t>(body.read(words,DetBodyShape))!=static_cast<std::uint32_t>(DetCollisionShape2D::Aabb);
            if(!movingSlopes?(otherMode!=DetBodyMode2D::Static || !otherWedge):
                ((otherMode!=DetBodyMode2D::Static && otherMode!=DetBodyMode2D::MovingObstacle)
                    || (!otherWedge && data.shape==DetCollisionShape2D::Aabb)))invalid();}
        if(result==2){
            if(data.mode==DetBodyMode2D::MovingObstacle && otherMode!=DetBodyMode2D::Kinematic)invalid();
            if(data.mode==DetBodyMode2D::Kinematic && otherMode!=DetBodyMode2D::MovingObstacle)invalid();
        }else if(data.mode==DetBodyMode2D::MovingObstacle
            && otherMode!=DetBodyMode2D::Static && otherMode!=DetBodyMode2D::MovingObstacle)invalid();
    }
    // Every recovered prescribed mover commits canonical zero whole velocity.
    if(data.mode==DetBodyMode2D::MovingObstacle && (data.velocity.x.bits() || data.velocity.y.bits()))invalid();
}
void validateExtendedState(const DetSessionCheckpoint& state,const DetCollision2DConfig& c,
    const DetStateLayout& body,const DetStateLayout& history) {
    const auto& words=state.globals.at(c.historySchema);
    auto u=[&](std::uint32_t field){return std::get<std::uint32_t>(history.read(words,field));};
    if(u(1)!=profile(c) || u(2)!=c.maxBodies || u(3)!=c.maxTriggerPairs || u(4)!=c.eventType
        || u(6)!=math::kDetGeometry2DProfileVersion || !std::get<bool>(history.read(words,7)) || u(8)!=c.maxContactIterations)invalid();
    if(c.stepUp && std::get<D>(history.read(words,9)).bits()!=c.maxStepHeight.bits())invalid();
    if(motionState(c) && u(128)!=static_cast<std::uint32_t>(c.motionPolicy))invalid();
    if(c.staticSlopes && (!std::get<bool>(history.read(words,129)) || u(130)!=math::kDetSlopeGeometry2DProfileVersion))invalid();
    if(c.movingSlopes && !std::get<bool>(history.read(words,131)))invalid();
    if(c.continuousTerrain && !std::get<bool>(history.read(words,132)))invalid();
    const auto count=u(5);if(count>c.maxTriggerPairs)invalid();DetCollisionPair2D previous{};
    const auto known=[&](SimEntityId id){return state.actors.contains(id) || std::binary_search(state.retiredIds.begin(),state.retiredIds.end(),id);};
    for(std::uint32_t i=0;i<c.maxTriggerPairs;++i){const DetCollisionPair2D p{std::get<DetEntityRef>(history.read(words,10+2*i)).value,std::get<DetEntityRef>(history.read(words,11+2*i)).value};
        if(i<count){if(!p.first || p.first>=p.second || !(previous<p) || !known(p.first) || !known(p.second))invalid();previous=p;}else if(p.first || p.second)invalid();}
    std::vector<Body> active;
    for(const auto& [id,a]:state.actors){const auto& w=a.blocks.at(c.bodySchema);
        if(c.stepUp && (std::get<bool>(body.read(w,DetBodyStepRequested)) || std::get<bool>(body.read(w,DetBodyStepped))))invalid();
        DetCollisionBody2D data{static_cast<DetBodyMode2D>(std::get<std::uint32_t>(body.read(w,DetBodyMode))),
            std::get<V>(body.read(w,DetBodyHalf)),std::get<V>(body.read(w,DetBodyOffset)),std::get<V>(body.read(w,DetBodyVelocity)),
            std::get<std::uint32_t>(body.read(w,DetBodyLayer)),std::get<std::uint32_t>(body.read(w,DetBodyMask)),std::get<bool>(body.read(w,DetBodyTrigger))};
        if(c.oneWayPlatforms){
            data.oneWay=std::get<bool>(body.read(w,DetBodyOneWay));
            data.ignoredOneWay=std::get<DetEntityRef>(body.read(w,DetBodyIgnoredOneWay));
            if(data.ignoredOneWay.value){
                if(data.ignoredOneWay.value==id || !known(data.ignoredOneWay.value))invalid();
                const auto target=state.actors.find(data.ignoredOneWay.value);
                if(target!=state.actors.end() && !std::get<bool>(body.read(target->second.blocks.at(c.bodySchema),DetBodyOneWay)))invalid();
            }
        }
        if(c.staticSlopes)data.shape=static_cast<DetCollisionShape2D>(std::get<std::uint32_t>(body.read(w,DetBodyShape)));
        validateBody(data,true,c.oneWayPlatforms,c.staticSlopes,c.movingSlopes);
        if(motionState(c))validateMotionOutput(state,id,data,body,c.bodySchema,[&](std::uint32_t field){return body.read(w,field);},c.staticSlopes,stopping(c),c.movingSlopes);
        if(data.mode==DetBodyMode2D::Disabled)continue;
        Body b{id,data,V::fromBits({a.pose.position[0],a.pose.position[1]}),D::fromBits(a.pose.position[2]),{}};b.box=bounds(b);active.push_back(b);
    }
    if(active.size()>c.maxBodies)invalid();validateSolids(active,stopping(c));
    for(const auto& event:state.pendingEvents)if(event.source==c.systemId){DetTriggerEvent2D decoded;
        if(event.type!=c.eventType || !decodeDetTriggerEvent2D(event.payload,decoded) || !known(decoded.pair.first) || !known(decoded.pair.second))invalid();}
}
