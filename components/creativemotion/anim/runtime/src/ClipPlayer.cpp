#include "opas/anim/ClipPlayer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace opas::anim
{
    namespace
    {

        double usableSpeed(double speed)
        {
            return std::isfinite(speed) && speed > 0.0 ? speed : 0.0;
        }

        // Writes fired events into a span and counts the ones that do not fit.
        class EventWriter
        {
        public:
            EventWriter(std::span<FiredEvent> out, const std::vector<ClipEvent>& events)
                : out_(out)
                , events_(events)
            {
            }

            // Every event with a time in [from, to), or [from, to] when `inclusive`.
            void span(double from, double to, bool inclusive)
            {
                for (const ClipEvent& e : events_)
                {
                    if (e.timeS >= from && (e.timeS < to || (inclusive && e.timeS <= to)))
                    {
                        add(e);
                    }
                }
            }

            // `cycles` whole cycles, each firing every event once.
            void wholeCycles(double cycles)
            {
                // Write while there is room, then count the rest without looping, so
                // the cost does not grow with the number of cycles (header).
                while (cycles >= 1.0 && !full() && !events_.empty())
                {
                    for (const ClipEvent& e : events_)
                    {
                        add(e);
                    }
                    cycles -= 1.0;
                }
                if (cycles >= 1.0 && !events_.empty())
                {
                    // Saturate in double first: a size_t near its maximum is not
                    // exactly representable, and casting a too-large double is
                    // undefined.
                    const double more = cycles * static_cast<double>(events_.size());
                    const std::size_t room = std::numeric_limits<std::size_t>::max() - dropped_;
                    const double limit = 0.5 * static_cast<double>(room); // safely below room
                    dropped_ = more >= limit ? std::numeric_limits<std::size_t>::max()
                                             : dropped_ + static_cast<std::size_t>(more);
                }
            }

            [[nodiscard]] std::size_t written() const { return written_; }
            [[nodiscard]] std::size_t dropped() const { return dropped_; }

        private:
            [[nodiscard]] bool full() const { return written_ >= out_.size(); }

            void add(const ClipEvent& e)
            {
                if (full())
                {
                    if (dropped_ < std::numeric_limits<std::size_t>::max())
                    {
                        ++dropped_;
                    }
                    return;
                }
                out_[written_++] = FiredEvent{ &e };
            }

            std::span<FiredEvent> out_;
            const std::vector<ClipEvent>& events_;
            std::size_t written_ = 0;
            std::size_t dropped_ = 0;
        };

    } // namespace

    void ClipPlayer::play(std::shared_ptr<const ClipBinding> binding, const PlayOptions& options)
    {
        if (binding == nullptr)
        {
            stop();
            return;
        }
        binding_ = std::move(binding);
        mode_ = options.mode;
        speed_ = usableSpeed(options.speed);
        const double duration = binding_->clip().durationS;
        const double start = std::isfinite(options.startTimeS) ? options.startTimeS : 0.0;
        if (mode_ == TimeMode::Loop)
        {
            timeS_ = start - std::floor(start / duration) * duration;
            if (timeS_ >= duration)
            { // rounding at an exact multiple
                timeS_ = 0.0;
            }
        }
        else
        {
            timeS_ = std::clamp(start, 0.0, duration);
        }
    }

    void ClipPlayer::stop()
    {
        binding_.reset();
        timeS_ = 0.0;
    }

    void ClipPlayer::setSpeed(double speed)
    {
        speed_ = usableSpeed(speed);
    }

    bool ClipPlayer::atEnd() const
    {
        return binding_ != nullptr && mode_ == TimeMode::Clamp && timeS_ >= binding_->clip().durationS;
    }

    PlayerStep ClipPlayer::advance(double dtS, std::span<FiredEvent> events)
    {
        if (binding_ == nullptr)
        {
            return {};
        }
        const double delta = speed_ * (std::isfinite(dtS) && dtS > 0.0 ? dtS : 0.0);
        return advanceClipTime(*binding_, mode_, timeS_, delta, events);
    }

    PlayerStep advanceClipTime(
        const ClipBinding& binding, TimeMode mode, double& timeS, double deltaS, std::span<FiredEvent> events)
    {
        const Clip& clip = binding.clip();
        const double duration = clip.durationS;
        // A product too large for a double (speed and dt both huge) is treated
        // like a non-finite dt: no advance.
        const bool atEnd = mode == TimeMode::Clamp && timeS >= duration;
        if (!(deltaS > 0.0) || !std::isfinite(deltaS) || atEnd)
        {
            return {};
        }
        EventWriter writer(events, clip.events);
        PlayerStep step;
        const double a = timeS;
        if (mode == TimeMode::Clamp)
        {
            const bool reachesEnd = a + deltaS >= duration;
            const double b = reachesEnd ? duration : a + deltaS;
            step.rootMotion = binding.rootMotion(a, b, TimeMode::Clamp);
            writer.span(a, b, reachesEnd);
            timeS = b;
        }
        else
        {
            const double b = a + deltaS; // unwrapped
            step.rootMotion = binding.rootMotion(a, b, TimeMode::Loop);
            const double cycles = std::floor(b / duration);
            if (cycles < 1.0)
            {
                writer.span(a, b, false);
                timeS = b;
            }
            else
            {
                // An event at durationS fires at the wrap (Loop time durationS is 0 again).
                writer.span(a, duration, true);
                writer.wholeCycles(cycles - 1.0);
                double rest = b - cycles * duration;
                if (rest >= duration)
                {
                    // floor() rounded down just under a wrap: that wrap happened.
                    writer.span(0.0, duration, true);
                    rest -= duration;
                }
                rest = std::clamp(rest, 0.0, std::nextafter(duration, 0.0));
                writer.span(0.0, rest, false);
                timeS = rest;
            }
        }
        step.eventsFired = writer.written();
        step.eventsDropped = writer.dropped();
        return step;
    }

    void ClipPlayer::sample(std::span<math::Transform> localPose) const
    {
        if (binding_ != nullptr)
        {
            binding_->sample(timeS_, mode_, localPose);
        }
    }

} // namespace opas::anim
