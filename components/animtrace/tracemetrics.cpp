#include "tracemetrics.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <istream>
#include <string_view>

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
            }
            if (!reader.seek("moved=(") || !reader.number(sample.mMoved[0]) || !reader.number(sample.mMoved[1])
                || !reader.number(sample.mMoved[2]))
                return false;
            if (!reader.seek("loco=(speed ") || !reader.number(sample.mLocoSpeed))
                return false;
            if (!reader.seek("animspeed=") || !reader.number(sample.mAnimSpeed))
                return false;
            // The traced bones are all named "bip01 <something>" and may contain spaces; the optional style text
            // before them has a `w=(` of its own, which is why the names are found by their prefix.
            while (reader.seek("bip01 "))
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
    }

    double angleDegrees(const Quat& a, const Quat& b)
    {
        return angleOf(relative(a, b));
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
        return text;
    }

    std::string formatJson(const Report& report)
    {
        std::string json = "{\n";
        json += "  \"label\": " + jsonText(report.mLabel) + ",\n";
        json += "  \"frames\": " + std::to_string(report.mSampleCount) + ",\n";
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
        json += "  ]\n}\n";
        return json;
    }
}
