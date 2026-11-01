#include "opas/anim/GraphInstance.hpp"

#include "opas/anim/PoseBlend.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace opas::anim
{
    namespace
    {

        // Summed steps round (30 steps of 1/60 s give 0.49999999999999994), so a
        // fade within this of its end is over, as in AnimationPlayer.
        constexpr double kEndToleranceS = 1e-9;

        double usableDt(double dtS)
        {
            return std::isfinite(dtS) && dtS > 0.0 ? dtS : 0.0;
        }

        RootMotionDelta scaled(const RootMotionDelta& d, double w)
        {
            return { d.translationM * w, d.yawRad * w };
        }

        RootMotionDelta added(const RootMotionDelta& a, const RootMotionDelta& b)
        {
            return { a.translationM + b.translationM, a.yawRad + b.yawRad };
        }

        RootMotionDelta blendRootMotion(const RootMotionDelta& from, const RootMotionDelta& to, double w)
        {
            return { from.translationM + (to.translationM - from.translationM) * w,
                from.yawRad + (to.yawRad - from.yawRad) * w };
        }

        bool compare(double v, CompareOp op, double value)
        {
            switch (op)
            {
                case CompareOp::Less:
                    return v < value;
                case CompareOp::LessEqual:
                    return v <= value;
                case CompareOp::Greater:
                    return v > value;
                case CompareOp::GreaterEqual:
                    return v >= value;
                case CompareOp::Equal:
                    return v == value;
                case CompareOp::NotEqual:
                    return v != value;
            }
            return false;
        }

    } // namespace

    std::optional<GraphInstance> GraphInstance::create(const AnimGraph& graph, std::span<const NamedClip> clips,
        std::size_t boneCount, std::vector<std::string>* warnings, std::string* error)
    {
        const auto failWith = [error](std::string message) -> std::optional<GraphInstance> {
            if (error != nullptr)
            {
                *error = std::move(message);
            }
            return std::nullopt;
        };
        if (const std::string problem = validateAnimGraph(graph); !problem.empty())
        {
            return failWith("graph \"" + graph.id + "\": " + problem);
        }
        std::map<std::string, std::shared_ptr<const ClipBinding>> clipOfName;
        for (const NamedClip& c : clips)
        {
            if (c.binding == nullptr)
            {
                return failWith("clip \"" + c.name + "\" has no binding");
            }
            if (c.binding->targetBoneCount() != boneCount)
            {
                return failWith("clip \"" + c.name + "\" is bound to " + std::to_string(c.binding->targetBoneCount())
                    + " bones, not " + std::to_string(boneCount));
            }
            if (!clipOfName.emplace(c.name, c.binding).second)
            {
                return failWith("two clips are named \"" + c.name + "\"");
            }
        }
        const auto find = [&clipOfName](const std::string& name) {
            const auto it = clipOfName.find(name);
            return it == clipOfName.end() ? nullptr : it->second;
        };

        GraphInstance g;
        g.graph_ = graph;
        std::map<std::string, std::size_t> stateIndex;
        for (std::size_t s = 0; s < graph.states.size(); ++s)
        {
            stateIndex[graph.states[s].id] = s;
        }
        for (const GraphParameter& p : graph.parameters)
        {
            g.parameters_.push_back(p.defaultValue);
        }
        for (const GraphState& state : graph.states)
        {
            StateClips sc;
            if (state.blend)
            {
                const Blend1D& blend = *state.blend;
                for (const BlendSample& sample : blend.samples)
                {
                    auto binding = find(sample.clip);
                    if (binding == nullptr)
                    {
                        return failWith("state \"" + state.id + "\": no clip \"" + sample.clip + "\"");
                    }
                    sc.samples.push_back(std::move(binding));
                    sc.at.push_back(sample.at);
                }
                const std::optional<std::size_t> p = g.parameterIndex(blend.parameter);
                if (!p)
                { // validateAnimGraph checked it; kept so nothing reads an empty optional
                    return failWith("state \"" + state.id + "\": no parameter \"" + blend.parameter + "\"");
                }
                sc.parameter = static_cast<int>(*p);
            }
            else
            {
                auto binding = find(state.clip);
                if (binding == nullptr)
                {
                    return failWith("state \"" + state.id + "\": no clip \"" + state.clip + "\"");
                }
                sc.samples.push_back(std::move(binding));
            }
            g.states_.push_back(std::move(sc));
        }
        for (const GraphTransition& t : graph.transitions)
        {
            ResolvedTransition rt;
            rt.to = stateIndex.at(t.to);
            rt.any = t.from == "*";
            rt.from = rt.any ? 0 : stateIndex.at(t.from);
            for (const GraphCondition& c : t.conditions)
            {
                ResolvedCondition rc{ c.kind, -1, c.op, c.value, c.event };
                if (c.kind == ConditionKind::Parameter)
                {
                    const std::optional<std::size_t> p = g.parameterIndex(c.parameter);
                    if (!p)
                    { // validateAnimGraph checked it, as above
                        return failWith("transition \"" + t.id + "\": no parameter \"" + c.parameter + "\"");
                    }
                    rc.parameter = static_cast<int>(*p);
                }
                rt.conditions.push_back(std::move(rc));
            }
            g.transitions_.push_back(std::move(rt));
        }
        // Trial order per state (header): priority, then a named `from` before
        // "*", then graph order.
        g.transitionsOfState_.resize(graph.states.size());
        for (std::size_t s = 0; s < graph.states.size(); ++s)
        {
            std::vector<std::size_t>& order = g.transitionsOfState_[s];
            for (std::size_t t = 0; t < g.transitions_.size(); ++t)
            {
                const ResolvedTransition& rt = g.transitions_[t];
                if ((rt.any && rt.to != s) || (!rt.any && rt.from == s))
                {
                    order.push_back(t);
                }
            }
            std::stable_sort(order.begin(), order.end(), [&g, &graph](std::size_t a, std::size_t b) {
                if (graph.transitions[a].priority != graph.transitions[b].priority)
                {
                    return graph.transitions[a].priority > graph.transitions[b].priority;
                }
                return !g.transitions_[a].any && g.transitions_[b].any;
            });
        }
        // Warnings: event conditions no clip of their state can fire.
        if (warnings != nullptr)
        {
            for (std::size_t t = 0; t < graph.transitions.size(); ++t)
            {
                const ResolvedTransition& rt = g.transitions_[t];
                std::set<std::string> names;
                for (std::size_t s = 0; s < g.states_.size(); ++s)
                {
                    if (rt.any ? s != rt.to : s == rt.from)
                    {
                        for (const auto& binding : g.states_[s].samples)
                        {
                            for (const ClipEvent& e : binding->clip().events)
                            {
                                names.insert(e.name);
                            }
                        }
                    }
                }
                for (const ResolvedCondition& c : rt.conditions)
                {
                    if (c.kind == ConditionKind::Event && !names.contains(c.event))
                    {
                        warnings->push_back("transition \"" + graph.transitions[t].id
                            + "\": no clip of its state has the event \"" + c.event + "\"");
                    }
                }
            }
        }
        g.inertializer_ = Inertializer(boneCount);
        g.pose_.resize(boneCount);
        g.previousPose_.resize(boneCount);
        g.incoming_.resize(boneCount);
        g.fadeFrom_.resize(boneCount);
        g.scratchA_.resize(boneCount);
        g.scratchB_.resize(boneCount);
        g.current_ = StatePlay{ stateIndex.at(graph.initialState) };
        g.sampleState(g.current_, 0.0, g.pose_);
        g.previousPose_ = g.pose_;
        return g;
    }

    std::optional<std::size_t> GraphInstance::parameterIndex(std::string_view id) const
    {
        for (std::size_t i = 0; i < graph_.parameters.size(); ++i)
        {
            if (graph_.parameters[i].id == id)
            {
                return i;
            }
        }
        return std::nullopt;
    }

    bool GraphInstance::setParameter(std::size_t index, double value)
    {
        if (index >= parameters_.size() || !std::isfinite(value))
        {
            return false;
        }
        const bool isBool = graph_.parameters[index].type == ParameterType::Bool;
        parameters_[index] = isBool ? (value != 0.0 ? 1.0 : 0.0) : value;
        return true;
    }

    bool GraphInstance::setParameter(std::string_view id, double value)
    {
        const std::optional<std::size_t> index = parameterIndex(id);
        return index && setParameter(*index, value);
    }

    double GraphInstance::parameter(std::size_t index) const
    {
        return index < parameters_.size() ? parameters_[index] : 0.0;
    }

    void GraphInstance::setHelperSolver(HelperSolver solver)
    {
        helperSolver_ = std::move(solver);
        helperSolver_.apply(pose_, timeS_, exertion_);
    }

    const HelperSolver& GraphInstance::helperSolver() const noexcept
    {
        return helperSolver_;
    }

    void GraphInstance::setExertion(double exertion) noexcept
    {
        exertion_ = std::isfinite(exertion) ? std::max(0.0, exertion) : 0.0;
    }

    double GraphInstance::exertion() const noexcept
    {
        return exertion_;
    }

    const std::string& GraphInstance::currentStateId() const
    {
        return graph_.states[current_.state].id;
    }

    bool GraphInstance::inTransition() const
    {
        return fading_ || inertializer_.active();
    }

    GraphInstance::BlendWeights GraphInstance::weights(std::size_t state) const
    {
        const StateClips& sc = states_[state];
        if (sc.parameter < 0)
        {
            return {};
        }
        const double p = parameters_[static_cast<std::size_t>(sc.parameter)];
        const std::size_t last = sc.at.size() - 1;
        if (!(p > sc.at.front()))
        {
            return { 0, 0, 0.0 };
        }
        if (!(p < sc.at[last]))
        {
            return { last, last, 0.0 };
        }
        const auto next = std::upper_bound(sc.at.begin(), sc.at.end(), p);
        const auto k = static_cast<std::size_t>(std::distance(sc.at.begin(), next)) - 1;
        return { k, k + 1, (p - sc.at[k]) / (sc.at[k + 1] - sc.at[k]) };
    }

    double GraphInstance::blendDuration(std::size_t state, const BlendWeights& w) const
    {
        const StateClips& sc = states_[state];
        const double d0 = sc.samples[w.first]->clip().durationS;
        const double d1 = sc.samples[w.second]->clip().durationS;
        return d0 + (d1 - d0) * w.secondWeight;
    }

    void GraphInstance::sampleState(const StatePlay& play, double phaseOffset, std::span<math::Transform> out)
    {
        const GraphState& state = graph_.states[play.state];
        const StateClips& sc = states_[play.state];
        const TimeMode mode = state.loop ? TimeMode::Loop : TimeMode::Clamp;
        if (sc.parameter < 0)
        {
            // phaseOffset is in clip seconds for a clip state.
            sc.samples.front()->sample(play.timeS + phaseOffset, mode, out);
            return;
        }
        const BlendWeights w = weights(play.state);
        // At the current clock each active sample uses its own time; with an
        // offset (the step before a state's start), the phase.
        const auto timeOf = [&play, phaseOffset](std::size_t sample, const ClipBinding& clip) {
            const double d = clip.clip().durationS;
            return phaseOffset == 0.0 ? sampleTime(play, sample, d) : (play.phase + phaseOffset) * d;
        };
        const ClipBinding& first = *sc.samples[w.first];
        const ClipBinding& second = *sc.samples[w.second];
        if (w.secondWeight <= 0.0)
        {
            first.sample(timeOf(w.first, first), mode, out);
            return;
        }
        if (w.secondWeight >= 1.0)
        {
            second.sample(timeOf(w.second, second), mode, out);
            return;
        }
        first.sample(timeOf(w.first, first), mode, scratchA_);
        second.sample(timeOf(w.second, second), mode, scratchB_);
        blendPoses(scratchA_, scratchB_, w.secondWeight, out);
    }

    double GraphInstance::sampleTime(const StatePlay& play, std::size_t sample, double durationS)
    {
        for (std::size_t i = 0; i < play.slotSample.size(); ++i)
        {
            if (play.slotSample[i] == sample)
            {
                return play.slotTimeS[i];
            }
        }
        return play.phase * durationS;
    }

    GraphInstance::StateStep GraphInstance::stepState(StatePlay& play, double dt, std::span<FiredEvent> events)
    {
        const GraphState& state = graph_.states[play.state];
        const StateClips& sc = states_[play.state];
        const TimeMode mode = state.loop ? TimeMode::Loop : TimeMode::Clamp;
        StateStep out;
        if (sc.parameter < 0)
        {
            const ClipBinding& clip = *sc.samples.front();
            const double delta = state.speed * dt;
            out.span = EventSpan{ &clip, mode, play.timeS, delta };
            out.step = advanceClipTime(clip, mode, play.timeS, delta, events);
            play.ended = mode == TimeMode::Clamp && play.timeS >= clip.clip().durationS;
            return out;
        }
        // A blend: one phase drives every sample (header).
        const BlendWeights w = weights(play.state);
        const double phaseDelta = state.speed * dt / blendDuration(play.state, w);
        const std::size_t dominant = w.secondWeight > 0.5 ? w.second : w.first;
        const std::array<std::size_t, 2> active{ w.first, w.second };
        const std::array<double, 2> weightOf{ 1.0 - w.secondWeight, w.secondWeight };
        std::array<std::size_t, 2> nextSample{ kNoSample, kNoSample };
        std::array<double, 2> nextTime{ 0.0, 0.0 };
        double dominantTime = sampleTime(play, dominant, sc.samples[dominant]->clip().durationS);
        for (std::size_t i = 0; i < 2; ++i)
        {
            if (weightOf[i] <= 0.0 || (i == 1 && w.second == w.first))
            {
                continue;
            }
            const ClipBinding& clip = *sc.samples[active[i]];
            const double d = clip.clip().durationS;
            double t = sampleTime(play, active[i], d);
            const double delta = phaseDelta * d;
            const bool isDominant = active[i] == dominant;
            if (isDominant)
            {
                out.span = EventSpan{ &clip, mode, t, delta };
            }
            const PlayerStep s = advanceClipTime(clip, mode, t, delta, isDominant ? events : std::span<FiredEvent>{});
            out.step.rootMotion = added(out.step.rootMotion, scaled(s.rootMotion, weightOf[i]));
            if (isDominant)
            {
                out.step.eventsFired = s.eventsFired;
                out.step.eventsDropped = s.eventsDropped;
                dominantTime = t;
            }
            nextSample[i] = active[i];
            nextTime[i] = t;
        }
        play.slotSample = nextSample;
        play.slotTimeS = nextTime;
        // The phase follows the dominant sample's clock, so the end (and the
        // phase other samples start from) agrees with the events it fired.
        const double dominantD = sc.samples[dominant]->clip().durationS;
        if (mode == TimeMode::Loop)
        {
            play.phase = dominantTime / dominantD;
            if (!(play.phase < 1.0) || play.phase < 0.0)
            { // rounding at the wrap
                play.phase = 0.0;
            }
        }
        else
        {
            play.phase = std::min(1.0, dominantTime / dominantD);
            play.ended = dominantTime >= dominantD;
        }
        return out;
    }

    // Whether an event named `name` lies in the clip-time span a step covered,
    // by the same rule advanceClipTime fires events with (ClipPlayer.hpp).
    bool GraphInstance::firedIn(const EventSpan& span, std::string_view name)
    {
        if (span.clip == nullptr || !(span.deltaS > 0.0) || !std::isfinite(span.deltaS))
        {
            return false;
        }
        const Clip& clip = span.clip->clip();
        const double d = clip.durationS;
        const double a = span.fromS;
        for (const ClipEvent& e : clip.events)
        {
            if (e.name != name)
            {
                continue;
            }
            const double t = e.timeS;
            if (span.mode == TimeMode::Clamp)
            {
                if (a >= d)
                {
                    return false;
                }
                const bool reachesEnd = a + span.deltaS >= d;
                const double b = reachesEnd ? d : a + span.deltaS;
                if (t >= a && (t < b || (reachesEnd && t <= d)))
                {
                    return true;
                }
                continue;
            }
            const double b = a + span.deltaS;
            const double cycles = std::floor(b / d);
            if (cycles < 1.0)
            {
                if (t >= a && t < b)
                {
                    return true;
                }
                continue;
            }
            const double rest = b - cycles * d;
            if (cycles >= 2.0 || rest >= d || (t >= a && t <= d) || t < rest)
            {
                return true;
            }
        }
        return false;
    }

    bool GraphInstance::holds(const ResolvedTransition& t, const EventSpan& span) const
    {
        for (const ResolvedCondition& c : t.conditions)
        {
            switch (c.kind)
            {
                case ConditionKind::Parameter:
                    if (!compare(parameters_[static_cast<std::size_t>(c.parameter)], c.op, c.value))
                    {
                        return false;
                    }
                    break;
                case ConditionKind::Event:
                    if (!firedIn(span, c.event))
                    {
                        return false;
                    }
                    break;
                case ConditionKind::ClipEnd:
                    if (!current_.ended)
                    {
                        return false;
                    }
                    break;
            }
        }
        return true;
    }

    void GraphInstance::enter(std::size_t transitionIndex)
    {
        const GraphTransition& t = graph_.transitions[transitionIndex];
        const StatePlay next{ transitions_[transitionIndex].to };
        const double duration = std::isfinite(t.durationS) && t.durationS > 0.0 ? t.durationS : 0.0;
        if (duration == 0.0)
        {
            // A cut: pose() keeps this step's output, and the new state shows
            // from the next step (header, step 4). That step's velocity must come
            // from the new state, not from the jump (as AnimationPlayer::play).
            current_ = next;
            cutPending_ = true;
            inertializer_.clear();
            fading_ = false;
            outgoingFrozen_ = false;
            return;
        }
        cutPending_ = false;
        if (t.mode == TransitionMode::Inertialize)
        {
            current_ = next;
            sampleState(current_, 0.0, incoming_);
            // The new state one step before its start, so the offset's rate
            // excludes the new state's own motion (as AnimationPlayer does).
            const GraphState& state = graph_.states[current_.state];
            double back = state.speed * lastDtS_;
            if (states_[current_.state].parameter >= 0)
            {
                back /= blendDuration(current_.state, weights(current_.state));
            }
            sampleState(current_, -back, fadeFrom_);
            inertializer_.start(previousPose_, pose_, fadeFrom_, incoming_, lastDtS_, duration);
            fading_ = false;
            outgoingFrozen_ = false;
            return;
        }
        if (fading_ || inertializer_.active())
        {
            std::copy(pose_.begin(), pose_.end(), fadeFrom_.begin());
            outgoingFrozen_ = true;
        }
        else
        {
            outgoing_ = current_;
            outgoingFrozen_ = false;
        }
        inertializer_.clear();
        current_ = next;
        fadeElapsedS_ = 0.0;
        fadeDurationS_ = duration;
        fading_ = true;
    }

    GraphStep GraphInstance::advance(double dtS, std::span<FiredEvent> events)
    {
        const double dt = usableDt(dtS);
        if (dt == 0.0)
        {
            return {}; // a paused step changes nothing
        }
        if (cutPending_)
        {
            sampleState(current_, 0.0, previousPose_); // the new state at time 0
            cutPending_ = false;
        }
        else
        {
            std::copy(pose_.begin(), pose_.end(), previousPose_.begin());
        }
        GraphStep step;
        const StateStep current = stepState(current_, dt, events);
        step.rootMotion = current.step.rootMotion;
        step.eventsFired = current.step.eventsFired;
        step.eventsDropped = current.step.eventsDropped;
        sampleState(current_, 0.0, incoming_);
        if (fading_)
        {
            fadeElapsedS_ += dt;
            const double w = fadeElapsedS_ >= fadeDurationS_ - kEndToleranceS ? 1.0 : fadeElapsedS_ / fadeDurationS_;
            RootMotionDelta fromMotion;
            if (!outgoingFrozen_)
            {
                fromMotion = stepState(outgoing_, dt, {}).step.rootMotion; // its events are not fired
                sampleState(outgoing_, 0.0, fadeFrom_);
            }
            step.rootMotion = blendRootMotion(fromMotion, step.rootMotion, w);
            blendPoses(fadeFrom_, incoming_, w, pose_);
            if (w >= 1.0)
            {
                fading_ = false;
                outgoingFrozen_ = false;
            }
        }
        else
        {
            std::copy(incoming_.begin(), incoming_.end(), pose_.begin());
            inertializer_.advanceAndApply(dt, pose_);
        }
        lastDtS_ = dt;
        for (const std::size_t t : transitionsOfState_[current_.state])
        {
            if (holds(transitions_[t], current.span))
            {
                enter(t);
                step.transition = static_cast<int>(t);
                break;
            }
        }
        timeS_ += dt;
        helperSolver_.apply(pose_, timeS_, exertion_);
        return step;
    }

} // namespace opas::anim
