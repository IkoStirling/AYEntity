// Included inside the collision implementation's private namespace.
// design reference: AYEntity/design.md Stage26 static step-up / whole-world scratch trials.
struct TraceStep {std::vector<Body> before,after;};
struct SolidContact {SimEntityId first,second;std::uint32_t axis;};
struct StepTrialRejected {};
struct ContactLimitExceeded : std::runtime_error {
    ContactLimitExceeded():std::runtime_error("deterministic collision 2D: contact iteration limit / crushing"){}
};
bool solidPair(const Body& a,const Body& b) {
    return !a.data.trigger && !b.data.trigger && (a.data.mode==DetBodyMode2D::Kinematic || b.data.mode==DetBodyMode2D::Kinematic)
        && masks(a.data.layer,a.data.mask,b.data.layer,b.data.mask);
}
void validateSolids(const std::vector<Body>& bodies) {
    for(std::size_t i=0;i<bodies.size();++i)for(std::size_t j=i+1;j<bodies.size();++j)
        if(solidPair(bodies[i],bodies[j]) && !bodies[i].data.oneWay && !bodies[j].data.oneWay
            && bodies[i].box.interiorOverlap(bodies[j].box))invalid();
}
// Profile3 top-plane sweep. Work in the platform frame; the ordinary full-box
// sweep cannot represent a transparent side/bottom or an intentional interior passage.
std::optional<math::DetHit2D> solidHit(const Body& a,const Body& b,V relative) {
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
void moveTogether(std::vector<Body>& bodies,D dt,std::uint32_t iterations,std::vector<TraceStep>& trace,
    std::vector<SolidContact>* contacts=nullptr,SimEntityId trialActor=0) {
    validateSolids(bodies);std::vector<V> remaining;
    for(const auto& b:bodies){const auto d=b.data.mode==DetBodyMode2D::Static?V{}:b.data.velocity*dt;if(!d.isFinite())invalid();remaining.push_back(d);}
    bool finished=false;const auto one=D::fromInt(1),half=D::fromBits(0x3f000000u);
    for(std::uint32_t pass=0;pass<iterations;++pass){
        std::optional<math::DetHit2D> best;std::optional<D> dynamicContactTime;std::size_t first=0,second=0;
        for(std::size_t i=0;i<bodies.size();++i)for(std::size_t j=i+1;j<bodies.size();++j)if(solidPair(bodies[i],bodies[j])){
            const auto relative=remaining[i]-remaining[j];if(!relative.isFinite())invalid();
            const auto hit=solidHit(bodies[i],bodies[j],relative);
            if(hit && hit->initialOverlap)invalid();
            if(hit && trialActor && (bodies[i].id==trialActor || bodies[j].id==trialActor)){
                const auto& other=bodies[i].id==trialActor?bodies[j]:bodies[i];
                if((other.data.mode!=DetBodyMode2D::Static || other.data.oneWay)
                    && (!dynamicContactTime || hit->fraction<*dynamicContactTime))dynamicContactTime=hit->fraction;
            }
            if(hit && (!best || hit->fraction<best->fraction || (hit->fraction==best->fraction && hit->axis<best->axis))){best=hit;first=i;second=j;}
            // Equal time/axis keeps the first lexicographic stable-ID pair.
        }
        // Reject a simultaneous non-static contact even when another stable pair
        // wins the response tie and consumes the final remaining displacement.
        if(best && dynamicContactTime && *dynamicContactTime<=best->fraction)throw StepTrialRejected{};
        if(best){const auto& a=bodies[first];const auto& b=bodies[second];
            if(contacts)contacts->push_back({a.id,b.id,best->axis});
        }
        TraceStep step{bodies,{}};const auto fraction=best?best->fraction:one;
        for(std::size_t i=0;i<bodies.size();++i){bodies[i].position=bodies[i].position+remaining[i]*fraction;bodies[i].box=bounds(bodies[i]);
            remaining[i]=remaining[i]*(one-fraction);if(!remaining[i].isFinite())invalid();}
        if(!best){finished=true;step.after=bodies;trace.push_back(std::move(step));break;}
        auto& a=bodies[first];auto& b=bodies[second];const auto n=best->axis;
        auto reverse=*best;reverse.normal=-reverse.normal;
        const bool ak=a.data.mode==DetBodyMode2D::Kinematic,bk=b.data.mode==DetBodyMode2D::Kinematic;
        if(ak && bk){
            const auto shared=axis(remaining[first],n)*half+axis(remaining[second],n)*half;
            const auto velocity=axis(a.data.velocity,n)*half+axis(b.data.velocity,n)*half;
            if(!shared.isFinite() || !velocity.isFinite())invalid();
            axis(remaining[first],n)=axis(remaining[second],n)=shared;axis(a.data.velocity,n)=axis(b.data.velocity,n)=velocity;
            snap(b,a,reverse);
        }else if(ak){axis(remaining[first],n)=axis(remaining[second],n);axis(a.data.velocity,n)=axis(b.data.velocity,n);snap(a,b,*best);}
        else if(bk){axis(remaining[second],n)=axis(remaining[first],n);axis(b.data.velocity,n)=axis(a.data.velocity,n);snap(b,a,reverse);}
        else invalid();
        step.after=bodies;trace.push_back(std::move(step));
        if(std::all_of(remaining.begin(),remaining.end(),[](const auto& v){return v.x.isZero() && v.y.isZero();})){finished=true;break;}
    }
    if(!finished)throw ContactLimitExceeded{};
    validateSolids(bodies);
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
            if(target==original.end() || target->id!=targetId || target->data.mode!=DetBodyMode2D::Static || target->data.oneWay)continue;
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
    const StepCandidate& candidate,D dt,std::uint32_t iterations) {
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
        moveTogether(trial,quarter,iterations,trace,nullptr,actorId);
        if(trial[candidate.actor].box.min.y.bits()!=targetTop.bits())return false;
        trial[candidate.actor].data.velocity={across,D{}};
        moveTogether(trial,half,iterations,trace,nullptr,actorId);
        const auto solvedAcross=trial[candidate.actor].data.velocity.x;
        trial[candidate.actor].data.velocity={D{},-rise};
        moveTogether(trial,quarter,iterations,trace,nullptr,actorId);
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
        const auto hit=math::detSweepAabb2D(a.box,relative,b.box);
        if(hit && (hit->initialOverlap || hit->fraction<D::fromInt(1)))crossing.insert(pair);
    }
    return {crossing.begin(),crossing.end()};
}
void validateExtendedState(const DetSessionCheckpoint& state,const DetCollision2DConfig& c,
    const DetStateLayout& body,const DetStateLayout& history) {
    const auto& words=state.globals.at(c.historySchema);
    auto u=[&](std::uint32_t field){return std::get<std::uint32_t>(history.read(words,field));};
    if(u(1)!=profile(c) || u(2)!=c.maxBodies || u(3)!=c.maxTriggerPairs || u(4)!=c.eventType
        || u(6)!=math::kDetGeometry2DProfileVersion || !std::get<bool>(history.read(words,7)) || u(8)!=c.maxContactIterations)invalid();
    if(c.stepUp && std::get<D>(history.read(words,9)).bits()!=c.maxStepHeight.bits())invalid();
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
        validateBody(data,true,c.oneWayPlatforms);if(data.mode==DetBodyMode2D::Disabled)continue;
        Body b{id,data,V::fromBits({a.pose.position[0],a.pose.position[1]}),D::fromBits(a.pose.position[2]),{}};b.box=bounds(b);active.push_back(b);
    }
    if(active.size()>c.maxBodies)invalid();validateSolids(active);
    for(const auto& event:state.pendingEvents)if(event.source==c.systemId){DetTriggerEvent2D decoded;
        if(event.type!=c.eventType || !decodeDetTriggerEvent2D(event.payload,decoded) || !known(decoded.pair.first) || !known(decoded.pair.second))invalid();}
}
