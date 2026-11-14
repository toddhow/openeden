#ifndef OPENEDEN_MWANIMATION_SCENERYANIM_H
#define OPENEDEN_MWANIMATION_SCENERYANIM_H

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <components/animation/controller.hpp>
#include <components/animation/tags.hpp>

#include "play.hpp"

namespace MWAnim
{
    /*
     * Keyframes for placed scenery (task 0096, stage f, `[Game] animate placed scenery keyframes`).
     *
     * A banner or flag is a static: the game gives only activators and creatures a CharacterController, so the
     * keyframe file next to the model (Misc::ResourceHelpers::correctActorModelPath: meshes/f/xfoo.kf for
     * meshes/f/foo.nif) was never played. The rule here is pure so that tests can pin it.
     */
    namespace SceneryKeyframes
    {
        /// True when the model should be given its keyframes: the setting is on, a keyframe file exists for it, and
        /// the model has no animation of its own (an auto-play controller: texture scroll, particles, ...).
        constexpr bool applies(bool settingOn, bool hasKeyframeFile, bool hasAutoPlayController)
        {
            return settingOn && hasKeyframeFile && !hasAutoPlayController;
        }

        /// The group to loop: "idle" when the file has it, else none. A placed Activator plays exactly that group and
        /// nothing else (CharacterController::refreshIdleAnims with idleStateToAnimGroup, no weather rule; "idlestorm"
        /// is an actor-only group, updateIdleStormState), so a copy that plays something else would move where the
        /// near object is still.
        inline std::string chooseGroup(const Anim::Tags& tags)
        {
            const auto& groups = tags.getGroups();
            if (groups.find(std::string_view("idle")) != groups.end())
                return "idle";
            return {};
        }

        /// The starting point of the loop, 0 (inclusive) to 1 (exclusive), from the reference number alone: two
        /// banners of one model wave out of step, and the same banner always starts at the same point. A fixed
        /// integer mix (splitmix64), no random numbers.
        inline float phase(std::uint64_t id)
        {
            std::uint64_t z = id + 0x9E3779B97F4A7C15ull;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            z = z ^ (z >> 31);
            return static_cast<float>(static_cast<double>(z >> 11) / 9007199254740992.0);
        }

        /// Loops `group` on `play` from the phase of `id`; returns false when the group cannot be played.
        inline bool start(Play& play, const std::string& group, std::uint64_t id)
        {
            if (group.empty() || !play.hasAnimation(group))
                return false;
            play.play(group, Play::Priority(1), 1, false, 1.f, "start", "stop", phase(id), 0xFFFFFFFFu, true);
            return true;
        }

        /// Poses `play` as a placed Activator is on its first frame: the `idle` group from its start, then one step of
        /// zero time (CharacterController::refreshIdleAnims, then runAnimation(0)). False when there is no `idle`; an
        /// idle whose start and stop are one time is posed too, though it never plays.
        inline bool startAtRest(Play& play)
        {
            if (!play.hasAnimation("idle"))
                return false;
            play.play("idle", Play::Priority(1), 1, false, 1.f, "start", "stop", 0.f, 0xFFFFFFFFu, true);
            play.runAnimation(0.f);
            return play.getInfo("idle");
        }

        /// An auto-play controller that advances a Play by the time that passed, so a group of auto-play controllers
        /// (near objects, far chunks) can run the keyframes: run() receives the group's timer, not a step.
        class Driver : public Anim::Controller
        {
        public:
            /// `owner` keeps a Play that nothing else owns (the far field); null when the object owns it.
            Driver(Play* play, std::shared_ptr<Play> owner = {})
                : mPlay(play)
                , mOwner(std::move(owner))
            {
                hints.autoPlay = true;
            }

            void run(vsg::Object& /*target*/, float time) const override
            {
                const float dt = time - mLast;
                mLast = time;
                if (dt > 0.f)
                    mPlay->runAnimation(dt);
            }

        private:
            Play* mPlay;
            std::shared_ptr<Play> mOwner;
            mutable float mLast = 0.f;
        };
    }
}

#endif
