#ifndef OPENMW_MWRENDER_SKYRIMFIRSTPERSON_HPP
#define OPENMW_MWRENDER_SKYRIMFIRSTPERSON_HPP

// Task 0245 (the player's first-person view in a Skyrim session): the pure rules of the first-person arms. Nothing here
// reads a file or touches the renderer, so a test runs them as they are. The scene graph half (the controller that
// plays the clips, the object that is drawn) is skyrimfirstperson.cpp and playerbody.cpp.
//
//   * clipPath / armsClipName: where the first-person clips are (`_1stperson/animations/<name>.hkx` of the actor
//     folder; the clip names of the pack's third-person clips exist there too: VERIFIED in Skyrim - Animations.bsa).
//   * idleClipFor: which clip the arms loop: empty hands, the weapon of the hand or a spell hand.
//   * Timeline: the loop and the one-shot clips (a swing, a cast) as a clock; the clips decode on a worker, so a clip
//     the timeline asks for may not be ready: the timeline asks, the caller answers with the clip's length.
//   * rootTranslation: where the arms' root goes so that the rig's camera node is at the player's eye while the arms
//     turn with the view (yaw and pitch), the turn being about the camera node.
//
// Threads: main thread, or one animation thread: a Timeline belongs to one controller. No allocation per call except
// the clip names of Timeline::Frame (a std::string copy; short names stay in the small buffer).

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include <memory>

#include <vsg/maths/quat.h>
#include <vsg/maths/vec3.h>

#include <components/animation/controller.hpp>
#include <components/animation/transform.hpp>
#include <components/vfs/manager.hpp>

#include "skyrimidle.hpp"

namespace MWRender::SkyrimFirstPerson
{
    /// The first-person skeleton's Havok file (VERIFIED: `meshes/actors/character/_1stperson/characterassets/
    /// skeletonfirst.hkx` in Skyrim - Animations.bsa) and the folder of its clips.
    inline constexpr std::string_view kSkeletonHkx = "meshes/actors/character/_1stperson/characterassets/skeletonfirst.hkx";
    inline constexpr std::string_view kClipFolder = "meshes/actors/character/_1stperson/animations/";
    /// The rig's camera node, lower case as the engine keeps bone names (`Camera1st [Cam1]` in the skeleton model).
    inline constexpr std::string_view kCameraBone = "camera1st [cam1]";

    /// A one-shot clip that has not decoded after this long is given up (the arms keep their loop).
    inline constexpr float kLoadTimeoutSeconds = 3.f;
    /// The answer of a duration callback while a clip is still being decoded.
    inline constexpr float kNotReady = -1.f;
    /// The longest clip name taken (the archive's longest first-person name is 35 characters).
    inline constexpr std::size_t kMaxClipName = 64;

    /// `name` lower-cased when it is a plain clip name (letters, digits, `_`; 1 to kMaxClipName characters), else empty.
    [[nodiscard]] inline std::string armsClipName(std::string_view name)
    {
        if (name.empty() || name.size() > kMaxClipName)
            return {};
        std::string out(name);
        for (char& c : out)
        {
            const unsigned char u = static_cast<unsigned char>(c);
            if (u >= 'A' && u <= 'Z')
                c = static_cast<char>(u - 'A' + 'a');
            else if (!((u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || u == '_'))
                return {};
        }
        return out;
    }

    /// The VFS path of a first-person clip; empty for a name armsClipName refuses.
    [[nodiscard]] inline std::string clipPath(std::string_view name)
    {
        const std::string clip = armsClipName(name);
        if (clip.empty())
            return {};
        return std::string(kClipFolder) + clip + ".hkx";
    }

    /// What the hands do while no swing or cast plays.
    enum class Stance : std::uint8_t
    {
        Sheathed, ///< nothing drawn
        Weapon, ///< the weapon (or the fists when there is none) is up
        Spell, ///< a spell hand is up
    };

    /// The clip that loops for the stance. `weaponType` is ActorAppearance::heldWeaponType (1 sword, 2 dagger, 3 axe,
    /// 4 mace, 5 two-handed sword, 6 two-handed axe or hammer, 0 none).
    [[nodiscard]] inline std::string_view idleClipFor(std::uint8_t weaponType, Stance stance)
    {
        switch (stance)
        {
            case Stance::Sheathed:
                return "mt_idle";
            case Stance::Spell:
                return "mrh_idle";
            case Stance::Weapon:
                break;
        }
        if (weaponType == 5)
            return "2hm_idle";
        if (weaponType == 6)
            return "2hw_idle";
        if (weaponType >= 1 && weaponType <= 4)
            return "1hm_idle";
        return "h2h_idle";
    }

    /// The clips of one first-person actor on a clock. `now` is the controller's own clock (seconds, never going down).
    class Timeline
    {
    public:
        struct Frame
        {
            std::string clip; ///< the clip to show
            float time = 0.f; ///< seconds into it (the caller loops an idle)
            bool once = false; ///< a one-shot clip (a swing or cast), not the loop
        };

        /// The loop clip. A change restarts it at the next pick.
        void setIdle(std::string_view name)
        {
            if (name == mIdle)
                return;
            mIdle.assign(name);
            mIdleStart = kUnset;
        }
        /// Starts a one-shot clip that replaces the loop until it has played; one already under way is replaced.
        void playOnce(std::string_view name)
        {
            mOnce.assign(name);
            mState = OnceState::Pending;
            mOnceSince = kUnset;
        }

        /// What to show at `now`. `duration(name)` answers the length of a clip in seconds, kNotReady while it is
        /// decoding and 0 when it failed (a clip that failed or that is not ready for kLoadTimeoutSeconds is dropped).
        template <class DurationFn>
        Frame pick(float now, DurationFn&& duration)
        {
            if (mState != OnceState::None)
            {
                const float length = duration(mOnce);
                if (mState == OnceState::Pending)
                {
                    if (mOnceSince == kUnset)
                        mOnceSince = now;
                    if (length > 0.f)
                    {
                        mState = OnceState::Playing;
                        mOnceStart = now;
                    }
                    else if (length == 0.f || now - mOnceSince > kLoadTimeoutSeconds)
                        mState = OnceState::None; // failed, or never decoded
                }
                if (mState == OnceState::Playing)
                {
                    const float t = now - mOnceStart;
                    if (t < length)
                        return { mOnce, t, true };
                    mState = OnceState::None;
                    mIdleStart = now; // the loop comes back from its start
                }
            }
            if (mIdleStart == kUnset)
                mIdleStart = now;
            return { mIdle, now - mIdleStart, false };
        }

    private:
        enum class OnceState : std::uint8_t
        {
            None,
            Pending, ///< asked for; waiting for the clip to decode
            Playing,
        };
        static constexpr float kUnset = -1.0e30f;

        std::string mIdle;
        std::string mOnce;
        OnceState mState = OnceState::None;
        float mIdleStart = kUnset;
        float mOnceStart = 0.f;
        float mOnceSince = kUnset;
    };

    /// The translation of the arms' root. `feet` is where the player stands, `yaw` the camera's yaw, `orient` the
    /// camera's full orientation (the arms are turned by it), `camRest` the camera node's place in the rig at rest.
    /// The camera node ends where it would stand if the arms were turned by the yaw alone; pitch and roll turn the arms
    /// about it.
    [[nodiscard]] inline vsg::vec3 rootTranslation(
        const vsg::vec3& feet, float yaw, const vsg::quat& orient, const vsg::vec3& camRest)
    {
        const vsg::vec3 eyeBase = feet + vsg::quat(yaw, vsg::vec3(0.f, 0.f, 1.f)) * camRest;
        return eyeBase - orient * camRest;
    }
}

namespace MWRender::SkyrimFirstPerson
{
    /// The arms' clip player: an auto-play controller of the first-person object that writes the bones of the current
    /// clip (the loop or a swing or cast, by a Timeline). The clips are decoded on workers; until one is ready the
    /// bones stay as they are. setIdle and playOnce are called on the main thread, run() on an update thread; one
    /// mutex guards both.
    class ArmsController : public Anim::Controller
    {
    public:
        /// `bones` maps lower-case bone names to the arms' transforms; `vfs` must outlive the controller.
        ArmsController(const VFS::Manager& vfs, SkyrimIdle::Controller::BoneMap bones);
        ~ArmsController() override;

        /// The loop clip (a first-person clip name; a name armsClipName refuses is ignored).
        void setIdle(std::string_view name);
        /// A swing or a cast: plays once, then the loop comes back.
        void playOnce(std::string_view name);
        /// The clip written last (empty before the first), for the trace.
        [[nodiscard]] std::string current() const;

        void run(vsg::Object& target, float time) const override;

    private:
        class Impl;
        std::unique_ptr<Impl> mImpl;
    };
}

#endif
