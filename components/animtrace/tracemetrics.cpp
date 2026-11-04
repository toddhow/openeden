#include "tracemetrics.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <istream>
#include <map>
#include <string_view>
#include <tuple>

namespace AnimTrace
{
    namespace
    {
        constexpr double sRadiansToDegrees = 57.295779513082320876798154814105;
        constexpr std::size_t sNumGroups = 4;

        struct Reader
        {
            const std::string& mLine;
            std::size_t mPos = 0;

            /// Moves behind the next occurrence of `key` at or after the current position.
            bool seek(std::string_view key)
            {
                const std::size_t found = mLine.find(key, mPos);
                if (found == std::string::npos)
                    return false;
                mPos = found + key.size();
                return true;
            }

            bool number(double& out)
            {
                const char* begin = mLine.c_str() + mPos;
                char* end = nullptr;
                out = std::strtod(begin, &end);
                if (end == begin)
                    return false;
                mPos += static_cast<std::size_t>(end - begin);
                return true;
            }

            /// Text up to (not including) the first of `stops`.
            std::string word(std::string_view stops)
            {
                const std::size_t stop = mLine.find_first_of(stops, mPos);
                const std::size_t end = stop == std::string::npos ? mLine.size() : stop;
                std::string result = mLine.substr(mPos, end - mPos);
                mPos = end;
                return result;
            }
        };

        std::string normaliseRef(std::string_view text)
        {
            std::string result;
            for (const char c : text)
            {
                if (c == '"' || c == '\'')
                    continue;
                result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            return result;
        }

        bool readQuaternion(Reader& reader, Quat& out)
        {
            return reader.number(out.mX) && reader.number(out.mY) && reader.number(out.mZ) && reader.number(out.mW);
        }

        /// Reads the part of a CMTRACE line behind "CMTRACE ". Returns false when a field is missing.
        bool parseTrace(const std::string& line, std::size_t start, std::string& ref, Sample& sample)
        {
            Reader reader{ line, start };
            if (!reader.seek("ref="))
                return false;
            ref = normaliseRef(reader.word(" "));
            if (!reader.seek("dt=") || !reader.number(sample.mDt))
                return false;
            for (std::size_t group = 0; group < sNumGroups; ++group)
            {
                if (!reader.seek(" g" + std::to_string(group) + "="))
                    return false;
                sample.mGroups[group] = reader.word("( ");
                if (line.compare(reader.mPos, 6, "(prev=") == 0)
                {
                    // The pose driver's text: (prev=<group>,trans=<elapsed>/<duration>) or (prev=<group>,trans=none).
                    GroupState& state = sample.mGroupStates[group];
                    reader.mPos += 6;
                    state.mPrevious = reader.word(",");
                    if (!reader.seek(",trans="))
                        return false;
                    state.mHasDriver = true;
                    if (line.compare(reader.mPos, 4, "none") == 0)
                        reader.mPos += 4;
                    else
                    {
                        if (!reader.number(state.mElapsed) || !reader.seek("/") || !reader.number(state.mDuration))
                            return false;
                        state.mTransitionActive = true;
                    }
                }
            }
            if (!reader.seek("moved=(") || !reader.number(sample.mMoved[0]) || !reader.number(sample.mMoved[1])
                || !reader.number(sample.mMoved[2]))
                return false;
            if (!reader.seek("loco=(speed ") || !reader.number(sample.mLocoSpeed))
                return false;
            if (!reader.seek("animspeed=") || !reader.number(sample.mAnimSpeed))
                return false;
            // The intent, before the bones: intent=(wants 1 start 0 stop 0 stance 0 turn 0.000).
            const std::size_t firstBone = line.find(" bip01 ", reader.mPos);
            if (const std::size_t intent = line.find(" intent=(", reader.mPos);
                intent != std::string::npos && intent < firstBone)
            {
                Reader intentReader{ line, intent + 9 };
                double wants = 0.0;
                double starting = 0.0;
                double stopping = 0.0;
                double stance = 0.0;
                if (!intentReader.seek("wants ") || !intentReader.number(wants) || !intentReader.seek("start ")
                    || !intentReader.number(starting) || !intentReader.seek("stop ") || !intentReader.number(stopping)
                    || !intentReader.seek("stance ") || !intentReader.number(stance) || !intentReader.seek("turn ")
                    || !intentReader.number(sample.mIntent.mTurnRate))
                    return false;
                sample.mIntent.mWants = wants != 0.0;
                sample.mIntent.mStarting = starting != 0.0;
                sample.mIntent.mStopping = stopping != 0.0;
                sample.mIntent.mStance = stance != 0.0;
                sample.mHasIntent = true;
            }
            // The targets come after the bones and name the same bones: the bone list stops where they start.
            const std::size_t targets = line.find(" tgt=[", reader.mPos);
            // The traced bones are all named "bip01 <something>" and may contain spaces; the optional style text
            // before them has a `w=(` of its own, which is why the names are found by their prefix.
            while (reader.seek("bip01 ") && (targets == std::string::npos || reader.mPos < targets))
            {
                const std::size_t nameStart = reader.mPos - 6;
                const std::size_t equals = line.find("=(", nameStart);
                if (equals == std::string::npos)
                    return false;
                BoneSample bone;
                bone.mName = line.substr(nameStart, equals - nameStart);
                reader.mPos = equals + 2;
                if (!readQuaternion(reader, bone.mRotation))
                    return false;
                sample.mBones.push_back(std::move(bone));
            }
            if (targets != std::string::npos)
            {
                // tgt=[<bone>=(x y z w);<bone>=(x y z w)]
                std::size_t at = targets + 6;
                while (at < line.size() && line[at] != ']')
                {
                    const std::size_t equals = line.find("=(", at);
                    if (equals == std::string::npos)
                        return false;
                    BoneSample target;
                    target.mName = line.substr(at, equals - at);
                    Reader targetReader{ line, equals + 2 };
                    if (!readQuaternion(targetReader, target.mRotation))
                        return false;
                    sample.mTargets.push_back(std::move(target));
                    at = line.find_first_of(";]", targetReader.mPos);
                    if (at == std::string::npos)
                        return false;
                    if (line[at] == ';')
                        ++at;
                }
            }
            return true;
        }

        bool parsePhase(const std::string& line, std::size_t start, Event& event)
        {
            Reader reader{ line, start };
            event.mName = reader.word(" ");
            if (event.mName.empty() || !reader.seek("t="))
                return false;
            return reader.number(event.mTime);
        }

        Quat multiply(const Quat& a, const Quat& b)
        {
            return { a.mW * b.mX + a.mX * b.mW + a.mY * b.mZ - a.mZ * b.mY,
                a.mW * b.mY - a.mX * b.mZ + a.mY * b.mW + a.mZ * b.mX,
                a.mW * b.mZ + a.mX * b.mY - a.mY * b.mX + a.mZ * b.mW,
                a.mW * b.mW - a.mX * b.mX - a.mY * b.mY - a.mZ * b.mZ };
        }

        Quat conjugate(const Quat& q)
        {
            return { -q.mX, -q.mY, -q.mZ, q.mW };
        }

        Quat normalised(const Quat& q)
        {
            const double length = std::sqrt(q.mX * q.mX + q.mY * q.mY + q.mZ * q.mZ + q.mW * q.mW);
            if (length <= 0.0)
                return {};
            return { q.mX / length, q.mY / length, q.mZ / length, q.mW / length };
        }

        /// The rotation that takes `a` to `b`: b * conj(a).
        Quat relative(const Quat& a, const Quat& b)
        {
            return multiply(normalised(b), conjugate(normalised(a)));
        }

        double angleOf(const Quat& rotation)
        {
            const double vector
                = std::sqrt(rotation.mX * rotation.mX + rotation.mY * rotation.mY + rotation.mZ * rotation.mZ);
            return 2.0 * std::atan2(vector, std::fabs(rotation.mW)) * sRadiansToDegrees;
        }

        std::string jsonText(const std::string& text)
        {
            std::string result = "\"";
            for (const char c : text)
            {
                if (c == '"' || c == '\\')
                {
                    result.push_back('\\');
                    result.push_back(c);
                }
                else if (c == '\n')
                    result += "\\n";
                else if (c == '\t')
                    result += "\\t";
                else if (c == '\r')
                    result += "\\r";
                else if (static_cast<unsigned char>(c) < 0x20)
                {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(c));
                    result += buffer;
                }
                else
                {
                    result.push_back(c);
                }
            }
            result.push_back('"');
            return result;
        }

        std::string fixed(double value, int decimals = 3)
        {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
            std::string text = buffer;
            // -0.000 would make two equal reports differ by a sign.
            if (text.size() > 1 && text[0] == '-' && text.find_first_not_of("-0.") == std::string::npos)
                text.erase(0, 1);
            return text;
        }

        Range rangeOf(const std::vector<double>& values)
        {
            Range range;
            if (values.empty())
                return range;
            range.mMin = *std::min_element(values.begin(), values.end());
            range.mMax = *std::max_element(values.begin(), values.end());
            double sum = 0.0;
            for (const double value : values)
                sum += value;
            range.mMean = sum / static_cast<double>(values.size());
            return range;
        }

        std::string rangeText(const Range& range, int decimals = 2)
        {
            return fixed(range.mMin, decimals) + ".." + fixed(range.mMax, decimals);
        }

        std::string rangeJson(const Range& range)
        {
            return "{\"min\": " + fixed(range.mMin) + ", \"max\": " + fixed(range.mMax)
                + ", \"mean\": " + fixed(range.mMean) + "}";
        }

        std::string metaJson(const std::vector<std::pair<std::string, std::string>>& meta)
        {
            std::string json = "{";
            for (std::size_t m = 0; m < meta.size(); ++m)
                json += (m == 0 ? "" : ", ") + jsonText(meta[m].first) + ": " + jsonText(meta[m].second);
            return json + "}";
        }
    }

    double angleDegrees(const Quat& a, const Quat& b)
    {
        return angleOf(relative(a, b));
    }

    namespace
    {
        /// Rotation series of one bone, from the samples' bones or their targets; a sample without the bone repeats
        /// the previous rotation. found counts the samples that have it.
        std::vector<Quat> boneSeries(
            const std::vector<Sample>& samples, const std::string& name, bool targets, std::vector<char>* has = nullptr)
        {
            std::vector<Quat> rotations(samples.size());
            if (has != nullptr)
                has->assign(samples.size(), 0);
            Quat last;
            for (std::size_t i = 0; i < samples.size(); ++i)
            {
                const auto& list = targets ? samples[i].mTargets : samples[i].mBones;
                const auto found = std::find_if(
                    list.begin(), list.end(), [&](const BoneSample& bone) { return bone.mName == name; });
                if (found != list.end())
                {
                    last = found->mRotation;
                    if (has != nullptr)
                        (*has)[i] = 1;
                }
                rotations[i] = last;
            }
            return rotations;
        }
    }

    void measureChanges(const Trace& trace, const std::vector<std::string>& boneNames,
        const std::vector<double>& maxStep, const std::vector<std::vector<double>>& jerks, Report& report)
    {
        const std::vector<Sample>& samples = trace.mSamples;
        const std::size_t count = samples.size();
        if (count == 0)
            return;
        // Offset of each frame: the largest angle between a bone and its target (bones that have one in that frame).
        std::vector<double> offset(count, 0.0);
        std::vector<std::size_t> offsetBone(count, 0);
        std::vector<std::vector<double>> excess(boneNames.size());
        for (std::size_t b = 0; b < boneNames.size(); ++b)
        {
            std::vector<char> has;
            const std::vector<Quat> applied = boneSeries(samples, boneNames[b], false);
            const std::vector<Quat> target = boneSeries(samples, boneNames[b], true, &has);
            const std::vector<double> targetJerk = jerkSeries(target);
            excess[b].assign(count, 0.0);
            for (std::size_t i = 0; i < count; ++i)
            {
                if (has[i] == 0)
                    continue;
                const double angle = angleDegrees(applied[i], target[i]);
                if (angle > offset[i])
                {
                    offset[i] = angle;
                    offsetBone[i] = b;
                }
                // The blend's own jerk: what the bone's jerk has above its target's (both need three frames with
                // targets to be meaningful).
                const bool threeFrames = i >= 2 && has[i - 1] != 0 && has[i - 2] != 0;
                if (threeFrames)
                    excess[b][i] = std::max(0.0, jerks[b][i] - targetJerk[i]);
            }
        }

        std::map<std::tuple<std::size_t, std::string, std::string>, std::size_t> seen;
        for (std::size_t i = 1; i < count; ++i)
        {
            for (std::size_t g = 0; g < sNumGroups; ++g)
            {
                const std::string& from = samples[i - 1].mGroups[g];
                const std::string& to = samples[i].mGroups[g];
                if (from == to)
                    continue;
                GroupChangeMetrics change;
                change.mGroup = g;
                change.mFrame = i;
                change.mFrom = from;
                change.mTo = to;
                change.mOccurrence = seen[std::make_tuple(g, from, to)]++;
                const GroupState& state = samples[i].mGroupStates[g];
                if (state.mHasDriver && state.mTransitionActive)
                    change.mDurationS = state.mDuration;
                // Frames from G while that transition runs (and the group stays).
                std::size_t end = i;
                while (end < count && samples[end].mGroups[g] == to && samples[end].mGroupStates[g].mTransitionActive)
                    ++end;
                change.mDurationFrames = end - i;
                change.mStepAtChange = maxStep[i];
                change.mHasTargets = !samples[i].mTargets.empty();
                if (change.mHasTargets)
                {
                    change.mOffsetAtChange = offset[i];
                    const std::size_t last = std::max(end, i + 1) - 1; // the transition's last frame (G when a cut)
                    for (std::size_t k = i; k <= last && k < count; ++k)
                    {
                        if (offset[k] >= change.mOffsetPeak)
                        {
                            change.mOffsetPeak = offset[k];
                            change.mOffsetBone = boneNames.empty() ? std::string() : boneNames[offsetBone[k]];
                        }
                    }
                    const std::size_t after = std::min(last + 1, count - 1);
                    change.mOffsetLeft = offset[after];
                    // Decay: the first frame after G whose offset is under a tenth of the peak.
                    const double limit = 0.1 * change.mOffsetPeak;
                    if (change.mOffsetPeak < 0.01)
                        change.mDecayed = true;
                    for (std::size_t k = i + 1; !change.mDecayed && k < count; ++k)
                    {
                        change.mDecaySeconds += samples[k].mDt;
                        if (offset[k] < limit)
                        {
                            change.mDecayed = true;
                            change.mDecayFrames = k - i;
                        }
                    }
                    if (!change.mDecayed)
                        change.mDecaySeconds = 0.0;
                    const std::size_t jerkEnd = std::min(count - 1, std::max(end, i + 2));
                    for (std::size_t b = 0; b < excess.size(); ++b)
                        for (std::size_t k = i; k <= jerkEnd; ++k)
                            change.mExcessJerk = std::max(change.mExcessJerk, excess[b][k]);
                }
                report.mChanges.push_back(std::move(change));
            }
        }
    }

    std::vector<double> stepSeries(const std::vector<Quat>& rotations)
    {
        std::vector<double> steps(rotations.size(), 0.0);
        for (std::size_t i = 1; i < rotations.size(); ++i)
            steps[i] = angleDegrees(rotations[i - 1], rotations[i]);
        return steps;
    }

    std::vector<double> jerkSeries(const std::vector<Quat>& rotations)
    {
        std::vector<double> jerk(rotations.size(), 0.0);
        for (std::size_t i = 2; i < rotations.size(); ++i)
        {
            const Quat previousStep = relative(rotations[i - 2], rotations[i - 1]);
            const Quat step = relative(rotations[i - 1], rotations[i]);
            jerk[i] = angleOf(multiply(step, conjugate(previousStep)));
        }
        return jerk;
    }

    std::vector<double> groundSpeedSeries(const std::vector<Sample>& samples)
    {
        std::vector<double> speeds(samples.size(), 0.0);
        for (std::size_t i = 0; i < samples.size(); ++i)
        {
            const Sample& sample = samples[i];
            if (sample.mDt > 0.0)
                speeds[i] = std::hypot(sample.mMoved[0], sample.mMoved[1]) / sample.mDt;
            else if (i > 0)
                speeds[i] = speeds[i - 1];
        }
        return speeds;
    }

    Trace parseLog(std::istream& log, const Options& options)
    {
        Trace trace;
        const std::string wanted = normaliseRef(options.mRef);
        std::string line;
        while (std::getline(log, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (const std::size_t phase = line.find("CMPHASE "); phase != std::string::npos)
            {
                Event event;
                if (parsePhase(line, phase + 8, event))
                {
                    event.mFrame = trace.mSamples.size();
                    trace.mEvents.push_back(std::move(event));
                }
                continue;
            }
            const std::size_t at = line.find("CMTRACE ");
            if (at == std::string::npos)
                continue;
            std::string ref;
            Sample sample;
            if (!parseTrace(line, at + 8, ref, sample))
            {
                // Only count it when it is plausibly ours: a damaged line has no usable ref to compare.
                if (line.find("ref=" + options.mRef) != std::string::npos
                    || line.find("ref=\"" + options.mRef + "\"") != std::string::npos)
                    ++trace.mSkippedLines;
                continue;
            }
            if (ref != wanted)
                continue;
            trace.mSamples.push_back(std::move(sample));
        }
        return trace;
    }

    Report measure(const Trace& trace, const Options& options, const std::string& label)
    {
        Report report;
        report.mLabel = label;
        report.mSampleCount = trace.mSamples.size();
        report.mSkippedLines = trace.mSkippedLines;
        const std::size_t count = trace.mSamples.size();
        for (const Sample& sample : trace.mSamples)
        {
            report.mSeconds += sample.mDt;
            report.mLongestFrame = std::max(report.mLongestFrame, sample.mDt);
        }
        if (count == 0)
            return report;

        // One rotation series per bone, found by name; a sample that lacks the bone repeats the previous rotation.
        for (const BoneSample& bone : trace.mSamples.front().mBones)
            report.mBoneNames.push_back(bone.mName);
        const std::size_t boneCount = report.mBoneNames.size();
        std::vector<std::vector<double>> steps(boneCount);
        std::vector<std::vector<double>> jerks(boneCount);
        for (std::size_t b = 0; b < boneCount; ++b)
        {
            std::vector<Quat> rotations(count);
            Quat last;
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto& bones = trace.mSamples[i].mBones;
                const auto found = std::find_if(bones.begin(), bones.end(),
                    [&](const BoneSample& bone) { return bone.mName == report.mBoneNames[b]; });
                if (found != bones.end())
                    last = found->mRotation;
                rotations[i] = last;
            }
            steps[b] = stepSeries(rotations);
            jerks[b] = jerkSeries(rotations);
        }
        std::vector<double> maxStep(count, 0.0);
        std::vector<std::size_t> maxStepBone(count, 0);
        std::vector<double> maxJerk(count, 0.0);
        for (std::size_t i = 0; i < count; ++i)
        {
            for (std::size_t b = 0; b < boneCount; ++b)
            {
                if (steps[b][i] > maxStep[i])
                {
                    maxStep[i] = steps[b][i];
                    maxStepBone[i] = b;
                }
                maxJerk[i] = std::max(maxJerk[i], jerks[b][i]);
            }
        }
        const std::vector<double> speeds = groundSpeedSeries(trace.mSamples);
        measureChanges(trace, report.mBoneNames, maxStep, jerks, report);
        for (const Sample& sample : trace.mSamples)
        {
            if (!sample.mHasIntent)
                continue;
            ++report.mIntentFrames;
            report.mWantsFrames += sample.mIntent.mWants ? 1 : 0;
            report.mStartingFrames += sample.mIntent.mStarting ? 1 : 0;
            report.mStoppingFrames += sample.mIntent.mStopping ? 1 : 0;
            report.mStanceFrames += sample.mIntent.mStance ? 1 : 0;
        }

        for (std::size_t e = 0; e < trace.mEvents.size(); ++e)
        {
            const Event& event = trace.mEvents[e];
            EventMetrics metrics;
            metrics.mName = event.mName;
            metrics.mFrame = event.mFrame;
            metrics.mTime = event.mTime;
            const std::size_t frame = event.mFrame;
            std::size_t windowEnd = count;
            if (e + 1 < trace.mEvents.size())
                windowEnd = std::min(count, std::max(trace.mEvents[e + 1].mFrame, frame + 1));
            metrics.mWindowEnd = windowEnd;
            if (frame >= count)
            {
                report.mEvents.push_back(std::move(metrics));
                continue;
            }
            const std::size_t last = std::min(frame + options.mWindowFrames, count - 1);
            metrics.mStepAtEvent = maxStep[frame];
            if (metrics.mStepAtEvent > 0.0 && boneCount > 0)
                metrics.mStepAtEventBone = report.mBoneNames[maxStepBone[frame]];
            for (std::size_t i = frame + 1; i <= last; ++i)
                metrics.mStepAfter = std::max(metrics.mStepAfter, maxStep[i]);
            for (std::size_t i = frame; i <= last; ++i)
            {
                metrics.mJerk = std::max(metrics.mJerk, maxJerk[i]);
                if (i >= 1)
                    metrics.mSpeedStep = std::max(metrics.mSpeedStep, std::fabs(speeds[i] - speeds[i - 1]));
            }
            // M5: the first run of mSettleFrames frames after the event in which every bone stays under the limit,
            // inside this event's own window.
            if (options.mSettleFrames > 0)
            {
                for (std::size_t start = frame + 1; start + options.mSettleFrames <= windowEnd; ++start)
                {
                    bool still = true;
                    for (std::size_t i = start; i < start + options.mSettleFrames && still; ++i)
                        still = maxStep[i] < options.mSettleStepDegrees;
                    if (!still)
                        continue;
                    metrics.mSettled = true;
                    metrics.mSettleFrames = start - frame;
                    for (std::size_t i = frame + 1; i <= start; ++i)
                        metrics.mSettleSeconds += trace.mSamples[i].mDt;
                    break;
                }
            }
            report.mEvents.push_back(std::move(metrics));
        }
        return report;
    }

    std::string formatText(const Report& report)
    {
        std::string text;
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), "CreativeMotion transition check: %s\n",
            report.mLabel.empty() ? "(unnamed)" : report.mLabel.c_str());
        text += buffer;
        std::snprintf(buffer, sizeof(buffer), "frames %zu, %.3f s, longest frame %.1f ms, unreadable lines %zu\n",
            report.mSampleCount, report.mSeconds, report.mLongestFrame * 1000.0, report.mSkippedLines);
        text += buffer;
        text += "step@F, step8 and jerk are degrees (jerk per frame squared); speed step is units per second per frame;\n";
        text += "settle is the first run of 8 frames under 1.5 degrees (frames and seconds after the event)\n";
        std::snprintf(buffer, sizeof(buffer), "%-14s %6s %8s %8s %8s %9s %10s   %s\n", "event", "frame", "step@F",
            "step8", "jerk", "speedstep", "settle", "worst bone at F");
        text += buffer;
        for (const EventMetrics& event : report.mEvents)
        {
            char settle[48];
            if (event.mSettled)
                std::snprintf(settle, sizeof(settle), "%zuf %.2fs", event.mSettleFrames, event.mSettleSeconds);
            else
                std::snprintf(settle, sizeof(settle), "never");
            std::snprintf(buffer, sizeof(buffer), "%-14s %6zu %8.2f %8.2f %8.2f %9.1f %10s   %s\n", event.mName.c_str(),
                event.mFrame, event.mStepAtEvent, event.mStepAfter, event.mJerk, event.mSpeedStep, settle,
                event.mStepAtEventBone.c_str());
            text += buffer;
        }
        for (const auto& [key, value] : report.mMeta)
            text += "meta " + key + ": " + value + "\n";
        std::snprintf(buffer, sizeof(buffer), "intent frames %zu: wants %zu, start %zu, stop %zu, stance %zu\n",
            report.mIntentFrames, report.mWantsFrames, report.mStartingFrames, report.mStoppingFrames,
            report.mStanceFrames);
        text += buffer;
        text += "group changes: D is the transition chosen at the change; offset, peak and left are the largest angle\n"
                "between a bone and its target (degrees) at the change, during the transition and after it; decay is\n"
                "the time until the offset is under a tenth of its peak; excess is the jerk above the target's\n";
        for (const GroupChangeMetrics& c : report.mChanges)
        {
            char decay[48];
            if (!c.mHasTargets)
                std::snprintf(decay, sizeof(decay), "-");
            else if (c.mDecayed)
                std::snprintf(decay, sizeof(decay), "%.3fs", c.mDecaySeconds);
            else
                std::snprintf(decay, sizeof(decay), "never");
            std::snprintf(buffer, sizeof(buffer),
                "g%zu %6zu %-18s -> %-18s D %.3fs (%zuf) step %6.2f offset %6.2f peak %6.2f left %6.2f decay %8s "
                "excess %6.2f\n",
                c.mGroup, c.mFrame, c.mFrom.c_str(), c.mTo.c_str(), c.mDurationS, c.mDurationFrames, c.mStepAtChange,
                c.mOffsetAtChange, c.mOffsetPeak, c.mOffsetLeft, decay, c.mExcessJerk);
            text += buffer;
        }
        return text;
    }

    std::string formatJson(const Report& report)
    {
        std::string json = "{\n";
        json += "  \"label\": " + jsonText(report.mLabel) + ",\n";
        json += "  \"meta\": " + metaJson(report.mMeta) + ",\n";
        json += "  \"frames\": " + std::to_string(report.mSampleCount) + ",\n";
        json += "  \"intentFrames\": {\"total\": " + std::to_string(report.mIntentFrames) + ", \"wants\": "
            + std::to_string(report.mWantsFrames) + ", \"start\": " + std::to_string(report.mStartingFrames)
            + ", \"stop\": " + std::to_string(report.mStoppingFrames)
            + ", \"stance\": " + std::to_string(report.mStanceFrames) + "},\n";
        json += "  \"unreadableLines\": " + std::to_string(report.mSkippedLines) + ",\n";
        json += "  \"seconds\": " + fixed(report.mSeconds) + ",\n";
        json += "  \"longestFrameSeconds\": " + fixed(report.mLongestFrame, 4) + ",\n";
        json += "  \"bones\": [";
        for (std::size_t b = 0; b < report.mBoneNames.size(); ++b)
            json += (b == 0 ? "" : ", ") + jsonText(report.mBoneNames[b]);
        json += "],\n  \"events\": [\n";
        for (std::size_t e = 0; e < report.mEvents.size(); ++e)
        {
            const EventMetrics& event = report.mEvents[e];
            json += "    {\"name\": " + jsonText(event.mName) + ", \"frame\": " + std::to_string(event.mFrame)
                + ", \"time\": " + fixed(event.mTime) + ", \"stepAtEvent\": " + fixed(event.mStepAtEvent)
                + ", \"stepAfter\": " + fixed(event.mStepAfter) + ", \"jerk\": " + fixed(event.mJerk)
                + ", \"speedStep\": " + fixed(event.mSpeedStep)
                + ", \"settled\": " + (event.mSettled ? "true" : "false")
                + ", \"settleFrames\": " + std::to_string(event.mSettled ? event.mSettleFrames : 0)
                + ", \"settleSeconds\": " + fixed(event.mSettled ? event.mSettleSeconds : 0.0)
                + ", \"worstBoneAtEvent\": " + jsonText(event.mStepAtEventBone) + "}";
            json += e + 1 < report.mEvents.size() ? ",\n" : "\n";
        }
        json += "  ],\n  \"changes\": [\n";
        for (std::size_t i = 0; i < report.mChanges.size(); ++i)
        {
            const GroupChangeMetrics& c = report.mChanges[i];
            json += "    {\"group\": " + std::to_string(c.mGroup) + ", \"frame\": " + std::to_string(c.mFrame)
                + ", \"occurrence\": " + std::to_string(c.mOccurrence) + ", \"from\": " + jsonText(c.mFrom)
                + ", \"to\": " + jsonText(c.mTo) + ", \"durationSeconds\": " + fixed(c.mDurationS)
                + ", \"durationFrames\": " + std::to_string(c.mDurationFrames) + ", \"stepAtChange\": "
                + fixed(c.mStepAtChange) + ", \"hasTargets\": " + (c.mHasTargets ? "true" : "false")
                + ", \"offsetAtChange\": " + fixed(c.mOffsetAtChange) + ", \"offsetPeak\": " + fixed(c.mOffsetPeak)
                + ", \"offsetBone\": " + jsonText(c.mOffsetBone) + ", \"offsetLeft\": " + fixed(c.mOffsetLeft)
                + ", \"decayed\": " + (c.mDecayed ? "true" : "false")
                + ", \"decayFrames\": " + std::to_string(c.mDecayFrames)
                + ", \"decaySeconds\": " + fixed(c.mDecaySeconds) + ", \"excessJerk\": " + fixed(c.mExcessJerk) + "}";
            json += i + 1 < report.mChanges.size() ? ",\n" : "\n";
        }
        json += "  ]\n}\n";
        return json;
    }

    Summary summarise(const std::vector<Report>& reports, const std::string& label)
    {
        Summary summary;
        summary.mLabel = label;
        summary.mRuns = reports.size();
        if (!reports.empty())
            summary.mMeta = reports.front().mMeta;

        using ChangeKey = std::tuple<std::size_t, std::string, std::string, std::size_t>;
        struct ChangeValues
        {
            std::vector<double> mFrame, mDuration, mStep, mOffsetAtChange, mOffsetPeak, mOffsetLeft, mDecay, mExcess;
            std::size_t mRuns = 0;
            std::size_t mTargetRuns = 0;
        };
        std::vector<ChangeKey> changeOrder;
        std::map<ChangeKey, ChangeValues> changes;
        using EventKey = std::pair<std::string, std::size_t>;
        struct EventValues
        {
            std::vector<double> mStepAtEvent, mStepAfter, mJerk;
        };
        std::vector<EventKey> eventOrder;
        std::map<EventKey, EventValues> events;
        std::vector<double> wants, starting, stopping;

        for (const Report& report : reports)
        {
            for (const GroupChangeMetrics& change : report.mChanges)
            {
                const ChangeKey key{ change.mGroup, change.mFrom, change.mTo, change.mOccurrence };
                auto [it, inserted] = changes.try_emplace(key);
                if (inserted)
                    changeOrder.push_back(key);
                ChangeValues& values = it->second;
                ++values.mRuns;
                values.mFrame.push_back(static_cast<double>(change.mFrame));
                values.mDuration.push_back(change.mDurationS);
                values.mStep.push_back(change.mStepAtChange);
                if (change.mHasTargets)
                {
                    ++values.mTargetRuns;
                    values.mOffsetAtChange.push_back(change.mOffsetAtChange);
                    values.mOffsetPeak.push_back(change.mOffsetPeak);
                    values.mOffsetLeft.push_back(change.mOffsetLeft);
                    values.mDecay.push_back(change.mDecayed ? change.mDecaySeconds : -1.0);
                    values.mExcess.push_back(change.mExcessJerk);
                }
            }
            std::map<std::string, std::size_t> eventOrdinal;
            for (const EventMetrics& event : report.mEvents)
            {
                const EventKey key{ event.mName, eventOrdinal[event.mName]++ };
                auto [it, inserted] = events.try_emplace(key);
                if (inserted)
                    eventOrder.push_back(key);
                it->second.mStepAtEvent.push_back(event.mStepAtEvent);
                it->second.mStepAfter.push_back(event.mStepAfter);
                it->second.mJerk.push_back(event.mJerk);
            }
            wants.push_back(static_cast<double>(report.mWantsFrames));
            starting.push_back(static_cast<double>(report.mStartingFrames));
            stopping.push_back(static_cast<double>(report.mStoppingFrames));
        }
        for (const ChangeKey& key : changeOrder)
        {
            const ChangeValues& values = changes.at(key);
            ChangeSummary change;
            change.mGroup = std::get<0>(key);
            change.mFrom = std::get<1>(key);
            change.mTo = std::get<2>(key);
            change.mOccurrence = std::get<3>(key);
            change.mRuns = values.mRuns;
            change.mTargetRuns = values.mTargetRuns;
            change.mFrame = rangeOf(values.mFrame);
            change.mDurationS = rangeOf(values.mDuration);
            change.mStepAtChange = rangeOf(values.mStep);
            change.mOffsetAtChange = rangeOf(values.mOffsetAtChange);
            change.mOffsetPeak = rangeOf(values.mOffsetPeak);
            change.mOffsetLeft = rangeOf(values.mOffsetLeft);
            change.mDecaySeconds = rangeOf(values.mDecay);
            change.mExcessJerk = rangeOf(values.mExcess);
            summary.mChanges.push_back(std::move(change));
        }
        for (const EventKey& key : eventOrder)
        {
            const EventValues& values = events.at(key);
            EventSummary event;
            event.mName = key.first;
            event.mRuns = values.mJerk.size();
            event.mStepAtEvent = rangeOf(values.mStepAtEvent);
            event.mStepAfter = rangeOf(values.mStepAfter);
            event.mJerk = rangeOf(values.mJerk);
            summary.mEvents.push_back(std::move(event));
        }
        summary.mWantsFrames = rangeOf(wants);
        summary.mStartingFrames = rangeOf(starting);
        summary.mStoppingFrames = rangeOf(stopping);
        return summary;
    }

    std::string formatSummaryText(const Summary& summary)
    {
        std::string text
            = "CreativeMotion summary: " + (summary.mLabel.empty() ? std::string("(unnamed)") : summary.mLabel)
            + ", runs " + std::to_string(summary.mRuns) + "\n";
        for (const auto& [key, value] : summary.mMeta)
            text += "  " + key + ": " + value + "\n";
        text += "intent frames: wants " + rangeText(summary.mWantsFrames, 0) + ", start "
            + rangeText(summary.mStartingFrames, 0) + ", stop " + rangeText(summary.mStoppingFrames, 0) + "\n";
        text += "group changes (min..max over the runs that have them; offsets and excess jerk in degrees, from the\n"
                "targets; decay is the seconds until the offset is under a tenth of its peak, -1 when it never is):\n";
        char buffer[512];
        for (const ChangeSummary& c : summary.mChanges)
        {
            std::snprintf(buffer, sizeof(buffer),
                "g%zu %s -> %s #%zu runs %zu frame %s D %s step %s offset %s peak %s left %s decay %s excess %s\n",
                c.mGroup, c.mFrom.c_str(), c.mTo.c_str(), c.mOccurrence, c.mRuns, rangeText(c.mFrame, 0).c_str(),
                rangeText(c.mDurationS, 3).c_str(), rangeText(c.mStepAtChange).c_str(),
                c.mTargetRuns > 0 ? rangeText(c.mOffsetAtChange).c_str() : "-",
                c.mTargetRuns > 0 ? rangeText(c.mOffsetPeak).c_str() : "-",
                c.mTargetRuns > 0 ? rangeText(c.mOffsetLeft).c_str() : "-",
                c.mTargetRuns > 0 ? rangeText(c.mDecaySeconds).c_str() : "-",
                c.mTargetRuns > 0 ? rangeText(c.mExcessJerk).c_str() : "-");
            text += buffer;
        }
        text += "scripted events (min..max): step@F, step8, jerk\n";
        for (const EventSummary& e : summary.mEvents)
        {
            std::snprintf(buffer, sizeof(buffer), "%-14s runs %zu step@F %s step8 %s jerk %s\n", e.mName.c_str(),
                e.mRuns, rangeText(e.mStepAtEvent).c_str(), rangeText(e.mStepAfter).c_str(),
                rangeText(e.mJerk).c_str());
            text += buffer;
        }
        return text;
    }

    std::string formatSummaryJson(const Summary& summary)
    {
        std::string json = "{\n";
        json += "  \"label\": " + jsonText(summary.mLabel) + ",\n";
        json += "  \"runs\": " + std::to_string(summary.mRuns) + ",\n";
        json += "  \"meta\": " + metaJson(summary.mMeta) + ",\n";
        json += "  \"intentFrames\": {\"wants\": " + rangeJson(summary.mWantsFrames) + ", \"start\": "
            + rangeJson(summary.mStartingFrames) + ", \"stop\": " + rangeJson(summary.mStoppingFrames) + "},\n";
        json += "  \"changes\": [\n";
        for (std::size_t i = 0; i < summary.mChanges.size(); ++i)
        {
            const ChangeSummary& c = summary.mChanges[i];
            json += "    {\"group\": " + std::to_string(c.mGroup) + ", \"from\": " + jsonText(c.mFrom)
                + ", \"to\": " + jsonText(c.mTo) + ", \"occurrence\": " + std::to_string(c.mOccurrence)
                + ", \"runs\": " + std::to_string(c.mRuns) + ", \"targetRuns\": " + std::to_string(c.mTargetRuns)
                + ", \"frame\": " + rangeJson(c.mFrame) + ", \"durationSeconds\": " + rangeJson(c.mDurationS)
                + ", \"stepAtChange\": " + rangeJson(c.mStepAtChange) + ", \"offsetAtChange\": "
                + rangeJson(c.mOffsetAtChange) + ", \"offsetPeak\": " + rangeJson(c.mOffsetPeak)
                + ", \"offsetLeft\": " + rangeJson(c.mOffsetLeft) + ", \"decaySeconds\": " + rangeJson(c.mDecaySeconds)
                + ", \"excessJerk\": " + rangeJson(c.mExcessJerk) + "}";
            json += i + 1 < summary.mChanges.size() ? ",\n" : "\n";
        }
        json += "  ],\n  \"events\": [\n";
        for (std::size_t i = 0; i < summary.mEvents.size(); ++i)
        {
            const EventSummary& e = summary.mEvents[i];
            json += "    {\"name\": " + jsonText(e.mName) + ", \"runs\": " + std::to_string(e.mRuns)
                + ", \"stepAtEvent\": " + rangeJson(e.mStepAtEvent) + ", \"stepAfter\": " + rangeJson(e.mStepAfter)
                + ", \"jerk\": " + rangeJson(e.mJerk) + "}";
            json += i + 1 < summary.mEvents.size() ? ",\n" : "\n";
        }
        json += "  ]\n}\n";
        return json;
    }
}
