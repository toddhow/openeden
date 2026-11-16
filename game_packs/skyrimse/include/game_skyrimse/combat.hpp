#ifndef OPENEDEN_GAME_SKYRIMSE_COMBAT_H
#define OPENEDEN_GAME_SKYRIMSE_COMBAT_H

// The Skyrim SE pack's side of the combat hooks (task 0168 stage S5-0, task 0170 stage S5-2, task 0171 stage S5-3;
// decision 0018, sdk/include/openeden/game/combat_rules.hpp).
//
// S5-2 gave the slot table its clips for the one-handed sword and a periodic test swing. S5-3 adds the first rules: on
// a HitFrame event the pack asks for a sweep along the weapon's reach, spends the attacker's stamina, takes the nearest
// valid target's health down by computeMeleeHit (combat_stats.hpp) against that target's armour, plays a hit reaction
// on an NPC, reports the damage and, at 0 health, the death (once). The pack owns every actor's health and stamina;
// for the player the health is the engine's, read and written through the contract (SubjectState, DamageReport).
// The weapon is the WEAP named by the test setting `combat test weapon` (empty: unarmed): inventory weapons wait for
// S5-9. Armour is the NPC's default outfit; inventory armour waits for S5-9.
//
// Test rules, all off by default: `combat test swing` (periodic swings), `combat test hostile` (attacks the player when
// in reach), `combat test player swing` (the player swings every N seconds).
//
// No engine, renderer, Jolt, Qt or OSG type. Main thread only; the vectors are reserved so that a frame allocates
// nothing (the trace callback, when set, builds strings: tracing is a developer aid).

#include <game_skyrimse/combat_sound.hpp>
#include <game_skyrimse/combat_ai.hpp>
#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/magic.hpp>
#include <game_skyrimse/progression.hpp>

#include <openeden/game/actor_rules.hpp>
#include <openeden/game/combat_rules.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace game_skyrimse
{
    /// Weapon animation types (WEAP DNAM +0).
    inline constexpr std::uint8_t kWeaponAnimHandToHand = 0;
    inline constexpr std::uint8_t kWeaponAnim1HSword = 1;
    inline constexpr std::uint8_t kWeaponAnimBow = 7;

    inline constexpr std::string_view kCombatTestSwingKey = "combat test swing";
    inline constexpr std::size_t kMaxCombatTestSwingName = 128;

    /// Task 0171 test settings, all off by default: the named actors attack the player when it is in reach, the player
    /// swings every N seconds, the editor id of the WEAP all fighters use (empty: unarmed), and the decision trace.
    inline constexpr std::string_view kCombatTestHostileKey = "combat test hostile";
    inline constexpr std::string_view kCombatTestPlayerSwingKey = "combat test player swing";
    inline constexpr std::string_view kCombatTestWeaponKey = "combat test weapon";
    /// Task 0192 (combat on many actors), all off by default: `combat test crowd` makes the N actors nearest to the
    /// player hostile to it, `combat test duels` makes the next 2M nearest fight each other in pairs; they are chosen
    /// once, when the combat clock reaches `combat test crowd delay` seconds (default kDefaultCrowdDelay).
    inline constexpr std::string_view kCombatTestCrowdKey = "combat test crowd";
    inline constexpr std::string_view kCombatTestDuelsKey = "combat test duels";
    inline constexpr std::string_view kCombatTestCrowdDelayKey = "combat test crowd delay";
    inline constexpr float kDefaultCrowdDelay = 4.f;
    inline constexpr std::string_view kCombatTraceKey = "combat trace";
    /// Task 0190 (stage S5-9): the combat AI (off by default). With it on the test settings above are overrides.
    inline constexpr std::string_view kCombatAiKey = "combat ai";
    /// Task 0172 (stage S5-4) test settings, all off by default: every Nth attack of a hostile actor is a power attack,
    /// every Mth a bash, and the actors with this editor id block.
    inline constexpr std::string_view kCombatTestPowerEveryKey = "combat test power every";
    inline constexpr std::string_view kCombatTestBashEveryKey = "combat test bash every";
    inline constexpr std::string_view kCombatTestBlockKey = "combat test block";
    /// Task 0178 (stage S5-6) test settings, all off by default: the editor id of the AMMO both sides shoot (empty: no
    /// shooting), the seconds the player holds the attack control to draw each shot (0: the real key only) and a
    /// BowSpeedBonus for every archer (the actor value is not read from any record yet). A bow is `combat test
    /// weapon` with an animation type of 7.
    inline constexpr std::string_view kCombatTestAmmoKey = "combat test ammo";
    inline constexpr std::string_view kCombatTestPlayerBowKey = "combat test player bow";
    inline constexpr std::string_view kCombatTestBowSpeedBonusKey = "combat test bow speed bonus";
    /// Task 0179 (stage S5-7): `combat perks` (default true) turns perk evaluation on; the two lists are comma
    /// separated PERK editor ids given to the stand-in player and to every NPC fighter (empty: none).
    inline constexpr std::string_view kCombatPerksKey = "combat perks";
    /// Task 0193 (stage S5-10): `combat sounds` (default true) plays the weapon, impact and bow sounds of a fight.
    inline constexpr std::string_view kCombatSoundsKey = "combat sounds";
    inline constexpr std::string_view kCombatTestPlayerPerksKey = "combat test player perks";
    inline constexpr std::string_view kCombatTestNpcPerksKey = "combat test npc perks";
    /// Task 0186 (stage S5-8) test settings, all off by default. `combat test player spell`: the SPEL (editor id) the
    /// player's attack control casts in place of a swing; `combat test player cast every` presses it automatically
    /// every N seconds (0: the real key only) and `combat test player cast hold` is how long an automatic press holds
    /// (a concentration spell lasts that long). `combat test cast` is a comma separated list of `actor:spell` (editor
    /// ids): that actor casts the spell every `combat test cast every` seconds (default 4, first cast after one period),
    /// a concentration spell for `combat test cast hold` seconds (default 3). `combat test spell conditions` applies
    /// the effects of a spell that carry conditions the pack cannot evaluate (the perk-gated Stagger and Demoralize
    /// effects of Firebolt), which are otherwise skipped.
    inline constexpr std::string_view kCombatTestPlayerSpellKey = "combat test player spell";
    inline constexpr std::string_view kCombatTestPlayerCastEveryKey = "combat test player cast every";
    inline constexpr std::string_view kCombatTestPlayerCastHoldKey = "combat test player cast hold";
    inline constexpr std::string_view kCombatTestCastKey = "combat test cast";
    inline constexpr std::string_view kCombatTestCastEveryKey = "combat test cast every";
    inline constexpr std::string_view kCombatTestCastHoldKey = "combat test cast hold";
    inline constexpr std::string_view kCombatTestSpellConditionsKey = "combat test spell conditions";
    /// Task 0191 (visible spells and beams), both off by default. `combat spell beams`: a held spell that is aimed or
    /// touch (Flames, Frostbite, Sparks) works as a ray each frame, and a touch spell hits what is within reach.
    /// `combat spell visuals`: a held beam streams drawn-only copies of its projectile model, and a ward shows the
    /// hit art of its effect on its caster while it is up.
    inline constexpr std::string_view kCombatSpellBeamsKey = "combat spell beams";
    inline constexpr std::string_view kCombatSpellVisualsKey = "combat spell visuals";
    /// How far a touch spell reaches, game units (ours: Skyrim's own reach is not in the data; about a sword's reach).
    inline constexpr float kTouchReach = 150.f;
    /// A held beam launches a drawn-only projectile this often, seconds (ours).
    inline constexpr float kBeamVisualInterval = 0.05f;
    /// The length of a beam whose projectile has no range, units (Flames, Frostbite and Sparks have 768 or more).
    inline constexpr float kDefaultBeamRange = 768.f;
    /// The height above the caster's feet a ward's body model is placed at (ours: the model's shield spans z -22 to 60
    /// round its origin, so 40 puts its middle at hip to waist height).
    inline constexpr float kWardBodyHeight = 40.f;
    inline constexpr float kDefaultCastPeriod = 4.f;
    inline constexpr float kDefaultCastHold = 3.f;
    /// A cast waiting for its clip's SpellFire event gives up after this many seconds (the clip never sent it).
    inline constexpr float kCastEventTimeoutSeconds = 3.f;
    /// How often a held concentration spell writes a trace line (seconds).
    inline constexpr float kConcentrationTraceSeconds = 1.f;
    /// The most concentrations held, casts waiting and spell shots remembered at once.
    inline constexpr std::size_t kMaxConcentrations = 32;
    inline constexpr std::size_t kMaxSpellShots = 64;
    inline constexpr std::size_t kMaxCasts = 32;
    inline constexpr std::size_t kMaxTestCasters = 16;
    inline constexpr std::size_t kMaxTestCastListBytes = 1024;

    /// Where an arrow leaves an archer: above its feet and ahead of its centre (game units; ours, tunable).
    inline constexpr float kBowReleaseHeight = 100.f;
    inline constexpr float kBowMuzzleForward = 30.f;
    /// The player's eye above its feet (the player has no head position in the contract; ours, tunable) and the point
    /// of a target an archer aims at, above its feet (chest height).
    inline constexpr float kPlayerEyeHeight = 118.f;
    inline constexpr float kBowAimHeight = 80.f;
    /// How far an NPC archer shoots when the bow record has no maximum range (the data: 2500 for bows).
    inline constexpr float kBowDefaultRange = 2500.f;
    /// The shortest hold of the attack control that shoots at all, and the speed of a barely drawn bow as a fraction
    /// of the full draw (ours, RECALLED-VERIFY: Skyrim scales speed and damage with the draw).
    inline constexpr float kMinPullSeconds = 0.2f;
    inline constexpr float kMinDrawSpeedFraction = 0.35f;
    /// PROJ gravity is a multiplier of this acceleration, units a second squared: 9.80665 m/s2 at the engine's
    /// 69.99125 units a metre (RECALLED-VERIFY: Skyrim's own reading of the field is not settled by the data).
    inline constexpr float kProjectileGravityUnits = 686.38f;
    /// Shots whose launch the pack remembers until the core tells how they ended.
    inline constexpr std::size_t kMaxShotsRemembered = 64;
    /// Seconds between the presses of `combat test player bow`.
    inline constexpr float kPlayerBowPeriod = 4.f;

    /// The columns of SlotTable::magic().
    inline constexpr std::uint8_t kMagicAimed = 0;
    inline constexpr std::uint8_t kMagicSelf = 1;

    /// Skill slot of Block in ActorStats::skills (actor value 9; the skills start at 6 with One-handed: RECALLED-VERIFY
    /// from the engine's actor value order, which the data agrees with for 6, 7 and 8).
    inline constexpr std::size_t kBlockSkillSlot = 3;
    /// What kind of attack a swing is (a bash and a power attack are attacks with their own clip slot).
    enum class AttackKind
    {
        Normal,
        Power,
        Bash,
    };

    /// The sweep's half angle: RACE ATKD +20 "strike angle" is 35 degrees in 1,582 of 2,065 attacks (note 0056 section
    /// 2); ATKD is not decoded per race yet, so every swing uses it (RECALLED-VERIFY, tunable).
    inline constexpr float kStrikeHalfAngle = 0.6108652f; // 35 degrees in radians
    /// The band the sweep covers: from a step below the feet to a head above them (game units).
    inline constexpr float kSweepHeightBelow = 16.f;
    inline constexpr float kSweepHeightAbove = 130.f;
    /// The radius of an actor's physics capsule (MWPhysics::NeutralActor::kRadius, mirrored: no engine include here).
    inline constexpr float kBodyRadius = 22.f;
    /// The player has no clip (no first-person arms), so its swing runs on a hidden clock whose HitFrame time and
    /// length come from the sidecar of the attack clip at run time (CombatRequestWriter::clipTiming, task 0171 stage
    /// S5-3b). There is no fallback number: until the timing is ready the player cannot swing, and a timing that has
    /// not arrived after this many seconds of trying is reported once.
    inline constexpr float kPlayerTimingTimeoutSeconds = 5.f;
    /// Seconds a fighter counts as "in combat" (the regeneration multipliers) after a hit given or taken.
    inline constexpr float kCombatLingerSeconds = 10.f;
    /// Seconds between two attacks of a hostile test actor.
    inline constexpr float kHostileAttackPeriod = 2.5f;
    inline constexpr std::size_t kMaxCombatFighters = 4096;
    /// Task 0192: the most swings (hit events, sweeps waiting for their result, results to resolve, bow events) the pack
    /// keeps in one frame. It mirrors NeutralCombatBridge::kMaxPlaying (64): the bridge plays no more clips than that,
    /// so no more hit events can come in one frame. It was 32, and a crowd of more than 32 fighters that reached their
    /// HitFrame together lost the rest of the blows without a word.
    inline constexpr std::size_t kMaxConcurrentAttacks = 64;
    /// A sweep asked for and not answered after this many updates is forgotten (the engine answers in the next one; a
    /// result it dropped, for an attacker that had gone for instance, must not hold a table slot for ever).
    inline constexpr std::size_t kSweepAnswerUpdates = 2;

    /// Task 0184: the most health one potion queues (a hostile number is data).
    inline constexpr float kMaxPlayerHeal = 100000.f;

    /// Seconds before the first test swing and between two (the 1HM_AttackRight clip lasts 1.3 s).
    inline constexpr float kTestSwingFirstDelay = 3.f;
    inline constexpr float kTestSwingPeriod = 4.f;

    /// What a fight needs to know about one combatant (an NPC or the player stand-in), from the records through
    /// CombatData; the pack's rules only combine them.
    struct FighterProfile
    {
        float mHealthMax = 0.f;
        float mStaminaMax = 0.f;
        float mHealthRegenPercent = 0.f; ///< percent of the maximum a second (RACE DATA +84)
        float mStaminaRegenPercent = 0.f; ///< (RACE DATA +92)
        float mMitigationPercent = 0.f; ///< of the worn armour, ArmourResult::mMitigationPercent
        float mUnarmedDamage = 0.f;
        float mUnarmedReach = 0.f;
        std::array<std::uint8_t, openeden::sdk::game::kActorSkillCount> mSkills{};
        float mMagickaMax = 0.f; ///< task 0186
        float mMagickaRegenPercent = 0.f; ///< percent of the maximum a second (RACE DATA +88)
        // Task 0179 (perks), everything empty by default: with no perks, no abilities and no armour pieces nothing
        // below is read and every number is the one the fields above give.
        std::vector<FormId> mPerks; ///< PERK forms (NPC_ PRKR plus the test settings), sorted and unique
        std::vector<FormId> mSpells; ///< SPLO of the NPC and its race (abilities among them)
        std::vector<FormId> mKeywords; ///< the NPC's own KWDA and its race's
        std::vector<FormId> mFactions;
        std::vector<ArmourPiece> mPieces; ///< the worn armour with keywords (mMitigationPercent is their result)
        FormId mBase = kNullFormId;
        FormId mRace = kNullFormId;
        bool mFemale = false;
        std::uint16_t mLevel = 1;
        /// Sums of the ability effects on DamageResist (added to the armour rating) and WeaponSpeedMult (the bow's
        /// draw speed is multiplied by it when `mHasWeaponSpeedAbility`); filled from mPerks and mSpells.
        float mAbilityDamageResist = 0.f;
        bool mHasWeaponSpeedAbility = false;
        float mAbilityWeaponSpeed = 1.f;
        /// Task 0193: the body's material and the bare hands' impact data set (RACE NAM4 and NAM5).
        FormId mBodyMaterial = kNullFormId;
        FormId mUnarmedImpactSet = kNullFormId;
        AiProfile mAi; ///< task 0190: AIDT, the combat style and the relation to the player
    };

    /// The bow draw speed of a fighter: bowDrawSpeed(weapon speed, test bonus), times the WeaponSpeedMult ability when
    /// it has one (Quick Shot: 1.3; RECALLED-VERIFY: the stored magnitude is the multiplier itself), clamped 0.1 to 10.
    [[nodiscard]] float fighterBowDrawSpeed(const FighterProfile& profile, float weaponSpeed, float testBonus);

    /// Where SkyrimCombat gets its numbers from. SkyrimWorldView implements it; a test supplies a fake. Main thread.
    class CombatData
    {
    public:
        virtual ~CombatData() = default;
        /// The numbers of the NPC with base record `base`. False for an unknown base (that actor never fights).
        virtual bool fighter(openeden::sdk::game::WorldId base, FighterProfile& out) = 0;
        /// The stand-in for the player (level 1, skills 15, no armour: the player has no records).
        virtual FighterProfile playerFighter() = 0;
        /// The WEAP with this editor id (case-insensitive), or null. The pointer stays valid as long as the data.
        virtual const WeaponRecord* weaponByEditorId(std::string_view editorId) = 0;
        virtual CombatSettings settings() = 0;
        /// Task 0178: the AMMO with this editor id (case-insensitive), or null; and the PROJ of an ammunition (null when
        /// it has none). Defaults: none.
        virtual const AmmoRecord* ammoByEditorId(std::string_view editorId)
        {
            (void)editorId;
            return nullptr;
        }
        virtual const ProjectileRecord* projectileOf(const AmmoRecord& ammo)
        {
            (void)ammo;
            return nullptr;
        }
        virtual ProjectileSettings projectileSettings() { return {}; }
        /// Task 0186: the SPEL with this editor id (case-insensitive), or null; a SPEL and an MGEF by form id (default:
        /// the perk library's); a PROJ by form id; the ward and magicka regeneration numbers.
        virtual const SpellRecord* spellByEditorId(std::string_view editorId)
        {
            (void)editorId;
            return nullptr;
        }
        virtual const SpellRecord* spellById(FormId id)
        {
            const PerkLibrary* library = perkLibrary();
            return library != nullptr ? library->spell(id) : nullptr;
        }
        virtual const MagicEffectRecord* effectById(FormId id)
        {
            const PerkLibrary* library = perkLibrary();
            return library != nullptr ? library->magicEffect(id) : nullptr;
        }
        virtual const ProjectileRecord* projectileById(FormId id)
        {
            (void)id;
            return nullptr;
        }
        /// Task 0191: the model path of an ARTO as stored (`Magic\\WardBodyFX.nif`), empty when unknown.
        virtual std::string_view artModelById(FormId id)
        {
            (void)id;
            return {};
        }
        virtual MagicSettings magicSettings() { return {}; }
        /// Task 0172: the block, bash and stagger numbers (default: the values Skyrim.esm stores).
        virtual BlockSettings blockSettings() { return {}; }
        /// True when every answer above can be given at once, without reading anything. SkyrimCombat asks this at the
        /// start of every update and does nothing for that update while it is false (no swing, no hit): the data's
        /// first use may need records that a worker is still reading (review fix of task 0171: nothing here may block
        /// the main thread). The first call may start that read. Default: always ready (a test's fake).
        virtual bool combatReady() { return true; }
        /// Task 0179: the perk records (null: no perk exists, every perk list is ignored), and a PERK by editor id
        /// (case-insensitive; 0 when unknown). Defaults: none.
        virtual const PerkLibrary* perkLibrary() { return nullptr; }
        virtual FormId perkByEditorId(std::string_view editorId)
        {
            (void)editorId;
            return kNullFormId;
        }
        /// Task 0193: the impact, material and sound records (null: no combat sounds), and whether a VFS path exists
        /// (default: every file does). Called when the data is ready.
        virtual const SoundLibrary* soundLibrary() { return nullptr; }
        virtual bool soundFileExists(const std::string& vfsPath)
        {
            (void)vfsPath;
            return true;
        }
    };

    /// The table slot (attack, block, ...) x weapon animation type (WEAP DNAM +0, 0 to 9) -> clip file name.
    /// Default-constructed it is empty (the S5-0 skeleton); `humanoid()` is the shipped table. The names it returns
    /// point into static storage.
    class SlotTable
    {
    public:
        struct Entry
        {
            openeden::sdk::game::ClipSlot mSlot;
            std::uint8_t mWeaponAnimType;
            std::string_view mClip; ///< file name under meshes/actors/character/animations, no extension
        };

        SlotTable() = default;

        /// The humanoid table: one-handed sword (animation type 1) from the clips of Skyrim - Animations.bsa. The names
        /// were read from the archive listing (1hm_* files in the humanoid folder, which is not per sex) and the events
        /// from `animationdata/defaultmale.txt` (task 0170). RACE ATKE is not read (it names the event that starts an
        /// attack, which the clips' own sidecars already carry).
        [[nodiscard]] static SlotTable humanoid();

        /// Task 0178: the bow's clips (animation type 7): `Bow_DrawLight` (events bowDrawStart 0.8 and bowDrawn
        /// 1.667 playback seconds) and `Bow_Release` (arrowRelease 0.033), read from `animationdata/defaultmale.txt`
        /// and the listing of Skyrim - Animations.bsa. A separate table so that humanoid() keeps its seven entries.
        [[nodiscard]] static SlotTable bow();

        /// Task 0186: the cast clips (slot Cast): the aimed release `MRh_Release` (event MRh_SpellFire_Event at 0.167
        /// playback seconds) under kMagicAimed and the self release `MRh_SelfRelease` (0.367) under kMagicSelf, read
        /// from `animationdata/defaultmale.txt` and the listing of Skyrim - Animations.bsa. The "weapon animation type"
        /// column is the delivery class here.
        [[nodiscard]] static SlotTable magic();

        /// Task 0190: the raised guard of the AI (slot BlockStart): the one-handed `1HM_BlockIdle`, in the listing of
        /// Skyrim - Animations.bsa (actors\character\animations\1hm_blockidle.hkx). A separate table so that humanoid()
        /// keeps its seven entries.
        [[nodiscard]] static SlotTable guard();

        /// Number of (slot, weapon type) entries.
        [[nodiscard]] std::size_t size() const { return mCount; }

        /// The clip for a slot and weapon animation type, or an empty view when there is none.
        [[nodiscard]] std::string_view clipFor(openeden::sdk::game::ClipSlot slot, std::uint8_t weaponAnimType) const
        {
            for (std::size_t i = 0; i < mCount; ++i)
                if (mEntries[i].mSlot == slot && mEntries[i].mWeaponAnimType == weaponAnimType)
                    return mEntries[i].mClip;
            return {};
        }

    private:
        const Entry* mEntries = nullptr;
        std::size_t mCount = 0;
    };

    /// CombatRules: the clip slots, the melee hit and damage, and the test rules.
    class SkyrimCombat final : public openeden::sdk::game::CombatRules
    {
    public:
        SkyrimCombat();

        void onClipEvent(const openeden::sdk::game::ClipEvent& event) override;
        void onSweepResult(openeden::sdk::game::CombatSubject subject, std::uint32_t token,
            const openeden::sdk::game::SweepHit* hits, std::size_t count) override;
        void combatUpdate(float seconds, openeden::sdk::game::CombatRequestWriter& writer) override;
        void onClipFinished(
            openeden::sdk::game::CombatSubject subject, std::uint32_t token, openeden::sdk::game::ClipEnd how) override;
        void onPlayerAttack(bool power) override;

        /// Names the editor id of the test swingers (case-insensitive); empty switches the rule off. A value longer
        /// than kMaxCombatTestSwingName is refused (false, the rule stays off).
        bool setTestSwing(std::string_view editorId);
        [[nodiscard]] const std::string& testSwing() const { return mTestSwing; }

        /// Task 0171: the numbers (null: no combat rule acts; the counters still count). Not owned.
        void setData(CombatData* data);
        /// Names the actors that attack the player when it is in reach; same rules as setTestSwing.
        bool setTestHostile(std::string_view editorId);
        /// Task 0192: the crowd test rules (see kCombatTestCrowdKey); 0 or negative counts are off, a delay that is not
        /// finite or is negative is the default.
        void setTestCrowd(int hostile, int duels, float delaySeconds);
        [[nodiscard]] std::size_t duelists() const { return mOpponents.size(); }
        /// Seconds between the player's automatic swings; 0, negative or not finite is off.
        void setTestPlayerSwing(float seconds);
        /// The WEAP both sides wield (case-insensitive editor id); empty is unarmed. False when too long.
        bool setTestWeapon(std::string_view editorId);
        /// Task 0172 (stage S5-4), all off by default. Every Nth attack of a `combat test hostile` actor is a power
        /// attack (0 or negative: never); every Mth is a bash (bash wins on a shared number); actors named by
        /// `combat test block` block while they are alive, not swinging and not staggered. Counts are per actor from
        /// 1 and there is no random number, so the trace is repeatable.
        void setTestPowerEvery(int n);
        void setTestBashEvery(int n);
        bool setTestBlock(std::string_view editorId);
        /// Task 0178 (stage S5-6), all off by default. The AMMO the archers shoot (case-insensitive editor id; empty:
        /// nobody shoots; false when too long); a BowSpeedBonus for every archer (finite and not below -0.9; the draw
        /// clip runs at weapon speed x (1 + bonus)); and the seconds the player holds the attack control for each shot
        /// of
        /// its own test (0, negative or not finite: the real key only).
        bool setTestAmmo(std::string_view editorId);
        void setTestBowSpeedBonus(float bonus);
        /// Task 0179 (stage S5-7). `combat perks` (default true): when false no perk is looked at. The two lists are
        /// comma separated PERK editor ids given to the stand-in player and to every NPC fighter; empty (the default)
        /// gives none. A name the data does not know is traced once and skipped.
        void setUsePerks(bool use) { mUsePerks = use; }
        void setTestPlayerPerks(std::string_view list) { mTestPlayerPerks = std::string(list); }
        void setTestNpcPerks(std::string_view list) { mTestNpcPerks = std::string(list); }
        /// Task 0193 (stage S5-10): `combat sounds` (default true); the sounds' counters and trace (lines `CSOUND`).
        void setUseSounds(bool use) { mSounds.setEnabled(use); }
        [[nodiscard]] const CombatSounds& sounds() const { return mSounds; }
        void setSoundTrace(std::function<void(const std::string&)> trace) { mSounds.setTrace(std::move(trace)); }
        [[nodiscard]] std::size_t perkEffectsApplied() const { return mPerkEffectsApplied; }
        void setTestPlayerBow(float holdSeconds);

        /// Task 0186 (stage S5-8), all off by default. The SPEL the player's attack control casts (case-insensitive
        /// editor id; empty: the control swings; false when too long), the seconds between automatic presses (0,
        /// negative or not finite: the real key only) and how long an automatic press holds.
        bool setTestPlayerSpell(std::string_view editorId);
        void setTestPlayerCastEvery(float seconds);
        void setTestPlayerCastHold(float seconds);
        /// `actor:spell,actor:spell` (editor ids): the named actors cast. False (nothing set) for a list that is too
        /// long or has an entry without a colon.
        bool setTestCasts(std::string_view list);
        void setTestCastEvery(float seconds);
        void setTestCastHold(float seconds);
        void setTestSpellConditions(bool apply) { mApplyEffectConditions = apply; }
        /// Task 0191, off by default (see kCombatSpellBeamsKey).
        void setSpellBeams(bool on) { mSpellBeams = on; }
        void setSpellVisuals(bool on) { mSpellVisuals = on; }
        [[nodiscard]] std::size_t spellsCast() const { return mSpellsCast; }
        [[nodiscard]] std::size_t effectsApplied() const { return mEffectsApplied; }
        [[nodiscard]] std::size_t effectsEnded() const { return mEffectsEnded; }
        [[nodiscard]] std::size_t activeEffects() const { return mEffects.size(); }
        [[nodiscard]] std::size_t concentrationsHeld() const { return mConcentrations.size(); }
        [[nodiscard]] std::size_t spellShotsRemembered() const { return mSpellShots.size(); }
        [[nodiscard]] std::size_t castsWaiting() const { return mCasts.size(); }
        void onProjectileResult(const openeden::sdk::game::ProjectileResult& result) override;
        void onPlayerAttackEdge(bool pressed) override;
        /// Task 0184: what the PLAYER wields and wears, from the inventory. A melee weapon replaces the test weapon for
        /// the player's own swings (reach, stamina, damage and skill); the pieces give the player's armour rating
        /// through armourOf with the stand-in's skills. An empty value (no weapon, no pieces) is "nothing equipped":
        /// every number is then the one the test settings and the stand-in give, as before. A weapon that is not a
        /// melee weapon is ignored by the melee rule. NPCs keep the test weapon.
        struct PlayerEquipment
        {
            const WeaponRecord* mWeapon = nullptr; ///< valid as long as the data
            std::vector<ArmourPiece> mPieces;
        };
        void setPlayerEquipment(PlayerEquipment equipment);
        /// Task 0184: restores `amount` health to the player at the next update, through the engine's own health (a
        /// DamageReport that takes none off). False when there are no combat data: nothing is queued then.
        bool queuePlayerHeal(float amount);
        /// The player's armour percent as the next hit sees it (0 until the player's data is read).
        [[nodiscard]] float playerMitigationPercent() const;

        // ---- task 0200: the player's skills and levels (combat_skills.cpp) -------------------------------------------
        /// Called once for each use of a skill the player makes in a fight (a weapon hit, a block, a hit taken in
        /// armour, a spell cast): the skill slot and what the use reports (damage, damage blocked, damage taken, the
        /// spell's magicka). Empty by default; nothing else of the combat changes.
        using SkillUseHook = std::function<void(std::size_t slot, float amount)>;
        void setSkillUseHook(SkillUseHook hook) { mSkillHook = std::move(hook); }
        /// With a progression the player's skills, level, vitals and perks in a fight are its own (read again when its
        /// revision moves); without one (null, the default) they are the stand-in's, as before.
        void setPlayerProgression(const progression::PlayerProgression* progression);

        // ---- task 0190 (stage S5-9): the combat AI (combat_ai.hpp, combat_ai_run.cpp) -----------------------------------

        /// What the AI asks of the actor hook (the combat writer has no movement): the cell source drains these once a
        /// frame, BEFORE the package brain runs, and turns them into goTo / cancelGoal and `onConversation`.
        struct AiMoveIntent
        {
            enum class Kind : std::uint8_t
            {
                Suspend, ///< the package brain stops driving this actor (it is in a fight)
                Resume, ///< the fight is over: the brain takes its package up again
                Stop, ///< cancel the goal
                GoTo, ///< walk or run to (x, y, z)
            };
            Kind mKind = Kind::Stop;
            openeden::sdk::game::ActorHandle mActor;
            double mX = 0.0;
            double mY = 0.0;
            double mZ = 0.0;
            openeden::sdk::game::ActorSpeed mSpeed = openeden::sdk::game::ActorSpeed::Walk;
        };

        /// Switches the AI on (the pack setting `combat ai`). Off, nothing of it runs and the old test rules are the
        /// whole of the hostile behaviour. On, `combat test hostile` names actors that engage at once, `power every` and
        /// `bash every` fix the attack kind and `combat test block` makes the named actors block always.
        void setCombatAi(bool on);
        [[nodiscard]] bool combatAi() const { return mAiOn; }
        /// Appends the movement the AI wants since the last drain, in order, and empties its queue.
        void drainAiMoves(std::vector<AiMoveIntent>& out);
        /// The result of a goal the AI asked for (forwarded from ActorRules::onGoalFinished).
        void aiGoalFinished(openeden::sdk::game::ActorHandle handle, openeden::sdk::game::GoalResult result);
        [[nodiscard]] AiMode aiModeOf(openeden::sdk::game::ActorHandle handle) const;
        /// A damage report as it went to the core: damage the player dealt to a living actor provokes it, whatever the
        /// means (a blow, an arrow, a spell; the neutral world's wrapper tells every report, AiDamageWriter below).
        void aiNoteDamage(const openeden::sdk::game::DamageReport& report);
        [[nodiscard]] std::size_t aiEngaged() const { return mAiActors.size(); }
        /// A test aid: sets a fighter's current health (clamped to [0, maximum]; 0 kills it for the AI's purposes).
        bool setFighterHealth(openeden::sdk::game::CombatSubject subject, float health);

        /// Called with one line per combat decision (`COMBAT ...`), when set. Main thread.
        void setTrace(std::function<void(const std::string&)> trace) { mTrace = std::move(trace); }
        /// Where a warning goes (the host's log); a warning is also traced.
        void setWarning(std::function<void(const std::string&)> warning) { mWarning = std::move(warning); }
        /// How many times the player's swing timing did not arrive within kPlayerTimingTimeoutSeconds (reported once).
        [[nodiscard]] std::size_t playerTimingTimeouts() const { return mPlayerTimingTimeouts; }

        /// What the pack believes about a fighter now; false for an unknown or not yet used one.
        struct FighterView
        {
            float mHealth = 0.f;
            float mHealthMax = 0.f;
            float mStamina = 0.f;
            float mStaminaMax = 0.f;
            bool mDead = false;
            bool mStaggered = false; ///< a stagger clip is playing
            bool mBlocking = false; ///< the block rule holds now
            std::uint32_t mArrowsKept = 0; ///< task 0178: arrows that ended in this fighter's inventory
            float mMagicka = 0.f; ///< task 0186
            float mMagickaMax = 0.f;
            float mWardPower = 0.f;
            bool mDemoralized = false;
        };
        [[nodiscard]] bool fighterView(openeden::sdk::game::CombatSubject subject, FighterView& out) const;

        /// The actor hooks forwarded by the cell source (the combat rules need the handles of the actors).
        void onActorAdded(const openeden::sdk::game::ActorInfo& info);
        void onActorRemoved(openeden::sdk::game::ActorHandle handle);

        [[nodiscard]] const SlotTable& slots() const { return mSlots; }
        [[nodiscard]] std::size_t updates() const { return mUpdates; }
        [[nodiscard]] std::size_t refusedEvents() const { return mRefusedEvents; }
        [[nodiscard]] std::size_t refusedSweepResults() const { return mRefusedSweepResults; }
        [[nodiscard]] std::size_t swingers() const { return mSwingers.size(); }
        [[nodiscard]] std::size_t hostiles() const { return mHostiles.size(); }
        [[nodiscard]] std::size_t swingsRequested() const { return mSwingsRequested; }
        [[nodiscard]] std::size_t eventsSeen() const { return mEventsSeen; }
        [[nodiscard]] std::size_t clipsFinished() const { return mClipsFinished; }
        [[nodiscard]] std::size_t clipsCompleted() const { return mClipsCompleted; }
        [[nodiscard]] std::size_t hitsResolved() const { return mHitsResolved; }
        /// Task 0178: arrows launched, results told, and arrows that were kept by their target.
        [[nodiscard]] std::size_t arrowsLaunched() const { return mArrowsLaunched; }
        [[nodiscard]] std::size_t projectileResultsSeen() const { return mProjectileResultsSeen; }
        [[nodiscard]] std::size_t arrowsKept() const { return mArrowsKept; }
        [[nodiscard]] std::size_t shotsRemembered() const { return mShots.size(); }
        /// Sweeps asked for whose result has not come (review fix of task 0171).
        [[nodiscard]] std::size_t pendingSweeps() const { return mAwaiting.size(); }
        /// Updates that did nothing because the data was not ready yet.
        [[nodiscard]] std::size_t updatesWaitingForData() const { return mWaitingForData; }

    private:
        struct Fighter
        {
            openeden::sdk::game::CombatSubject mSubject;
            openeden::sdk::game::WorldId mBase = openeden::sdk::game::kNoWorldId;
            bool mReady = false; ///< the profile was asked for
            bool mHasProfile = false;
            FighterProfile mProfile;
            float mHealth = 0.f;
            float mStamina = 0.f;
            bool mDead = false;
            bool mSwinging = false;
            std::uint32_t mSwingToken = 0;
            float mCombatUntil = -1.f;
            float mNextAttack = 0.f;
            bool mStaggered = false; ///< task 0172: a stagger clip plays (mStaggerToken)
            std::uint32_t mStaggerToken = 0;
            std::uint32_t mAttackCount = 0; ///< attacks begun by the hostile rule, for the every-N choice
            bool mAiScanned = false; ///< task 0190: looked at once for hostility on sight
            // Task 0178: the bow. Drawing -> (BowDrawn event) -> Releasing -> (arrowRelease event) -> shot.
            enum class Bow : std::uint8_t
            {
                None,
                Drawing,
                Releasing,
            } mBow
                = Bow::None;
            std::uint32_t mBowToken = 0;
            std::uint32_t mArrowsKept = 0;
            float mMagicka = 0.f; ///< task 0186
            float mWardPower = 0.f; ///< what a ward holds now
            float mWardMax = 0.f;
            float mMagickaRegenAt = 0.f; ///< the combat clock at which regeneration may resume
            float mDemoralizedUntil = -1.f;
            std::vector<std::pair<std::uint32_t, float>> mPerkValues; ///< task 0179: base actor values for conditions
            std::vector<FormId> mWornKeywords; ///< task 0179
        };
        /// A bow event (BowDrawn or the release) seen since the last combatUpdate: acted on there.
        struct PendingBow
        {
            openeden::sdk::game::CombatSubject mSubject;
            std::uint32_t mToken = 0;
            bool mRelease = false; ///< the release event; else the drawn event
        };
        /// A shot launched and not yet told about.
        struct Shot
        {
            std::uint32_t mToken = 0;
            openeden::sdk::game::CombatSubject mOwner;
            float mDraw = 1.f;
        };
        struct PendingHit
        {
            openeden::sdk::game::CombatSubject mSubject;
            std::uint32_t mToken = 0;
            bool mPower = false;
            std::size_t mAskedAt = 0; ///< mUpdates when the sweep was asked for (mAwaiting only)
            bool mBash = false; ///< task 0172: the swing is a bash (mPower is false then)
        };
        struct SweepDelivery
        {
            PendingHit mWho;
            std::size_t mCount = 0;
            openeden::sdk::game::SweepHit mHits[openeden::sdk::game::kMaxSweepHits];
        };

        Fighter* find(openeden::sdk::game::CombatSubject subject);
        [[nodiscard]] const Fighter* find(openeden::sdk::game::CombatSubject subject) const;
        bool ready(Fighter& fighter);
        void regenerate(float seconds);
        void beginAttack(openeden::sdk::game::CombatRequestWriter& writer, Fighter& fighter, AttackKind kind);
        void loadSettings();
        // Task 0179: perks. All of it does nothing for a fighter with no perks, no armour perk and no ability.
        void resolveTestPerks();
        void preparePerks(Fighter& fighter);
        [[nodiscard]] bool perksMatter(const Fighter& fighter) const;
        [[nodiscard]] PerkParticipant actorParticipant(const Fighter& fighter, bool power) const;
        [[nodiscard]] PerkParticipant weaponParticipant() const;
        void applyPerksToHit(Fighter& attacker, Fighter& victim, bool power, MeleeHit& hit);
        [[nodiscard]] float perkMitigation(const Fighter& victim, bool& changed);
        [[nodiscard]] bool bowReady();
        // Task 0186 (stage S5-8): the magic runtime (combat_magic.cpp).
        struct Cast;
        struct Concentration;
        void updateMagic(openeden::sdk::game::CombatRequestWriter& writer, float step);
        void resolveTestCasts();
        void startTestCasts(openeden::sdk::game::CombatRequestWriter& writer);
        void runPlayerCast(openeden::sdk::game::CombatRequestWriter& writer);
        bool beginCast(openeden::sdk::game::CombatRequestWriter& writer, Fighter& caster, const SpellRecord& spell,
            bool heldByKey, float holdSeconds);
        void fireCast(openeden::sdk::game::CombatRequestWriter& writer, const Cast& cast);
        void tickConcentrations(openeden::sdk::game::CombatRequestWriter& writer, float step);
        void endConcentration(std::size_t index, const char* why);
        // Task 0191 (visible spells and beams).
        /// Where a ray from `caster` goes: the player's from the eye along the view, an actor's at the player.
        [[nodiscard]] bool aimRay(openeden::sdk::game::CombatRequestWriter& writer, const Fighter& caster, float range,
            openeden::sdk::game::RayRequest& out);
        [[nodiscard]] float beamRange(const SpellRecord& spell) const;
        void tickBeam(openeden::sdk::game::CombatRequestWriter& writer, Concentration& beam, Fighter& caster,
            const SpellRecord& spell, float paidSeconds);
        void touchCast(openeden::sdk::game::CombatRequestWriter& writer, Fighter& caster, const SpellRecord& spell);
        void launchBeamVisual(openeden::sdk::game::CombatRequestWriter& writer, const Fighter& caster,
            const SpellRecord& spell, const openeden::sdk::game::RayRequest& ray, float length);
        [[nodiscard]] std::uint32_t showWardBody(
            openeden::sdk::game::CombatRequestWriter& writer, const Fighter& caster, const SpellRecord& spell);
        void applySpellEffects(openeden::sdk::game::CombatRequestWriter& writer, Fighter& caster, Fighter& target,
            const SpellRecord& spell, const openeden::sdk::game::SweepHit* where);
        void applyOneEffect(openeden::sdk::game::CombatRequestWriter& writer, Fighter& caster, Fighter& target,
            const SpellRecord& spell, const SpellEffect& effect, const MagicEffectRecord& record, bool& blocked,
            const openeden::sdk::game::SweepHit* where);
        /// Changes one of the target's vitals by `delta` (clamped to [0, maximum]); the damage of a hit on health is
        /// reported and a death handled. Returns the change that really happened (signed).
        float changeVital(openeden::sdk::game::CombatRequestWriter& writer, Fighter* source, Fighter& target,
            std::int32_t value, float delta, const openeden::sdk::game::SweepHit* where);
        [[nodiscard]] bool isBusyCasting(const openeden::sdk::game::CombatSubject& subject) const;
        [[nodiscard]] bool isBusyConcentrating(const openeden::sdk::game::CombatSubject& subject) const;
        void cancelCastsOf(const openeden::sdk::game::CombatSubject& subject);
        void tickEffects(openeden::sdk::game::CombatRequestWriter& writer, float step);
        [[nodiscard]] float resistOf(const Fighter& fighter, std::int32_t resistValue);
        [[nodiscard]] bool launchSpell(openeden::sdk::game::CombatRequestWriter& writer, Fighter& caster,
            const SpellRecord& spell, const openeden::sdk::game::SubjectState* target);
        [[nodiscard]] bool resolveSpellShot(
            openeden::sdk::game::CombatRequestWriter& writer, const openeden::sdk::game::ProjectileResult& result);
        void beginBowDraw(openeden::sdk::game::CombatRequestWriter& writer, Fighter& fighter);
        void releaseBow(openeden::sdk::game::CombatRequestWriter& writer, Fighter& fighter, std::uint32_t token);
        bool launchArrow(openeden::sdk::game::CombatRequestWriter& writer, Fighter& archer, float draw,
            const openeden::sdk::game::SubjectState* target);
        void runPlayerBow(openeden::sdk::game::CombatRequestWriter& writer);
        void resolveProjectile(
            openeden::sdk::game::CombatRequestWriter& writer, const openeden::sdk::game::ProjectileResult& result);
        [[nodiscard]] bool isBlocking(const Fighter& fighter) const;
        [[nodiscard]] bool isBlocker(openeden::sdk::game::ActorHandle handle) const;
        // Task 0190 (stage S5-9): the combat AI (combat_ai_run.cpp). Everything here is inert while mAiOn is false.
        struct AiActor
        {
            openeden::sdk::game::ActorHandle mHandle;
            AiActorState mState;
            const SpellRecord* mSpell = nullptr;
            bool mSpellLooked = false;
            bool mMoving = false; ///< a goal is out (the brain was told to wait, the core is walking)
            double mGoalX = 0.0;
            double mGoalY = 0.0;
            float mGoalAt = -1.0e9f; ///< the combat clock when the goal was issued
            float mFacedAt = -1.0e9f;
            float mGuardClipEnds = -1.f; ///< the raised-guard clip plays until then
            std::uint32_t mGuardToken = 0;
            bool mFleeGoalOut = false; ///< a flee goal was issued and has not ended
            int mGoalFails = 0; ///< goals in a row that ended NoPath or Blocked outside a flee
            bool mGiveUp = false; ///< too many failed goals: it lets go of the fight
        };
        struct AiWatcher
        {
            openeden::sdk::game::ActorHandle mHandle;
            float mNextSight = 0.f;
        };
        void runAi(openeden::sdk::game::CombatRequestWriter& writer);
        void aiScan();
        bool aiBegin(Fighter& fighter, const char* reason);
        void aiForget(openeden::sdk::game::ActorHandle handle);
        void aiPickSpell(Fighter& fighter, AiActor& actor);
        void aiRelease(std::size_t index, const char* why);
        void aiProvoke(const Fighter& attacker, Fighter& victim);
        void aiRaiseGuard(openeden::sdk::game::CombatRequestWriter& writer, AiActor& actor, Fighter& fighter);
        void aiMove(openeden::sdk::game::CombatRequestWriter& writer, AiActor& actor, Fighter& fighter,
            AiMove move, const openeden::sdk::game::SubjectState& self, const openeden::sdk::game::SubjectState& target,
            float distance, float reach);
        void aiPushGoTo(AiActor& actor, double x, double y, double z, openeden::sdk::game::ActorSpeed speed);
        [[nodiscard]] bool aiGuardUp(openeden::sdk::game::ActorHandle handle) const;
        [[nodiscard]] AiActor* aiFind(openeden::sdk::game::ActorHandle handle);
        [[nodiscard]] const AiActor* aiFind(openeden::sdk::game::ActorHandle handle) const;
        bool mAiOn = false;
        SlotTable mGuardSlots;
        std::vector<AiActor> mAiActors;
        std::vector<AiWatcher> mAiWatchers;
        std::size_t mAiScanCursor = 0;
        std::map<openeden::sdk::game::WorldId, std::uint32_t> mAiEngagements;
        std::map<openeden::sdk::game::WorldId, float> mAiCooldown; ///< the combat clock before which it does not engage
        std::vector<AiMoveIntent> mAiMoves;

        void startHostiles(openeden::sdk::game::CombatRequestWriter& writer);
        void runPlayerSwing(openeden::sdk::game::CombatRequestWriter& writer);
        void processHit(openeden::sdk::game::CombatRequestWriter& writer, const PendingHit& hit);
        void resolveSweep(openeden::sdk::game::CombatRequestWriter& writer, const SweepDelivery& delivery);
        void applyHit(openeden::sdk::game::CombatRequestWriter& writer, Fighter& attacker, bool power, bool bash,
            const openeden::sdk::game::SweepHit& hit, Fighter& victim, const MeleeHit* preset = nullptr);
        void trace(const std::string& line) const
        {
            if (mTrace)
                mTrace(line);
        }

        SlotTable mSlots;
        CombatData* mData = nullptr;
        CombatSettings mCombatSettings;
        BlockSettings mBlockSettings;
        bool mHaveSettings = false;
        int mPowerEvery = 0;
        int mBashEvery = 0;
        std::string mTestBlock; ///< lower case
        std::vector<openeden::sdk::game::ActorHandle> mBlockers;
        const WeaponRecord* mWeapon = nullptr;
        bool mWeaponLooked = false;
        // Task 0184
        PlayerEquipment mEquip;
        bool mEquipDirty = false;
        float mPendingHeal = 0.f;
        float mBaseMitigation = 0.f; ///< the stand-in's, kept so that unequipping restores it
        bool mBaseMitigationKnown = false;
        std::vector<ArmourPiece> mBasePieces;
        void applyPlayerEquipment();
        [[nodiscard]] const WeaponRecord* weaponFor(const Fighter& fighter) const;
        void applyPlayerHeal(openeden::sdk::game::CombatRequestWriter& writer);
        // Task 0200
        SkillUseHook mSkillHook;
        const progression::PlayerProgression* mProgression = nullptr;
        std::uint64_t mProgressionSynced = 0;
        bool mHaveStandProfile = false;
        FighterProfile mStandProfile; ///< the player's profile as ready() made it, before the progression
        void syncProgression(openeden::sdk::game::CombatRequestWriter& writer);
        void reportSkillUse(std::size_t slot, float amount);
        void reportSpellUse(const Fighter& caster, const SpellRecord& spell, float magicka);
        void reportHitSkills(const Fighter& attacker, const Fighter& victim, const WeaponRecord* weapon,
            float blow, float taken, bool blocked);
        std::string mTestWeapon; ///< lower case
        std::string mTestHostile; ///< lower case
        std::string mTestSwing; ///< lower case
        std::vector<openeden::sdk::game::ActorHandle> mSwingers;
        std::vector<openeden::sdk::game::ActorHandle> mHostiles;
        // Task 0192: the crowd test. mOpponents lists each duelist with its one enemy (both directions).
        std::size_t mTestCrowd = 0;
        std::size_t mTestDuels = 0;
        float mCrowdDelay = kDefaultCrowdDelay;
        bool mCrowdChosen = false;
        std::vector<std::pair<openeden::sdk::game::ActorHandle, openeden::sdk::game::ActorHandle>> mOpponents;
        void chooseCrowd(openeden::sdk::game::CombatRequestWriter& writer);
        [[nodiscard]] bool crowdOn() const { return mTestCrowd > 0 || mTestDuels > 0; }
        /// With a crowd test an NPC may only hurt its enemies (the player for the hostile ones, its opponent for a
        /// duelist); without one anybody in the arc may be hit, as before.
        [[nodiscard]] bool mayHit(const Fighter& attacker, const Fighter& victim) const;
        float mPlayerAutoPeriod = 0.f;
        float mNextPlayerAuto = 1.f;
        bool mPlayerAttackPending = false;
        bool mPlayerPower = false;
        bool mPlayerSwingActive = false;
        bool mPlayerHitDone = false;
        float mPlayerSwingStart = 0.f;
        std::uint32_t mPlayerSwingToken = 0;
        // Reserved for the usual cell; a cell with more actors makes onActorAdded grow it (an allocation per added
        // actor beyond that, never one per frame). kMaxCombatFighters bounds it.
        std::vector<Fighter> mFighters; ///< index 0 is the player
        std::vector<PendingHit> mPendingHits; ///< Hit events seen since the last combatUpdate
        std::vector<PendingBow> mPendingBow; ///< task 0178
        std::vector<Shot> mShots; ///< task 0178
        std::vector<openeden::sdk::game::ProjectileResult> mProjectileResults; ///< told, resolved in combatUpdate
        SlotTable mBowSlots;
        SlotTable mMagicSlots;
        // Task 0186. A cast: an NPC's waits for its clip's SpellFire event; the player's waits for a time.
        struct Cast
        {
            openeden::sdk::game::CombatSubject mCaster;
            const SpellRecord* mSpell = nullptr;
            std::uint32_t mToken = 0; ///< the clip's token (0 for the player)
            float mFireAt = -1.f; ///< the player's: the combat clock at which the spell goes
            float mStarted = 0.f;
            float mHold = 0.f; ///< a concentration spell: seconds to hold
            bool mHeldByKey = false; ///< the player's real key: held until the release edge
            bool mFireSeen = false; ///< the SpellFire event came
        };
        struct Concentration
        {
            openeden::sdk::game::CombatSubject mCaster;
            const SpellRecord* mSpell = nullptr;
            float mEndAt = -1.f; ///< the combat clock; negative: until released
            float mTraceAt = 0.f;
            bool mHeldByKey = false;
            bool mBeam = false; ///< task 0191: a held spell that is aimed or touch, a ray every update
            float mNextVisual = 0.f; ///< the combat clock of the next drawn-only copy of the projectile
            std::uint32_t mEffectToken = 0; ///< a shown effect (a ward's body) to hide when this ends, 0 none
        };
        struct SpellShot
        {
            std::uint32_t mToken = 0;
            openeden::sdk::game::CombatSubject mOwner;
            const SpellRecord* mSpell = nullptr;
        };
        struct TestCaster
        {
            std::string mActor; ///< lower case editor id
            std::string mSpellName; ///< lower case editor id
            const SpellRecord* mSpell = nullptr;
            bool mLooked = false;
            openeden::sdk::game::ActorHandle mHandle;
            float mNext = 0.f;
        };
        EffectList mEffects;
        std::vector<Cast> mCasts;
        std::vector<Concentration> mConcentrations;
        std::vector<SpellShot> mSpellShots;
        std::vector<TestCaster> mTestCasters;
        std::vector<std::pair<openeden::sdk::game::ActorHandle, std::string>> mActorNames; ///< lower case editor ids
        bool mPlayerCastWanted = false; ///< a press waits for the cast clip's timing
        float mPlayerCastWantedAt = 0.f;
        bool mPlayerCastHeld = false; ///< the attack control is down (a real key or an automatic press)
        std::vector<bool> mCastEdges; ///< attack-control edges for the spell (true: pressed)
        std::string mTestPlayerSpell; ///< lower case
        const SpellRecord* mPlayerSpell = nullptr;
        bool mPlayerSpellLooked = false;
        float mPlayerCastEvery = 0.f;
        float mPlayerCastHold = kDefaultCastHold;
        float mNextPlayerCast = 1.f;
        float mAutoCastReleaseAt = -1.f;
        float mCastPeriod = kDefaultCastPeriod;
        float mCastHold = kDefaultCastHold;
        bool mApplyEffectConditions = false;
        bool mSpellBeams = false;
        bool mSpellVisuals = false;
        std::vector<std::uint32_t> mPendingHides; ///< shown effects whose spell ended, hidden in the next update
        MagicSettings mMagicSettings;
        bool mHaveMagicSettings = false;
        std::size_t mSpellsCast = 0;
        std::size_t mEffectsApplied = 0;
        std::size_t mEffectsEnded = 0;
        std::string mTestAmmo; ///< lower case
        const AmmoRecord* mAmmo = nullptr;
        const ProjectileRecord* mProjectile = nullptr;
        bool mAmmoLooked = false;
        ProjectileSettings mProjectileSettings;
        float mBowSpeedBonus = 0.f;
        bool mUsePerks = true;
        std::string mTestPlayerPerks;
        std::string mTestNpcPerks;
        std::vector<FormId> mPlayerTestPerks; ///< resolved once
        std::vector<FormId> mNpcTestPerks;
        bool mPerksResolved = false;
        std::unique_ptr<PerkEngine> mPerkEngine;
        std::size_t mPerkEffectsApplied = 0;
        float mPlayerBowHold = 0.f;
        float mNextPlayerBow = 1.f;
        std::vector<bool> mPlayerEdges; ///< attack-control edges since the last combatUpdate (true: pressed)
        bool mPlayerDrawing = false;
        float mPlayerDrawStart = 0.f;
        float mPlayerDrawTime = 0.f; ///< playback seconds of the draw clip to the BowDrawn event, 0 until read
        bool mPlayerDrawKicked = false;
        float mPlayerAutoReleaseAt = -1.f;
        std::size_t mArrowsLaunched = 0;
        std::size_t mProjectileResultsSeen = 0;
        std::size_t mArrowsKept = 0;
        std::vector<PendingHit> mAwaiting; ///< sweeps asked for, result not here yet
        std::vector<SweepDelivery> mDeliveries; ///< sweep results waiting for the next combatUpdate
        std::function<void(const std::string&)> mTrace;
        std::function<void(const std::string&)> mWarning;
        // The timing of the player's two attack clips, once known: asking the engine again is then never needed.
        openeden::sdk::game::ClipTiming mKnownTiming[2];
        bool mHaveTiming[2] = { false, false };
        std::size_t mWaitingForData = 0;
        bool mWarnedAwaitingFull = false;
        bool mWarnedHitsFull = false; ///< task 0192
        float mPlayerHitTime = 0.f; ///< of the swing that runs: from the sidecar
        float mPlayerSwingLength = 0.f;
        bool mPlayerTimingKicked = false; ///< the read of the player's attack clip sidecars was started
        float mPlayerTimingSince = -1.f; ///< clock of the first refused ask, -1 when none is waiting
        std::size_t mPlayerTimingTimeouts = 0;
        float mClock = 0.f;
        float mNextSwing = kTestSwingFirstDelay;
        std::uint32_t mNextToken = 1;
        std::size_t mUpdates = 0;
        std::size_t mRefusedEvents = 0;
        std::size_t mRefusedSweepResults = 0;
        std::size_t mSwingsRequested = 0;
        std::size_t mEventsSeen = 0;
        std::size_t mClipsFinished = 0;
        std::size_t mClipsCompleted = 0;
        std::size_t mHitsResolved = 0;
        // Task 0193 (stage S5-10): the sounds. Defined in combat_sound.cpp's companion block of combat.cpp.
        CombatSounds mSounds;
        void soundEvent(const openeden::sdk::game::ClipEvent& event);
        void soundHit(openeden::sdk::game::CombatRequestWriter& writer, const Fighter& attacker, const Fighter& victim,
            bool blocked, bool bash);
    };

    /// Task 0190: passes every request on and tells the AI about each damage report that went through, so that damage
    /// of every kind (a blow, an arrow, a spell) provokes the victim. The cell source wraps the combat writer in it.
    class AiDamageWriter final : public openeden::sdk::game::CombatRequestWriter
    {
    public:
        AiDamageWriter(openeden::sdk::game::CombatRequestWriter& inner, SkyrimCombat& combat)
            : mInner(inner)
            , mCombat(combat)
        {
        }
        bool playClip(openeden::sdk::game::CombatSubject subject, const openeden::sdk::game::ClipRequest& r) override
        {
            return mInner.playClip(subject, r);
        }
        bool stopClip(openeden::sdk::game::CombatSubject subject) override { return mInner.stopClip(subject); }
        bool requestSweep(
            openeden::sdk::game::CombatSubject subject, const openeden::sdk::game::SweepRequest& r) override
        {
            return mInner.requestSweep(subject, r);
        }
        bool reportDamage(const openeden::sdk::game::DamageReport& report) override
        {
            const bool ok = mInner.reportDamage(report);
            mCombat.aiNoteDamage(report);
            return ok;
        }
        bool faceHeading(openeden::sdk::game::CombatSubject subject, float heading) override
        {
            return mInner.faceHeading(subject, heading);
        }
        bool clipTiming(std::string_view clip, bool perSex, openeden::sdk::game::ClipTiming& out) override
        {
            return mInner.clipTiming(clip, perSex, out);
        }
        bool subjectState(openeden::sdk::game::CombatSubject subject, openeden::sdk::game::SubjectState& out) override
        {
            return mInner.subjectState(subject, out);
        }
        bool launchProjectile(
            openeden::sdk::game::CombatSubject owner, const openeden::sdk::game::ProjectileRequest& r) override
        {
            return mInner.launchProjectile(owner, r);
        }
        bool setClipSpeed(openeden::sdk::game::CombatSubject subject, float multiplier) override
        {
            return mInner.setClipSpeed(subject, multiplier);
        }
        bool clipEventTime(
            std::string_view clip, bool perSex, openeden::sdk::game::ClipEventKind kind, float& out) override
        {
            return mInner.clipEventTime(clip, perSex, kind, out);
        }
        bool reportDeath(openeden::sdk::game::ActorHandle actor) override { return mInner.reportDeath(actor); }
        // Contract 0.5 (tasks 0191, 0193): a wrapper that does not forward a new virtual silently gives the pack the
        // default, which refuses.
        bool castRay(openeden::sdk::game::CombatSubject from, const openeden::sdk::game::RayRequest& request,
            openeden::sdk::game::RayHit& out) override
        {
            return mInner.castRay(from, request, out);
        }
        bool showEffect(
            openeden::sdk::game::CombatSubject subject, const openeden::sdk::game::EffectRequest& request) override
        {
            return mInner.showEffect(subject, request);
        }
        bool hideEffect(std::uint32_t token) override { return mInner.hideEffect(token); }
        bool playSound(const openeden::sdk::game::SoundRequest& request) override { return mInner.playSound(request); }

    private:
        openeden::sdk::game::CombatRequestWriter& mInner;
        SkyrimCombat& mCombat;
    };
}

#endif
