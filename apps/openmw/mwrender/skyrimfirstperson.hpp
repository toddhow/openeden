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

#include <algorithm>
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

#include <components/havokpack/skeleton.hpp>

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
        if (weaponType == 7)
            return "bow_idleheld"; // task 0249: a bow held, the string not drawn
        if (weaponType == 5)
            return "2hm_idle";
        if (weaponType == 6)
            return "2hw_idle";
        if (weaponType >= 1 && weaponType <= 4)
            return "1hm_idle";
        return "h2h_idle";
    }

    /// Task 0247. The time the arms cross-fade between two clips (a cut would pop): OURS, not read from the game.
    inline constexpr float kBlendSeconds = 0.15f;

    /// Task 0249. When an event of a first-person clip falls, in animation seconds into the clip (the sidecar's stored
    /// time times the generator's playback speed): not known yet (the sidecar is still being read), the clip has no such
    /// event, or at `seconds`.
    struct EventTime
    {
        enum class State : std::uint8_t
        {
            Unknown,
            Missing,
            At,
        };
        State state = State::Unknown;
        float seconds = 0.f;
    };

    /// MWMechanics::DrawState as the engine numbers it: 0 nothing, 1 weapon, 2 spell.
    inline constexpr std::uint8_t kDrawNothing = 0;
    inline constexpr std::uint8_t kDrawWeapon = 1;
    inline constexpr std::uint8_t kDrawSpell = 2;

    /// The first-person clip that raises the hands for `stance`: a weapon's own equip clip (the fists when there is
    /// no weapon or the type is unknown), `mrh_equip` for a spell hand. Empty for Sheathed. The names are those of
    /// `_1stperson/animations` in Skyrim - Animations.bsa (VERIFIED in the archive listing: 1hm_equip, dag_equip,
    /// axe_equip, mac_equip, 2hc_equip, 2hw_equip, bow_equip, h2h_equip, mrh_equip). `weaponType` is the pack's
    /// animation type (heldWeaponType); 7 is a bow, which no pack gives the arms yet.
    [[nodiscard]] inline std::string_view equipClipFor(std::uint8_t weaponType, Stance stance)
    {
        if (stance == Stance::Sheathed)
            return {};
        if (stance == Stance::Spell)
            return "mrh_equip";
        switch (weaponType)
        {
            case 1:
                return "1hm_equip";
            case 2:
                return "dag_equip";
            case 3:
                return "axe_equip";
            case 4:
                return "mac_equip";
            case 5:
                return "2hc_equip";
            case 6:
                return "2hw_equip";
            case 7:
                return "bow_equip";
            default:
                return "h2h_equip";
        }
    }

    /// The first-person clip that lowers them again (the same names with `unequip`).
    [[nodiscard]] inline std::string_view unequipClipFor(std::uint8_t weaponType, Stance stance)
    {
        if (stance == Stance::Sheathed)
            return {};
        if (stance == Stance::Spell)
            return "mrh_unequip";
        switch (weaponType)
        {
            case 1:
                return "1hm_unequip";
            case 2:
                return "dag_unequip";
            case 3:
                return "axe_unequip";
            case 4:
                return "mac_unequip";
            case 5:
                return "2hc_unequip";
            case 6:
                return "2hw_unequip";
            case 7:
                return "bow_unequip";
            default:
                return "h2h_unequip";
        }
    }

    namespace Detail
    {
        /// A draw state above 2 is read as nothing.
        [[nodiscard]] constexpr std::uint8_t normaliseDraw(std::uint8_t state)
        {
            return state == kDrawWeapon || state == kDrawSpell ? state : kDrawNothing;
        }
    }

    /// The clip a change of the engine's draw state plays: raising the weapon hand or the spell hand when something
    /// is drawn (also when it changes from one to the other), lowering what was drawn when nothing is. Empty for no
    /// change.
    [[nodiscard]] inline std::string_view drawTransitionClip(std::uint8_t weaponType, std::uint8_t from, std::uint8_t to)
    {
        from = Detail::normaliseDraw(from);
        to = Detail::normaliseDraw(to);
        if (from == to)
            return {};
        if (to == kDrawWeapon)
            return equipClipFor(weaponType, Stance::Weapon);
        if (to == kDrawSpell)
            return equipClipFor(weaponType, Stance::Spell);
        return unequipClipFor(weaponType, from == kDrawSpell ? Stance::Spell : Stance::Weapon);
    }

    /// The stance of the hands. The engine's draw state decides (F draws and sheathes); the holds after a swing or a
    /// cast only cover a player who has not drawn (an attack works without drawing in this build, as before this
    /// task), a spell hold winning over a weapon hold as it did.
    [[nodiscard]] inline Stance stanceFor(std::uint8_t drawState, bool swingHold, bool castHold)
    {
        drawState = Detail::normaliseDraw(drawState);
        if (drawState == kDrawWeapon)
            return Stance::Weapon;
        if (drawState == kDrawSpell)
            return Stance::Spell;
        if (castHold)
            return Stance::Spell;
        return swingHold ? Stance::Weapon : Stance::Sheathed;
    }

    /// True while the weapon model is in the hand: the weapon stance, or the unequip clip that puts it away still
    /// playing.
    [[nodiscard]] inline bool weaponInHand(Stance stance, bool unequipPlaying)
    {
        return stance == Stance::Weapon || unequipPlaying;
    }

    /// The pose between two poses of one bone: the translation and scale are mixed linearly, the rotation by the
    /// normalised linear mix of the quaternions along the short way. `weight` is 0 for `a` and 1 for `b` (clamped).
    [[nodiscard]] inline HavokPack::Transform blendTransform(
        const HavokPack::Transform& a, const HavokPack::Transform& b, float weight)
    {
        const float w = std::isfinite(weight) ? std::clamp(weight, 0.f, 1.f) : 1.f;
        HavokPack::Transform out;
        for (int i = 0; i < 3; ++i)
        {
            out.mTranslation[i] = a.mTranslation[i] + (b.mTranslation[i] - a.mTranslation[i]) * w;
            out.mScale[i] = a.mScale[i] + (b.mScale[i] - a.mScale[i]) * w;
        }
        float dot = 0.f;
        for (int i = 0; i < 4; ++i)
            dot += a.mRotation[i] * b.mRotation[i];
        const float sign = dot < 0.f ? -1.f : 1.f;
        float length = 0.f;
        for (int i = 0; i < 4; ++i)
        {
            out.mRotation[i] = a.mRotation[i] * (1.f - w) + b.mRotation[i] * sign * w;
            length += out.mRotation[i] * out.mRotation[i];
        }
        length = std::sqrt(length);
        if (length > 1e-6f)
            for (float& c : out.mRotation)
                c /= length;
        else
            out.mRotation = b.mRotation;
        return out;
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
            // Task 0247: while the arms cross-fade, the clip they come from, at its own time, and the weight of `clip`
            // (0 at the switch, 1 when it is over; `from` is empty when there is no fade).
            std::string from;
            float fromTime = 0.f;
            float blend = 1.f;
        };

        /// True while a one-shot clip is playing (decoded and started, not over), as of the last pick.
        [[nodiscard]] bool playingOnce() const { return mState == OnceState::Playing; }

        /// The loop clip. A change restarts it at the next pick.
        /// Task 0249: `offsetSeconds` is how far into the loop it starts when it is (re)started (a hold that began
        /// before the clip was chosen); ignored while the loop does not change.
        void setIdle(std::string_view name, float offsetSeconds = 0.f)
        {
            if (name == mIdle)
                return;
            mIdle.assign(name);
            mIdleStart = kUnset;
            mIdleOffset = std::isfinite(offsetSeconds) && offsetSeconds > 0.f ? offsetSeconds : 0.f;
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
        /// Task 0247: when the clip shown changes (a one-shot starts or is over, the loop is replaced) the answer also
        /// names the clip it comes from, at the time it would have reached, for kBlendSeconds (Frame::from, fromTime,
        /// blend). A one-shot clip that is the source is held at its last pose, a loop keeps running.
        template <class DurationFn>
        Frame pick(float now, DurationFn&& duration)
        {
            Frame frame = pickClip(now, duration);
            const float start = now - frame.time;
            if (mShown.valid
                && (frame.clip != mShown.clip || frame.once != mShown.once
                    || (frame.once && std::fabs(start - mShown.start) > kSameStartEpsilon)))
            {
                mFrom = mShown;
                mFading = true;
                mFadeStart = now;
            }
            mShown = { frame.clip, start, frame.once, true };
            if (mFading)
            {
                const float w = (now - mFadeStart) / kBlendSeconds;
                if (!(w < 1.f) || !mFrom.valid)
                    mFading = false;
                else
                {
                    frame.from = mFrom.clip;
                    float t = now - mFrom.start;
                    if (mFrom.once)
                    {
                        const float length = duration(mFrom.clip);
                        if (length > 0.f)
                            t = std::min(t, length);
                    }
                    frame.fromTime = std::max(t, 0.f);
                    frame.blend = std::max(w, 0.f);
                }
            }
            return frame;
        }

    private:
        struct Segment
        {
            std::string clip;
            float start = 0.f; ///< the time at which the clip's own time was 0
            bool once = false;
            bool valid = false;
        };
        static constexpr float kSameStartEpsilon = 1.0e-4f;

        Segment mShown; ///< what the last pick showed
        Segment mFrom; ///< what the arms fade from
        bool mFading = false;
        float mFadeStart = 0.f;

        template <class DurationFn>
        Frame pickClip(float now, DurationFn&& duration)
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
            {
                mIdleStart = now - mIdleOffset;
                mIdleOffset = 0.f;
            }
            return { mIdle, now - mIdleStart, false };
        }

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
        float mIdleOffset = 0.f;
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
        void setIdle(std::string_view name, float offsetSeconds = 0.f);
        /// A swing or a cast: plays once, then the loop comes back.
        void playOnce(std::string_view name);
        /// Task 0247: starts decoding a clip now so that it is ready when it is asked for (the draw, sheathe, idle and
        /// swing clips of the weapon in the hand). A name armsClipName refuses is ignored.
        void prefetch(std::string_view name);
        /// True while a one-shot clip (a swing, a cast, a draw or sheathe) is playing, as of the last frame.
        [[nodiscard]] bool playingOnce() const;
        /// Task 0249: true, with the seconds into it, while `clip` is the one-shot clip the last frame showed.
        [[nodiscard]] bool onceProgress(std::string_view clip, float& elapsed) const;
        /// Task 0249: when the named event (the sidecar `animationdata/firstperson.txt`: weaponDraw, weaponSheathe, ...)
        /// of the clip falls. The sidecar is read on a worker after the clip has decoded: Unknown until then.
        [[nodiscard]] EventTime eventTime(std::string_view clip, std::string_view event) const;
        /// The clip written last (empty before the first), for the trace.
        [[nodiscard]] std::string current() const;

        void run(vsg::Object& target, float time) const override;

    private:
        class Impl;
        std::unique_ptr<Impl> mImpl;
    };
}

#endif
