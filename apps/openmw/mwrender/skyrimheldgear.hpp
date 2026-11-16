#ifndef OPENMW_MWRENDER_SKYRIMHELDGEAR_HPP
#define OPENMW_MWRENDER_SKYRIMHELDGEAR_HPP

// Task 0220 (Skyrim actors show their gear): the weapon and shield of a neutral actor. A game pack names the models
// (openeden::sdk::game::ActorAppearance::heldWeaponModel, heldWeaponType, heldShieldModel); this file reads them as
// ordinary static Skyrim models and puts them where the game does, on named nodes of the skeleton:
//   * the shield on `SHIELD` (the forearm), always: a shield stays on the arm at peace;
//   * the weapon on the sheath node of its animation type while the actor is at peace (`WeaponSword`, `WeaponDagger`,
//     `WeaponAxe`, `WeaponMace`, `WeaponBack` for the two-handed weapons) and on `WEAPON` (the right hand) while it
//     fights. Both copies are built at once (one shared model) and a switch node shows one of them, so a change of
//     state never builds or compiles anything.
// The node names are those of `skeleton.nif` (read from the installed file, task 0220 stage log): WEAPON, SHIELD,
// WeaponAxe, WeaponBack, WeaponBow, WeaponDagger, WeaponMace, WeaponSword, QUIVER.
//
// The state: an actor is drawn while a combat clip has started on it in the last kDrawnHoldSeconds (the combat AI's
// engaged fighter plays an attack, block, bash, bow or cast clip as it fights). There is no draw or sheathe animation
// (the archive's equip clips belong to the game's behaviour graph, which is not used): the weapon changes place on the
// frame the first combat clip starts and back kDrawnHoldSeconds after the last one.
//
// Threads: the controller runs in the animation update (like every controller); `note()` is called from the main
// thread, so the pending flag is atomic. Nothing here allocates per frame.

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

#include <vsg/nodes/Switch.h>

#include <components/animation/controller.hpp>
#include <components/mwanimation/objectanim.hpp>
#include <components/vfs/manager.hpp>

#include <openeden/game/actor_appearance.hpp>

#include "skyrimmodel.hpp"

namespace MWRender::SkyrimHeldGear
{
    /// The skeleton's nodes (case as the file writes them; looked up without regard to case).
    inline constexpr std::string_view kHandNode = "WEAPON";
    inline constexpr std::string_view kShieldNode = "SHIELD";

    /// Seconds after the last combat clip that the weapon stays in the hand. OURS: the game keeps it drawn until the
    /// actor's AI decides it is safe, which this pack does not model; the combat AI gives up a lost target after 15 s.
    inline constexpr float kDrawnHoldSeconds = 8.f;

    /// The sheath node of a weapon's animation type (Skyrim SE WEAP DNAM +0): 1 sword, 2 dagger, 3 axe, 4 mace, 5 and 6
    /// the two-handed weapons on the back. Empty for a type with no sheath (a bow's is WeaponBow but bows are not held:
    /// task 0220 limit) and for 0.
    [[nodiscard]] std::string_view sheathNodeFor(std::uint8_t weaponType);

    /// When the weapon is in the hand. Pure: the time comes in, nothing is read.
    class DrawClock
    {
    public:
        /// A combat clip started (any thread). The next update() draws the weapon.
        void note() noexcept { mPending.store(true, std::memory_order_release); }
        /// Task 0247: the first-person arms say whether the weapon is in the hand (any thread). While forced, the
        /// answer is that and a note changes nothing; `unforce` goes back to the clock (which has no hold left).
        void force(bool drawn) noexcept { mForced.store(drawn ? 1 : 0, std::memory_order_release); }
        void unforce() noexcept { mForced.store(-1, std::memory_order_release); }
        /// The state at `now` (seconds on the animation's clock, which never goes down within one actor; a value below
        /// the last one is a restarted clock and the hold carries over). True: drawn.
        [[nodiscard]] bool update(float now) noexcept;

    private:
        std::atomic<bool> mPending{ false };
        std::atomic<int> mForced{ -1 }; ///< -1 the clock decides, 0 sheathed, 1 drawn (task 0247)
        float mUntil = -1.0e30f;
        float mLast = -1.0e30f;
    };

    /// The controller: runs the clock and shows the sheathed or the drawn copy.
    class Controller : public Anim::Controller
    {
    public:
        Controller(vsg::ref_ptr<vsg::Switch> sheathed, vsg::ref_ptr<vsg::Switch> drawn);

        void note() noexcept { mClock.note(); }
        void force(bool drawn) noexcept { mClock.force(drawn); }
        void unforce() noexcept { mClock.unforce(); }
        /// The last state the controller set (for tests and the trace).
        [[nodiscard]] bool drawn() const noexcept { return mShownDrawn.load(std::memory_order_relaxed); }

        void run(vsg::Object& target, float time) const override;

    private:
        vsg::ref_ptr<vsg::Switch> mSheathed;
        vsg::ref_ptr<vsg::Switch> mDrawn;
        mutable DrawClock mClock;
        mutable std::atomic<bool> mShownDrawn{ false };
        mutable bool mApplied = false; ///< a state was set at least once
    };

    /// What attaching did; logged by the caller.
    struct Report
    {
        bool mWeaponAttached = false;
        bool mShieldAttached = false;
        std::string mSheathNode; ///< where the weapon waits
        std::string mFailure; ///< the first thing that went wrong, empty when nothing did
    };

    /// Adds the actor's weapon and shield (when the appearance names them) to the nodes of its skeleton and, for a
    /// weapon, a Controller as an auto-play controller (also kept alive in `anim.keepAlive`). Call after the animation
    /// object is made and before the node is compiled. Never throws; a model that cannot be read leaves that item out.
    void attach(MWAnim::ObjectAnimation& anim, const openeden::sdk::game::ActorAppearance& appearance,
        const VFS::Manager& vfs, const SkyrimModel::BuildContext& context, Report& report);

    /// A combat clip started on the actor: draw the weapon. Finds the controller in `keepAlive`; a no-op without one.
    void noteCombat(const std::vector<vsg::ref_ptr<vsg::Object>>& keepAlive);

    /// Task 0247: the first-person arms decide whether the weapon is in the hand (the engine's draw state, the draw
    /// and sheathe clips) instead of the clock: true forces the drawn copy, false the sheathed one. Finds the
    /// controller in `keepAlive`; a no-op without one.
    void setDrawn(const std::vector<vsg::ref_ptr<vsg::Object>>& keepAlive, bool drawn);
}

#endif
