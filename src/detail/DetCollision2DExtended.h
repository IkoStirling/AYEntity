// Included inside the collision implementation's private namespace.
struct TraceStep {std::vector<Body> before,after;};
bool solidPair(const Body& a,const Body& b) {
    return !a.data.trigger && !b.data.trigger && (a.data.mode==DetBodyMode2D::Kinematic || b.data.mode==DetBodyMode2D::Kinematic)
        && masks(a.data.layer,a.data.mask,b.data.layer,b.data.mask);
}
void validateSolids(const std::vector<Body>& bodies) {
    for(std::size_t i=0;i<bodies.size();++i)for(std::size_t j=i+1;j<bodies.size();++j)
        if(solidPair(bodies[i],bodies[j]) && bodies[i].box.interiorOverlap(bodies[j].box))invalid();
}
void moveTogether(std::vector<Body>& bodies,D dt,std::uint32_t iterations,std::vector<TraceStep>& trace) {
    validateSolids(bodies);std::vector<V> remaining;
    for(const auto& b:bodies){const auto d=b.data.mode==DetBodyMode2D::Static?V{}:b.data.velocity*dt;if(!d.isFinite())invalid();remaining.push_back(d);}
    bool finished=false;const auto one=D::fromInt(1),half=D::fromBits(0x3f000000u);
    for(std::uint32_t pass=0;pass<iterations;++pass){
        std::optional<math::DetHit2D> best;std::size_t first=0,second=0;
        for(std::size_t i=0;i<bodies.size();++i)for(std::size_t j=i+1;j<bodies.size();++j)if(solidPair(bodies[i],bodies[j])){
            const auto relative=remaining[i]-remaining[j];if(!relative.isFinite())invalid();
            const auto hit=math::detSweepAabb2D(bodies[i].box,relative,bodies[j].box);
            if(hit && hit->initialOverlap)invalid();
            if(hit && (!best || hit->fraction<best->fraction || (hit->fraction==best->fraction && hit->axis<best->axis))){best=hit;first=i;second=j;}
            // Equal time/axis keeps the first lexicographic stable-ID pair.
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
    if(!finished)throw std::runtime_error("deterministic collision 2D: contact iteration limit / crushing");
    validateSolids(bodies);
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
    const auto count=u(5);if(count>c.maxTriggerPairs)invalid();DetCollisionPair2D previous{};
    const auto known=[&](SimEntityId id){return state.actors.contains(id) || std::binary_search(state.retiredIds.begin(),state.retiredIds.end(),id);};
    for(std::uint32_t i=0;i<c.maxTriggerPairs;++i){const DetCollisionPair2D p{std::get<DetEntityRef>(history.read(words,10+2*i)).value,std::get<DetEntityRef>(history.read(words,11+2*i)).value};
        if(i<count){if(!p.first || p.first>=p.second || !(previous<p) || !known(p.first) || !known(p.second))invalid();previous=p;}else if(p.first || p.second)invalid();}
    std::vector<Body> active;
    for(const auto& [id,a]:state.actors){const auto& w=a.blocks.at(c.bodySchema);
        DetCollisionBody2D data{static_cast<DetBodyMode2D>(std::get<std::uint32_t>(body.read(w,DetBodyMode))),
            std::get<V>(body.read(w,DetBodyHalf)),std::get<V>(body.read(w,DetBodyOffset)),std::get<V>(body.read(w,DetBodyVelocity)),
            std::get<std::uint32_t>(body.read(w,DetBodyLayer)),std::get<std::uint32_t>(body.read(w,DetBodyMask)),std::get<bool>(body.read(w,DetBodyTrigger))};
        validateBody(data,true);if(data.mode==DetBodyMode2D::Disabled)continue;
        Body b{id,data,V::fromBits({a.pose.position[0],a.pose.position[1]}),D::fromBits(a.pose.position[2]),{}};b.box=bounds(b);active.push_back(b);
    }
    if(active.size()>c.maxBodies)invalid();validateSolids(active);
    for(const auto& event:state.pendingEvents)if(event.source==c.systemId){DetTriggerEvent2D decoded;
        if(event.type!=c.eventType || !decodeDetTriggerEvent2D(event.payload,decoded) || !known(decoded.pair.first) || !known(decoded.pair.second))invalid();}
}
