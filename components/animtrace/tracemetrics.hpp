#ifndef OPENEDEN_COMPONENTS_ANIMTRACE_TRACEMETRICS_H
#define OPENEDEN_COMPONENTS_ANIMTRACE_TRACEMETRICS_H

// OpenEden, CreativeMotion in the game, stage 0 (task 0067): the maths of the transition measurements.
//
// The game writes one `CMTRACE` log line per actor per frame when `[Game] creativemotion animation debug` is on
// (CharacterController::traceAnimation) and the scripted scenario writes `CMPHASE <name> t=<seconds>` lines. This
// library reads both from a log and measures, for every scripted event, how big the rotation steps of the traced
// bones are around it. It knows nothing about the game: it only reads text.
//
// Rotations are the bones' LOCAL rotation quaternions as the trace prints them (x y z w, four decimals). A frame's
// "step" is the angle of the rotation that takes the previous frame's quaternion to this one. A step below about
// 1.5 degrees is noise (research note 0022: the trace prints four decimals).

#include <array>
#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

namespace AnimTrace
{
    struct Quat
    {
        double mX = 0.0;
        double mY = 0.0;
        double mZ = 0.0;
        double mW = 1.0;
    };

    struct BoneSample
    {
        std::string mName;
        Quat mRotation;
    };

    /// One CMTRACE line of one actor.
    struct Sample
    {
        double mDt = 0.0; // seconds, as the line says
        std::array<std::string, 4> mGroups; // g0..g3, the animation group each bone group plays
        double mMoved[3] = { 0.0, 0.0, 0.0 }; // `moved=` (world units this frame)
        double mLocoSpeed = 0.0;
        double mAnimSpeed = 0.0;
        std::vector<BoneSample> mBones;
    };

    /// A scripted event: the CMPHASE line that was logged just before sample number `mFrame`.
    struct Event
    {
        std::string mName;
        double mTime = 0.0; // the scenario's own clock
        std::size_t mFrame = 0;
    };

    struct Trace
    {
        std::vector<Sample> mSamples;
        std::vector<Event> mEvents;
        std::size_t mSkippedLines = 0; // CMTRACE lines of the chosen actor that could not be read
    };

    /// Rotation angle, in degrees, between two unit quaternions (0 to 180; q and -q are the same rotation). Uses
    /// atan2 of the relative rotation's parts, which keeps its precision for tiny angles (acos of a float dot does
    /// not).
    double angleDegrees(const Quat& a, const Quat& b);

    /// The step series of one bone: element i is the angle between sample i-1 and sample i (element 0 is 0).
    std::vector<double> stepSeries(const std::vector<Quat>& rotations);

    /// The second difference of the rotation: element i is the angle between the rotation step of frame i and the
    /// one of frame i-1 (degrees per frame squared; elements 0 and 1 are 0). Zero for constant angular velocity.
    std::vector<double> jerkSeries(const std::vector<Quat>& rotations);

    /// Ground speed of each sample in units per second: the length of `moved` in x and y divided by dt.
    std::vector<double> groundSpeedSeries(const std::vector<Sample>& samples);

    struct Options
    {
        /// Only lines of this actor are read (the debug string of the reference id, compared without case and
        /// without quotation marks).
        std::string mRef = "player";
        /// The window after an event in which the "step8", jerk and speed measures look, in frames.
        std::size_t mWindowFrames = 8;
        /// A bone is still when its step stays below this many degrees (note 0022's noise floor).
        double mSettleStepDegrees = 1.5;
        /// For this many frames in a row.
        std::size_t mSettleFrames = 8;
    };

    /// What was measured around one event (spec 0067, stage 0, M1 to M5). All maxima are over the traced bones.
    struct EventMetrics
    {
        std::string mName;
        std::size_t mFrame = 0;
        double mTime = 0.0;
        double mStepAtEvent = 0.0; // M1: the largest step in the event's own frame
        std::string mStepAtEventBone;
        double mStepAfter = 0.0; // M2: the largest step in the window frames after it
        double mJerk = 0.0; // M3: the largest second difference from the event's frame through the window
        double mSpeedStep = 0.0; // M4: the largest frame to frame change of ground speed (units per second)
        bool mSettled = false; // M5: every bone under the settle step for mSettleFrames frames in a row
        std::size_t mSettleFrames = 0; // frames after the event's frame at which that run starts
        double mSettleSeconds = 0.0; // the same, as the sum of the frames' dt
        std::size_t mWindowEnd = 0; // first frame that belongs to the next event (or the sample count)
    };

    struct Report
    {
        std::string mLabel;
        std::size_t mSampleCount = 0;
        std::size_t mSkippedLines = 0;
        double mSeconds = 0.0; // the sum of all dt
        double mLongestFrame = 0.0; // seconds
        std::vector<std::string> mBoneNames;
        std::vector<EventMetrics> mEvents;
    };

    /// Reads the CMTRACE and CMPHASE lines of a log. Lines of other actors, other text and lines it cannot read
    /// (counted in mSkippedLines) are ignored; it never throws.
    Trace parseLog(std::istream& log, const Options& options);

    /// The measurements of every event of the trace. Deterministic: the same trace gives the same report.
    Report measure(const Trace& trace, const Options& options, const std::string& label);

    /// A fixed-width table for people.
    std::string formatText(const Report& report);

    /// Canonical JSON: fixed key order, numbers with three decimals, one event per line, ends with a newline.
    std::string formatJson(const Report& report);
}

#endif
