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

#include <game_skyrimse/ability_numbers.hpp>
#include <game_skyrimse/barks.hpp>
#include <game_skyrimse/combat_ai.hpp>
#include <game_skyrimse/combat_factions.hpp>
#include <game_skyrimse/combat_gear.hpp>
#include <game_skyrimse/combat_sound.hpp>
#include <game_skyrimse/combat_stats.hpp>
#include <game_skyrimse/creatures.hpp>
#include <game_skyrimse/dragons.hpp>
#include <game_skyrimse/image_space_mod.hpp>
#include <game_skyrimse/magic.hpp>
#include <game_skyrimse/magic_conditions.hpp>
#include <game_skyrimse/magic_status.hpp>
#include <game_skyrimse/player_hands.hpp>
#include <game_skyrimse/progression.hpp>
#include <game_skyrimse/shouts.hpp>
#include <game_skyrimse/summons.hpp>
#include <game_skyrimse/value_modifiers.hpp>
#include <game_skyrimse/werewolf.hpp>

#include <openeden/game/actor_rules.hpp>
#include <openeden/game/combat_rules.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace game_skyrimse
{
    namespace crime
    {
        struct CrimeActor;
    }

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
    /// Task 0197: targets from relations, followers, guards and sight (needs `combat ai`); off by default.
    inline constexpr std::string_view kCombatFactionsKey = "combat factions";
    /// Task 0197 test rule: this actor (editor id) attacks the nearest valid target on sight.
    inline constexpr std::string_view kCombatTestAttackerKey = "combat test attacker";
    inline constexpr std::string_view kCombatFactionsDelayKey = "combat factions delay";
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
    /// ids): that actor casts the spell every `combat test cast every` seconds (default 4, first cast after one
    /// period), a concentration spell for `combat test cast hold` seconds (default 3). `combat test spell conditions`
    /// applies every effect whatever its conditions say; with it unset (task 0236) an effect applies when its
    /// conditions hold (magic_conditions.hpp) and one whose conditions fail, or cannot be answered, does nothing.
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
    /// Task 0242, off by default: target actor spells (Heal Other, Calm) go to the actor in the sights, target location
    /// spells (the runes) are placed where their projectile lands and go off when an enemy comes near, and an aimed
    /// spell whose effect has an area (the Fireball) hurts everyone within that many feet of where it ended.
    inline constexpr std::string_view kCombatSpellDeliveryKey = "combat spell delivery";
    /// Task 0295, off by default: a weapon's enchantment applies on a landed blow, a staff casts its enchantment (the
    /// player's attack control, an NPC with a staff), a Bound Weapon effect puts its weapon in the caster's hand.
    inline constexpr std::string_view kCombatWeaponMagicKey = "combat weapon magic";
    /// A rune stays this long unless it goes off first, seconds (ours: the data gives no lifetime for a rune).
    inline constexpr float kRuneLifetimeSeconds = 600.f;
    /// The most runes standing at once; a new one replaces the oldest (ours).
    inline constexpr std::size_t kMaxRunes = 32;
    /// How far into the world a rune's projectile is drawn where it lands, game units (ours).
    inline constexpr float kRuneStickDepth = 4.f;
    /// The distance an enemy sets a rune off when the PROJ names none, game units (ours; the three runes name 150).
    inline constexpr float kDefaultRuneProximity = 150.f;
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
    /// Task 0192: the most swings (hit events, sweeps waiting for their result, results to resolve, bow events) the
    /// pack keeps in one frame. It mirrors NeutralCombatBridge::kMaxPlaying (64): the bridge plays no more clips than
    /// that, so no more hit events can come in one frame. It was 32, and a crowd of more than 32 fighters that reached
    /// their HitFrame together lost the rest of the blows without a word.
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
        /// Task 0212: the creature profile of the race (null for a person and with the setting off): its clips are the
        /// fighter's attack, reaction and death clips. Not owned (static).
        const CreatureProfile* mCreature = nullptr;
        /// Task 0240: the spells the race's attacks carry (RACE ATKD +8, each once, in order): a creature's disease is
        /// one of them. Empty for a person.
        std::vector<FormId> mAttackSpells;
        /// Task 0254: the NPC's own ACBS Unique and Essential flags. A unique actor stays dead for good; the others
        /// come back when their cell resets (iHoursToRespawnCell).
        bool mUnique = false;
        bool mEssential = false;
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

        /// Task 0216: the same, for one placement. `variation` is the placement's own number (the low 32 bits of its
        /// id), the number the appearance of the actor was described with, so that an NPC whose template is a leveled
        /// list is the same creature in combat as on screen. The default ignores it.
        virtual bool fighterFor(openeden::sdk::game::WorldId base, std::uint32_t variation, FighterProfile& out)
        {
            (void)variation;
            return fighter(base, out);
        }
        /// The stand-in for the player (level 1, skills 15, no armour: the player has no records).
        virtual FighterProfile playerFighter() = 0;
        /// The WEAP with this editor id (case-insensitive), or null. The pointer stays valid as long as the data.
        virtual const WeaponRecord* weaponByEditorId(std::string_view editorId) = 0;
        /// Task 0295: the WEAP with this form id (a Bound Weapon effect names its weapon), null when unknown.
        virtual const WeaponRecord* weaponById(FormId id)
        {
            (void)id;
            return nullptr;
        }
        virtual CombatSettings settings() = 0;
        /// Task 0197: how the factions of `a` feel about those of `b` (symmetric: an enemy in either direction is an
        /// enemy). Default: neutral.
        virtual openeden::sdk::game::ActorRelation factionRelation(const FighterProfile& a, const FighterProfile& b)
        {
            (void)a;
            (void)b;
            return openeden::sdk::game::ActorRelation::Neutral;
        }
        /// Task 0178: the AMMO with this editor id (case-insensitive), or null; and the PROJ of an ammunition (null
        /// when it has none). Defaults: none.
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
        /// Task 0299: a WEAP and an AMMO by form id (a script's Weapon.Fire names its weapon and ammunition so).
        virtual const WeaponRecord* weaponById(FormId id)
        {
            (void)id;
            return nullptr;
        }
        virtual const AmmoRecord* ammoById(FormId id)
        {
            (void)id;
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
        /// Task 0257: the IMAD with this editor id (case-insensitive), or null. Default: none.
        virtual const ImageModifierRecord* imageModifierByEditorId(std::string_view editorId)
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
        /// Task 0242: an EXPL (the area a projectile leaves), null when unknown.
        virtual const ExplosionRecord* explosionById(FormId id)
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
        /// Task 0238: does this RACE carry the Child flag (RACE DATA +32 bit 2)? IsChild asks it. Default: no.
        virtual bool raceIsChild(FormId race)
        {
            (void)race;
            return false;
        }
        /// Task 0238: the stored value of a GLOB (GetGlobalValue in an effect's conditions). False when the form is not
        /// a global the data knows. Default: none.
        virtual bool globalDefault(FormId global, float& value)
        {
            (void)global;
            (void)value;
            return false;
        }
        /// Task 0205: a shout by form id or editor id (case-insensitive) and the two shout GMSTs. Defaults: none.
        virtual const ShoutRecord* shoutById(FormId id)
        {
            (void)id;
            return nullptr;
        }
        virtual const ShoutRecord* shoutByEditorId(std::string_view editorId)
        {
            (void)editorId;
            return nullptr;
        }
        virtual shouts::Settings shoutSettings() { return {}; }
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
        /// Task 0214: what the NPC with this base record fights with (its best melee weapon, its shield, the armour of
        /// its inventory). Asked only with `combat real gear`. `worn` is the armour the profile already counts. False
        /// (the default) leaves the fighter with the test weapon and no shield.
        virtual bool fighterGear(
            openeden::sdk::game::WorldId base, const std::vector<ArmourPiece>& worn, FighterGear& out)
        {
            (void)base;
            (void)worn;
            out = {};
            return false;
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

        /// Task 0212: the wolf's clips (CreatureProfile of the wolf, animation type 0 = no weapon): Attack,
        /// PowerAttack, Recoil, Stagger and Death. Clip names are the sidecar's generator names, which equal the files'
        /// (VERIFIED for these five in animationdata/wolfproject.txt).
        [[nodiscard]] static SlotTable wolf();
        /// Task 0214: the melee clips of every weapon class (see combat_gear.hpp): the one-handed set (the seven of
        /// humanoid() plus the raised guard), the two-handed sword (2HM_*), the two-handed axe and hammer (2HW_*) and
        /// the shield (SHD_BlockHit, SHD_BlockBash, SHD_BlockIdle). Names from the listing of Skyrim - Animations.bsa
        /// and the generators of animationdata/defaultmale.txt (docs/tasks/0214-skyrim-real-weapons.md).
        [[nodiscard]] static SlotTable gear();

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
    /// Task 0299: the direction a placed object faces as the engine's heading and pitch (heading 0 faces +Y, pitch
    /// positive up), from the stored Euler angles of its placement (radians; R = Rx * Ry * Rz with every angle
    /// negated, as eulerToQuat does: a positive stored X angle tilts the object's +Y axis DOWN, RECALLED-VERIFY by the
    /// dart ports' frames). The model's forward axis is +Y.
    struct TrapAim
    {
        float heading = 0.f;
        float pitch = 0.f;
    };
    [[nodiscard]] TrapAim trapAimOf(double rx, double ry, double rz);
    /// Task 0299: where a trap's dart leaves: the source's place moved `kTrapMuzzleForward` along the facing. The dart
    /// port's NIF (`meshes/traps/trapdartportsingle01.nif`) has a ProjectileNode at (0, 16, 0) with no rotation, read
    /// from its node block; a dart that starts at the origin hits the port's own collision at once (seen in game).
    inline constexpr double kTrapMuzzleForward = 16.0;
    void trapMuzzle(const double (&from)[3], const TrapAim& aim, double (&out)[3]);

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
        /// Task 0214 (`combat real gear`, off by default): fighters use their own melee weapon and shield and play the
        /// clips of that weapon's class. The test weapon still overrides an NPC's weapon when it is set.
        void setRealGear(bool on) { mRealGear = on; }
        [[nodiscard]] bool realGear() const { return mRealGear; }

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
        /// Task 0236: with it set every effect applies whatever its conditions say (the override); unset (the default)
        /// the effects whose conditions hold apply and the others do not.
        void setTestSpellConditions(bool apply) { mApplyEffectConditions = apply; }
        /// Task 0238: the rate a vital of `subject` regenerates at now (percent of its maximum a second; 0 health, 1
        /// magicka, 2 stamina), with the Fortify and Damage Rate effects running on it. -1 for an unknown fighter.
        [[nodiscard]] float regenRatePercent(openeden::sdk::game::CombatSubject subject, int vital) const;
        /// Task 0238: where the player stands (IsInInterior and GetInWorldspace in an effect's conditions: the actors
        /// that are loaded stand in the player's cell or worldspace). Returns false when unknown. Without it those two
        /// functions leave the effect's list unanswered.
        void setPlaceSource(std::function<bool(effectconditions::Place&)> source) { mPlaceSource = std::move(source); }
        /// Task 0238: a source of GLOB values that comes before the data's own stored values (a script's SetValue).
        void setGlobalSource(effectconditions::Evaluator::GlobalLookup source) { mGlobalSource = std::move(source); }
        /// Task 0236: how the effect condition lists came out, for the traces and the tests.
        [[nodiscard]] std::uint64_t effectConditionsPassed() const { return mEffectConditionsPassed; }
        [[nodiscard]] std::uint64_t effectConditionsFailed() const { return mEffectConditionsFailed; }
        [[nodiscard]] std::uint64_t effectConditionsUnanswered() const { return mEffectConditionsUnanswered; }
        /// Task 0191, off by default (see kCombatSpellBeamsKey).
        void setSpellBeams(bool on) { mSpellBeams = on; }
        void setSpellVisuals(bool on) { mSpellVisuals = on; }
        /// Task 0242, off by default (see kCombatSpellDeliveryKey).
        void setSpellDelivery(bool on) { mSpellDelivery = on; }
        /// Task 0250 (combat_summons.cpp): Summon Creature and Reanimate effects. Needs the delivery setting (Reanimate
        /// is target actor delivery) and the hooks; off by default.
        void setSummons(bool on) { mSummonsOn = on; }
        void setSummonHooks(SummonHooks hooks) { mSummonHooks = std::move(hooks); }
        /// Task 0254 (combat_deaths.cpp): told the placement of a corpse a Reanimate spell hid (it is saved so that the
        /// corpse comes back as a corpse after a load). Empty: nobody is told.
        void setCorpseRaisedSink(std::function<void(openeden::sdk::game::WorldId)> sink)
        {
            mCorpseRaised = std::move(sink);
        }
        /// Task 0254: a saved game said this actor was dead. Marks it dead (health 0, no more swings), plays its death
        /// clip at kRestoredDeathClipSpeed so that it lies in the clip's last pose at once, and tells the core it is
        /// dead (loot opens). `writer` must be the core's own writer, not a wrapped one: the report must not reach the
        /// script, crime or HUD hooks again (no OnDeath, no quest kill). False while the actor's data is not read yet
        /// or the actor is unknown.
        bool restoreDead(openeden::sdk::game::CombatRequestWriter& writer, openeden::sdk::game::ActorHandle handle);
        void setMaxSummons(std::size_t n) { mMaxSummons = n > 0 ? n : 1; }
        /// A test aid (`summons test kill = <editor id>`): the named actor dies kSummonTestKillSeconds after the data
        /// was read, so a scenario has a corpse to raise. Lower case is applied here.
        void setTestKill(std::string_view editorId);
        /// A test aid (`summons test magicka`): the stand-in player's maximum magicka (0: the stand-in's 100).
        void setTestPlayerMagicka(float magicka) { mTestPlayerMagicka = std::isfinite(magicka) ? magicka : 0.f; }
        [[nodiscard]] const SummonLedger& summons() const { return mSummons; }
        [[nodiscard]] std::size_t runesStanding() const { return mRunes.size(); }
        [[nodiscard]] std::size_t runesSetOff() const { return mRunesSetOff; }
        [[nodiscard]] std::size_t explosions() const { return mExplosionsDone; }
        [[nodiscard]] std::size_t spellsCast() const { return mSpellsCast; }
        [[nodiscard]] std::size_t effectsApplied() const { return mEffectsApplied; }
        /// Task 0233: paralysis, frenzy and fear effects that took hold.
        [[nodiscard]] std::size_t statusesApplied() const { return mStatusesApplied; }
        /// Task 0233: the actor is held by a paralysis, runs from a fear, or is in a frenzy (false for an unknown one).
        [[nodiscard]] bool paralyzed(openeden::sdk::game::CombatSubject subject) const;
        [[nodiscard]] bool feared(openeden::sdk::game::CombatSubject subject) const;
        [[nodiscard]] bool frenzied(openeden::sdk::game::CombatSubject subject) const;
        [[nodiscard]] std::size_t effectsEnded() const { return mEffectsEnded; }
        [[nodiscard]] std::size_t activeEffects() const { return mEffects.size(); }
        [[nodiscard]] std::size_t concentrationsHeld() const { return mConcentrations.size(); }
        [[nodiscard]] std::size_t spellShotsRemembered() const { return mSpellShots.size(); }
        [[nodiscard]] std::size_t castsWaiting() const { return mCasts.size(); }
        void onProjectileResult(const openeden::sdk::game::ProjectileResult& result) override;
        void onPlayerAttackEdge(bool pressed) override;
        /// Task 0205 (M-S7): the player's shout. `state` (not owned; null: no shouts) holds the words, the souls and
        /// the cooldown; the shout control is `onPlayerShoutEdge`. `setTestPlayerShout` equips the SHOU with this
        /// editor id when the data are ready (false when the name is too long); `setTestShoutEvery` presses the control
        /// by itself every N seconds (0, negative or not finite: the real key only) and holds it `setTestShoutHold`
        /// seconds.
        void setShoutState(shouts::ShoutState* state) { mShouts = state; }
        void onPlayerShoutEdge(bool pressed) override;
        bool setTestPlayerShout(std::string_view editorId);
        void setTestShoutEvery(float seconds);
        void setTestShoutHold(float seconds);
        [[nodiscard]] std::size_t shoutsSpoken() const { return mShoutsSpoken; }
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
        /// Task 0229: the poison on the player's wielded melee weapon. The next `charges` landed blows apply the
        /// spell's effects to the target through the magic runtime; 0 charges clears it. The hook is told the charges
        /// left after each blow.
        void setPlayerPoison(SpellRecord spell, int charges);
        [[nodiscard]] int playerPoisonCharges() const { return mPoisonCharges; }

        // ---- task 0295 (weapon magic: enchanted weapons, staves, bound weapons)
        /// On: a weapon's enchantment (WEAP EITM, a fire and forget touch ENCH) applies its effects to the target of
        /// each landed, unblocked blow; a staff casts its enchantment (the player's attack control, an NPC's cast
        /// action) at no magicka; a Bound Weapon effect puts its weapon in the caster's hand for its duration. Off by
        /// default.
        void setWeaponMagic(bool on) { mWeaponMagic = on; }
        [[nodiscard]] bool weaponMagic() const { return mWeaponMagic; }
        /// The enchantment spell of the player's equipped staff (null: no staff, no enchantment, setting off or data
        /// not read).
        [[nodiscard]] const SpellRecord* playerStaffSpell();
        /// The bound weapon a fighter holds now (null: none, or it has run out).
        [[nodiscard]] const WeaponRecord* boundWeaponOf(openeden::sdk::game::CombatSubject subject) const;
        void setPoisonSpentHook(std::function<void(int remaining)> hook) { mPoisonSpent = std::move(hook); }
        /// Task 0184: restores `amount` health to the player at the next update, through the engine's own health (a
        /// DamageReport that takes none off). False when there are no combat data: nothing is queued then.
        bool queuePlayerHeal(float amount);
        /// The player's armour percent as the next hit sees it (0 until the player's data is read).
        [[nodiscard]] float playerMitigationPercent() const;

        // ---- task 0200: the player's skills and levels (combat_skills.cpp)
        // -------------------------------------------
        /// Called once for each use of a skill the player makes in a fight (a weapon hit, a block, a hit taken in
        /// armour, a spell cast): the skill slot and what the use reports (damage, damage blocked, damage taken, the
        /// spell's magicka). Empty by default; nothing else of the combat changes.
        using SkillUseHook = std::function<void(std::size_t slot, float amount)>;
        void setSkillUseHook(SkillUseHook hook) { mSkillHook = std::move(hook); }

        // ---- task 0227: sneak attacks ----------------------------------------------------------------------------
        /// Asked once per blow or arrow of the PLAYER on an actor (not a bash): the multiplier before perks (1 or less:
        /// not a sneak attack), from the victim's placement reference, the weapon's WEAP animation type (0 bare hands)
        /// and whether it is a shot. `done` is told of a blow that landed with its final multiplier (perks included).
        /// Both empty by default: nothing of the combat changes.
        using SneakMultiplierHook = std::function<float(std::uint32_t victim, std::uint8_t animType, bool ranged)>;
        using SneakDoneHook = std::function<void(std::uint32_t victim, float multiplier)>;
        void setSneakAttackHooks(SneakMultiplierHook multiplier, SneakDoneHook done)
        {
            mSneakMultiplier = std::move(multiplier);
            mSneakDone = std::move(done);
        }
        /// With a progression the player's skills, level, vitals and perks in a fight are its own (read again when its
        /// revision moves); without one (null, the default) they are the stand-in's, as before.
        void setPlayerProgression(const progression::PlayerProgression* progression);
        /// Task 0225 (character creation): the player's race abilities (RACE SPLO). `chosen` are the abilities of the
        /// race he chose, `standIn` those of the race the stand-in player record already brought (the Nord's): the
        /// profile's spell list loses the second and gains the first, and the ability sums (damage resist, weapon
        /// speed) are worked out again. A copy; the call may come at any time.
        void setPlayerRaceAbilities(std::vector<FormId> chosen, std::vector<FormId> standIn);
        /// Task 0237 (standing stones, shrine blessings): spells the player has beyond his record's, worked into his
        /// ability sums (damage resistance) and the resist lookups like the race's. A copy; replaces the last list.
        void setPlayerExtraAbilities(std::vector<FormId> spells);
        /// Task 0241: perks the player has beyond his progression's (Actor.AddPerk of the Papyrus host: the Ritual
        /// stone's). A copy; replaces the last list; the perks join his list like the test setting's.
        void setPlayerExtraPerks(std::vector<FormId> perks);
        /// Task 0259 (werewolf): what the beast form changes in the player's fighting: his bare hands become the race's
        /// claws and his clips the werewolf's (the creature row `werewolf::playerProfile()`); an equipped weapon is
        /// ignored while it lasts. `setPlayerBeast(nullptr)` makes him human again.
        using BeastForm = werewolf::BeastNumbers;
        void setPlayerBeast(const BeastForm* form);
        [[nodiscard]] bool playerBeast() const { return mBeast; }
        /// The player cast a power whose effect is of the werewolf archetype (36): this is called instead of a cast.
        void setBeastStartHook(std::function<void()> hook) { mBeastStart = std::move(hook); }
        /// Task 0241 (combat_abilities.cpp): the numbers the player's held abilities and blessings give (the sum over
        /// his spell list, ability_numbers.hpp). Everything is zero when he holds none or the perk rules are off.
        [[nodiscard]] AbilityNumbers playerAbilityNumbers() const;
        /// Moves whenever those numbers change (the pack gives the carry weight bonus to the inventory when it does).
        [[nodiscard]] std::uint64_t abilityRevision() const { return mAbilityRevision; }
        /// The player's resistance and weakness in percent against an effect whose MGEF resist value is `resistValue`.
        [[nodiscard]] float playerResistPercent(std::int32_t resistValue);
        [[nodiscard]] float playerWeaknessPercent(std::int32_t resistValue);
        /// An Invisibility effect (a stone's power) is running on the player: observers do not notice him.
        [[nodiscard]] bool playerInvisible() const;

        // ---- task 0248 (combat_race_effects.cpp): the effects the older tasks left without a rule
        // ----------------------
        /// Skill points and percent skill modifiers the player holds (abilities and blessings, and the diseases'), for
        /// the progression's effective skill values; `skillModRevision` moves when either changes.
        struct SkillModifiers
        {
            std::array<float, 18> mPoints{};
            std::array<float, 18> mPower{};
        };
        [[nodiscard]] SkillModifiers playerSkillModifiers() const;
        [[nodiscard]] std::uint64_t skillModRevision() const { return mSkillModRevision; }
        /// Carry weight from abilities, blessings and diseases (Steed +100, Black-Heart Blight -10).
        [[nodiscard]] float playerCarryBonus() const;
        /// The factor on a shout's recovery time (the Talos blessing); 1 with none.
        [[nodiscard]] float playerShoutRecoveryScale() const;
        /// Night Eye runs on the player (a status only: nothing draws a brighter view).
        [[nodiscard]] bool playerNightEye() const;
        /// Task 0257: Night Eye on screen. `setNightEyeView(true)` turns the view on; `playerImageSpace` then gives the
        /// state to draw now (the intro of the game's own image space modifier while Night Eye runs, its last key held,
        /// the outro when it ends); `active` false in `out` means no change. Reads the combat clock, so it moves with
        /// combat updates.
        void setNightEyeView(bool on) { mNightEyeView = on; }
        [[nodiscard]] bool playerImageSpace(openeden::sdk::game::PlayerImageSpaceState& out);
        [[nodiscard]] bool calmed(openeden::sdk::game::CombatSubject subject) const;
        [[nodiscard]] bool commanded(openeden::sdk::game::CombatSubject subject) const;
        /// Does the fighter hold this perk now (a timed one a running effect gave included)?
        [[nodiscard]] bool holdsPerk(openeden::sdk::game::CombatSubject subject, FormId perk) const;

        // ---- task 0240 (diseases; combat_disease.cpp) ---------------------------------------------------------------
        /// Told once for each landed, unblocked, non-bash blow of a non-player fighter on the player: the spells its
        /// race's attacks carry and its placement reference. Empty by default: nothing of the combat changes.
        using PlayerHitHook = std::function<void(const std::vector<FormId>& attackSpells, std::uint32_t attacker)>;
        void setPlayerHitHook(PlayerHitHook hook) { mPlayerHitHook = std::move(hook); }
        /// What long-lasting effects (a disease's) change in the player's numbers: the maxima and regeneration rates of
        /// stamina and magicka are moved by the amounts (and moved back when this changes), a weapon skill's damage is
        /// scaled by 1 + percent / 100 (never below 0). Applied once the player has a profile, again after every
        /// progression sync (which rewrites the maxima), and when it changes.
        void setPlayerValueModifiers(const PlayerValueModifiers& modifiers);
        [[nodiscard]] const PlayerValueModifiers& playerValueModifiers() const { return mValueMods; }
        /// The spells the player has now (his race's and extra abilities among them), a copy; empty before his profile.
        [[nodiscard]] std::vector<FormId> playerSpells() const;

        // ---- task 0228: the powers of the player's race (RACE SPLO spells of type power or lesser power)
        // ---------------
        /// What the magic runtime does with a power: its effects change the player's health, magicka or stamina or
        /// stagger or demoralise (Works); a modifier of a value the pack does not track is kept as a status for its
        /// duration and changes nothing else (StatusOnly); every other archetype does nothing (Unsupported). Effects
        /// with conditions the pack cannot evaluate count as not there (unless the test setting applies them). Spell
        /// areas are in feet: 21.33 units a foot (RECALLED-VERIFY: 64 units to 3 feet).
        static constexpr float kUnitsPerFoot = 64.f / 3.f;
        enum class PowerSupport : std::uint8_t
        {
            Works,
            StatusOnly,
            Unsupported
        };
        struct RacePower
        {
            FormId mId = kNullFormId;
            std::string mName; ///< the editor id (the pack reads no SPEL names)
            PowerSupport mSupport = PowerSupport::Unsupported;
        };
        /// The powers among `spells` (a race's SPLO list), in the list's order; empty until the combat data is read.
        [[nodiscard]] std::vector<RacePower> racePowers(const std::vector<FormId>& spells);
        /// Queues the cast of the power `spell` by the player: it goes through the same cast clip and effect rules as
        /// any spell, with no magicka cost taken beyond the record's own (0 for a power). False when the spell is not a
        /// known power, or one is already waiting. The once-a-day rule is the caller's.
        bool castPlayerPower(FormId spell);

        // ---- task 0234 (scrolls; combat_scroll.cpp) --------------------------------------------------------
        /// Queues the cast of the scroll `scroll` (a SpellRecord decoded from a SCRL, SpellRecord::mScroll) by the
        /// player: the same cast clip, delivery and effect rules as any spell, no magicka. False (nothing queued,
        /// so the caller uses nothing up) when the combat has never run, the id is not a scroll with effects, or
        /// another waits.
        bool castPlayerScroll(FormId scroll);
        /// Told once per accepted scroll: `cast` true when its cast began, false when it gave up waiting for the
        /// cast clip's timing (the caller gives the scroll back).
        void setScrollOutcome(std::function<void(FormId scroll, bool cast)> sink);

        // ---- task 0190 (stage S5-9): the combat AI (combat_ai.hpp, combat_ai_run.cpp)
        // -----------------------------------

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
                Fly, ///< task 0253: a dragon in the air: put it at (x, y, z) facing mHeading (a new pose loop of mClip
                     ///< when mStart)
                Land, ///< task 0253: take the dragon off its flight pose at (x, y, z)
            };
            Kind mKind = Kind::Stop;
            openeden::sdk::game::ActorHandle mActor;
            double mX = 0.0;
            double mY = 0.0;
            double mZ = 0.0;
            openeden::sdk::game::ActorSpeed mSpeed = openeden::sdk::game::ActorSpeed::Walk;
            float mHeading = 0.f; ///< Fly: radians as the engine stores it
            std::string_view mClip; ///< Fly: the pose clip (static text of dragons.hpp)
            bool mStart = false; ///< Fly: start a new loop of mClip now
            bool mDirect = false; ///< GoTo of a dragon: walk straight at (x, y) instead of asking the navigator
        };

        // ---- task 0253 (dragons, combat_dragon.cpp)
        /// Switches the dragons on (the pack setting `dragons`, with the flight numbers). Off: nothing of it runs.
        void setDragons(const dragons::Settings& settings) { mDragonSettings = settings; }
        [[nodiscard]] std::size_t dragonCount() const { return mDragonStates.size(); }
        /// The flight phase of a dragon (Ground for an actor that is not one).
        [[nodiscard]] dragons::Phase dragonPhase(openeden::sdk::game::ActorHandle handle) const;
        [[nodiscard]] std::uint32_t dragonBreaths() const;
        [[nodiscard]] std::uint32_t dragonSouls() const { return mDragonSouls; }
        /// Task 0280 (saves): the dragons that are in the air now (placement id and place), in ascending placement
        /// order, for the chunk `dragons`; dragons on the ground and dead ones are not in it.
        [[nodiscard]] std::vector<dragons::DragonSave> savedDragons() const;
        /// A load: dragons that were in the air at the save. Each is put back in the cruise when the actor of that
        /// placement is loaded and has its profile (it may be in a cell that is not loaded yet); it then follows the
        /// ordinary flight rules (not engaged, it lands near the player). At most kMaxDragonSaves are kept; the list
        /// replaces the one before.
        void restoreDragons(std::vector<dragons::DragonSave> saves);
        [[nodiscard]] std::size_t dragonRestoresPending() const { return mDragonRestores.size(); }

        /// Switches the AI on (the pack setting `combat ai`). Off, nothing of it runs and the old test rules are the
        /// whole of the hostile behaviour. On, `combat test hostile` names actors that engage at once, `power every`
        /// and `bash every` fix the attack kind and `combat test block` makes the named actors block always.
        void setCombatAi(bool on);
        /// Task 0197: the target is chosen from relations, followers, guards and sight (needs the AI); off, the AI's
        /// target is the player as before.
        void setCombatFactions(bool on);
        /// Who follows the player (the cell source's actor table). Without it nobody does.
        void setFollowerQuery(std::function<bool(openeden::sdk::game::WorldId reference)> query);
        /// Test rule: this actor (editor id, case-insensitive) attacks the nearest valid target on sight (frenzied).
        void setTestAttacker(std::string_view editorId);
        /// Seconds of combat clock before the factions rules look for targets (default 0; a scenario that arranges the
        /// street first sets it).
        void setFactionsDelay(float seconds) { mFactionsDelay = (seconds > 0.f && seconds < 1.0e6f) ? seconds : 0.f; }
        /// Task 0204 (crime): the loaded fighters as the crime rules see them (appended), one of them, and the guard
        /// moves. `crimeHold` suspends the actor's package and stops its goal, `crimeRelease` lets its package go on,
        /// `crimeAttack` makes it fight the player (the AI engages it as if it were provoked). All inert for an actor
        /// the combat rules do not know.
        void crimeActors(std::vector<crime::CrimeActor>& out);
        [[nodiscard]] bool crimeActor(openeden::sdk::game::ActorHandle handle, crime::CrimeActor& out);
        void crimeGoTo(openeden::sdk::game::ActorHandle handle, double x, double y, double z,
            openeden::sdk::game::ActorSpeed speed);
        void crimeHold(openeden::sdk::game::ActorHandle handle);
        void crimeRelease(openeden::sdk::game::ActorHandle handle);
        void crimeAttack(openeden::sdk::game::ActorHandle handle);
        /// Task 0299 (dungeon traps): a script's Weapon.Fire(source, ammo). The shot flies from `x y z` along
        /// `heading` and `pitch` (the engine's: heading 0 faces +Y), belongs to nobody and hurts the first actor or
        /// the player it meets by the weapon's and the ammunition's own damage after the victim's armour (no skill, no
        /// draw: a trap has neither). False when the weapon or ammunition is unknown, has no projectile, or the core
        /// refuses the shot. `trapShotsInFlight` counts the shots whose end has not come.
        struct TrapShot
        {
            FormId mWeapon = 0;
            FormId mAmmo = 0;
            double x = 0.0, y = 0.0, z = 0.0;
            float heading = 0.f;
            float pitch = 0.f;
        };
        bool fireTrapShot(openeden::sdk::game::CombatRequestWriter& writer, const TrapShot& shot);
        [[nodiscard]] std::size_t trapShotsInFlight() const { return mTrapShots.size(); }
        /// Task 0264 (companions): what the game's own scripts ask of the combat. `scriptStartCombat` makes the actor
        /// fight the player (Actor.StartCombat), `scriptStopCombat` ends its fight (Actor.StopCombat) and
        /// `scriptResetHealth` heals it to the maximum (ResetHealthAndLimbs) and ends a bleedout.
        /// `setEssentialBleedout(true)` makes an ESSENTIAL actor (ACBS) that a blow would kill collapse into bleedout
        /// instead (health 1, no death report) and tell the sink once; the sink's script event is OnEnterBleedout. All
        /// inert for an actor the combat does not know.
        bool scriptStartCombat(openeden::sdk::game::ActorHandle handle);
        bool scriptStopCombat(openeden::sdk::game::ActorHandle handle);
        bool scriptResetHealth(openeden::sdk::game::ActorHandle handle);
        [[nodiscard]] bool scriptInCombat(openeden::sdk::game::ActorHandle handle) const;
        void setEssentialBleedout(bool on) { mEssentialBleedout = on; }
        /// Task 0278 (kill moves). When the player's blow would kill a person the hook is asked (once per such blow);
        /// true means "play a kill move": the blow leaves the victim at 1 health, the victim and the player take no
        /// damage and the victim does nothing until `killMoveFinish`, which kills it at the next update. With no hook
        /// nothing changes.
        struct KillMoveQuery
        {
            openeden::sdk::game::ActorHandle mVictim;
            std::uint8_t mAnimType = 0; ///< the weapon's animation type
            bool mCreature = false;
            bool mEssential = false;
        };
        using KillMoveHook = std::function<bool(const KillMoveQuery&)>;
        void setKillMoveHook(KillMoveHook hook) { mKillMoveHook = std::move(hook); }
        void killMoveFinish(openeden::sdk::game::ActorHandle victim);
        [[nodiscard]] bool killMoveActive() const { return mKillMoveActive; }
        using BleedoutSink = std::function<void(openeden::sdk::game::ActorHandle actor)>;
        void setBleedoutSink(BleedoutSink sink) { mBleedoutSink = std::move(sink); }
        /// Test aid (task 0204): this actor (editor id, case-insensitive) counts as a guard for the crime rules.
        void setCrimeTestGuard(std::string_view editorId);
        /// The target of an engaged AI actor (the player when it is not engaged).
        [[nodiscard]] openeden::sdk::game::CombatSubject aiTargetOf(openeden::sdk::game::ActorHandle handle) const;
        [[nodiscard]] std::size_t aiEngagedCount() const { return mAiActors.size(); }
        /// Task 0209 (music): is an engaged AI fighter that has not given up fighting the player?
        [[nodiscard]] bool playerEngagedByAi() const;
        [[nodiscard]] bool combatAi() const { return mAiOn; }
        /// Appends the movement the AI wants since the last drain, in order, and empties its queue.
        void drainAiMoves(std::vector<AiMoveIntent>& out);
        /// The result of a goal the AI asked for (forwarded from ActorRules::onGoalFinished).
        void aiGoalFinished(openeden::sdk::game::ActorHandle handle, openeden::sdk::game::GoalResult result);
        [[nodiscard]] AiMode aiModeOf(openeden::sdk::game::ActorHandle handle) const;
        /// A damage report as it went to the core: damage the player dealt to a living actor provokes it, whatever the
        /// means (a blow, an arrow, a spell; the neutral world's wrapper tells every report, AiDamageWriter below).
        void aiNoteDamage(const openeden::sdk::game::DamageReport& report);
        /// Task 0231 (barks): told of the fight's moments (an engagement, a flight, the end of a fight, a hurt actor, a
        /// death). Hook calls only; empty (the default) and nothing is looked at.
        /// Task 0292 (the College of Winterhold's tests): what a spell did, for the Papyrus host's OnHit,
        /// OnMagicEffectApply and OnSpellCast events. `mCaster` is who cast it; `mTarget` is who it reached (`mHasTarget`
        /// false for a hit on the world, where `mX`, `mY`, `mZ` is where it ended). Cast: a non-held spell went off.
        /// Hit: a shot ended on something. EffectApplied: one effect of the spell reached a target (its conditions
        /// held), the caster itself for a self spell.
        struct SpellEvent
        {
            enum class Kind : std::uint8_t
            {
                Cast,
                Hit,
                EffectApplied,
            };
            Kind mKind = Kind::Cast;
            openeden::sdk::game::CombatSubject mCaster;
            openeden::sdk::game::CombatSubject mTarget;
            bool mHasTarget = false;
            FormId mSpell = kNullFormId;
            FormId mEffect = kNullFormId;
            double mX = 0.0;
            double mY = 0.0;
            double mZ = 0.0;
        };
        using SpellEventSink = std::function<void(const SpellEvent&)>;
        void setSpellEventSink(SpellEventSink sink) { mSpellEventSink = std::move(sink); }
        using BarkSink = std::function<void(barks::Kind, openeden::sdk::game::ActorHandle)>;
        void setBarkSink(BarkSink sink) { mBarkSink = std::move(sink); }
        /// A damage report as it went to the core: a living actor that lost health says a hurt line.
        void barkNoteDamage(const openeden::sdk::game::DamageReport& report)
        {
            if (mBarkSink && !report.victimIsPlayer && report.amount > 0.f && report.healthAfter > 0.f)
                mBarkSink(barks::Kind::Hit, report.victim);
        }
        void barkNoteDeath(openeden::sdk::game::ActorHandle actor)
        {
            if (mBarkSink)
                mBarkSink(barks::Kind::Death, actor);
        }
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
            std::uint8_t mWeaponClass = 0; ///< task 0214: the clip class the fighter swings with (0: unknown)
            bool mHasShield = false;
            std::uint16_t mLevel = 1; ///< task 0244: the level of its profile (read only)
            /// Task 0244: fighting now (a blow given or taken lately, or an engaged AI actor or test hostile).
            bool mEngaged = false;
            bool mUnique = false; ///< task 0254: ACBS Unique
            bool mEssential = false; ///< task 0254: ACBS Essential
        };
        [[nodiscard]] bool fighterView(openeden::sdk::game::CombatSubject subject, FighterView& out) const;
        /// Task 0254: the same for one actor, reading its numbers now if they were not (an idle fighter is otherwise
        /// only read when something happens to it). False while the records are still being read or for an unknown
        /// actor.
        bool readView(openeden::sdk::game::ActorHandle handle, FighterView& out);
        /// Task 0244 (read only): calls `visit` for every actor fighter that is ready (the player is left out). The
        /// visitor must not call back into this object.
        using FighterVisitor = std::function<void(const openeden::sdk::game::CombatSubject&, const FighterView&)>;
        void forEachFighter(const FighterVisitor& visit) const;

        // ---- task 0210 (waiting and sleeping)
        /// Queues a rest of `seconds` real seconds of regeneration (what the game hours stood for): at the next update
        /// every living fighter gets health, magicka and stamina back by its own regeneration rates, out of combat. The
        /// player's health goes through a report that takes none off, as a level up's does. A number that is not
        /// finite or not above 0 queues nothing; the sum of queued seconds is capped at kMaxRestSeconds.
        void requestRest(float seconds);
        /// The player fought a moment ago (the combat linger time of a blow given or taken).
        [[nodiscard]] bool playerInCombat() const;
        /// A living hostile is engaged with the player (the combat AI's engaged actors and the test hostiles).
        [[nodiscard]] bool enemiesNear() const;
        /// Task 0282 (Actor.IsInCombat of a script): is this subject fighting? The player: he fought a moment ago or a
        /// living hostile is engaged with him. An actor: the combat AI has it engaged (scriptInCombat).
        [[nodiscard]] bool scriptInCombatOf(const openeden::sdk::game::CombatSubject& subject) const;
        [[nodiscard]] std::size_t restsApplied() const { return mRestsApplied; }
        /// The combat records are read (true too when there is no data at all: nothing is then waited for).
        [[nodiscard]] bool dataReady() const { return mData == nullptr || mData->combatReady(); }
        static constexpr float kMaxRestSeconds = 24.f * 3600.f;

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
        /// Task 0222: how many swings the PLAYER began (a count that only grows) and the clip of the last one, for the
        /// core to draw the swing on the player's body. The pack's own hidden clock still decides the hit.
        [[nodiscard]] std::uint32_t playerSwingCount() const { return mPlayerSwingCount; }
        [[nodiscard]] const std::string& playerSwingClip() const { return mPlayerSwingClip; }
        /// Task 0245: the player's casts, like his swings (the first-person arms play the clip).
        [[nodiscard]] std::uint32_t playerCastCount() const { return mPlayerCastCount; }
        [[nodiscard]] const std::string& playerCastClip() const { return mPlayerCastClip; }
        /// Task 0279: the beast's own body clips (the Howl): a count that grows by one for each and the newest clip
        /// (a bare clip name of the beast's folder). 0 and empty: none.
        [[nodiscard]] std::uint32_t playerBeastBodyCount() const { return mBeastBodyCount; }
        [[nodiscard]] const std::string& playerBeastBodyClip() const { return mBeastBodyClip; }
        /// Task 0249: the player's bow and spell hold for the first-person hands (a snapshot taken at the end of
        /// each combat update; callable from the main thread while the combat update runs).
        [[nodiscard]] PlayerHands playerHands() const;
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
            bool mBleedout = false; ///< task 0264: an essential actor that collapsed instead of dying
            bool mDead = false;
            bool mSwinging = false;
            std::uint32_t mSwingToken = 0;
            float mCombatUntil = -1.f;
            float mNextAttack = 0.f;
            bool mStaggered = false; ///< task 0172: a stagger clip plays (mStaggerToken)
            std::uint32_t mStaggerToken = 0;
            std::uint32_t mAttackCount = 0; ///< attacks begun by the hostile rule, for the every-N choice
            bool mAiScanned = false; ///< task 0190: looked at once for hostility on sight
            /// Task 0197: who its arrows and spells are aimed at (the player unless the AI chose another target).
            openeden::sdk::game::CombatSubject mAimTarget = openeden::sdk::game::CombatSubject::ofPlayer();
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
            ActorStatus mStatus; ///< task 0233: paralysis, frenzy and fear
            std::vector<std::pair<std::uint32_t, float>> mPerkValues; ///< task 0179: base actor values for conditions
            std::vector<FormId> mWornKeywords; ///< task 0179
            FighterGear mGear; ///< task 0214: filled with `combat real gear`
            std::vector<FormId> mTimedPerks; ///< task 0248: perks a running Script effect holds (Berserk)
            AbilityNumbers mAbilities; ///< task 0241: the player's held abilities and blessings (player only)
            bool mHasAbilities = false; ///< mAbilities holds something
            const WeaponRecord* mBoundWeapon = nullptr; ///< task 0295: a Bound Weapon effect's weapon
            float mBoundUntil = -1.f; ///< the combat clock at which it goes
        };
        [[nodiscard]] bool isEngagedNow(const Fighter& f) const; // task 0244
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
        // Task 0238 (combat_regen.cpp): the regeneration rate effects (actor values 27 to 29 and 155 to 157).
        [[nodiscard]] bool anyRegenEffect() const;
        [[nodiscard]] float regenRateFor(const Fighter& fighter, int vital, float baseRate, bool anyRunning) const;
        void addPlayerHealthRegen(float bonusUnits);
        void traceRegenRates(bool anyRunning);
        float mNextRegenTrace = 0.f;
        void flushPlayerHealthRegen(openeden::sdk::game::CombatRequestWriter& writer);
        float mPlayerHealthBonus = 0.f; ///< health earned by rate effects on the player, not yet sent to the engine
        void applyRest(openeden::sdk::game::CombatRequestWriter& writer); // task 0210
        float mRestSeconds = 0.f; ///< queued, applied by the next combatUpdate
        std::size_t mRestsApplied = 0;
        void beginAttack(openeden::sdk::game::CombatRequestWriter& writer, Fighter& fighter, AttackKind kind);
        void loadSettings();
        // Task 0179: perks. All of it does nothing for a fighter with no perks, no armour perk and no ability.
        void resolveTestPerks();
        void preparePerks(Fighter& fighter);
        [[nodiscard]] bool perksMatter(const Fighter& fighter) const;
        [[nodiscard]] PerkParticipant actorParticipant(const Fighter& fighter, bool power) const;
        [[nodiscard]] PerkParticipant weaponParticipant(const WeaponRecord* weapon) const;
        // Task 0214: real gear. All of it answers what the parent answered while mRealGear is false.
        [[nodiscard]] const WeaponRecord* perkWeaponFor(const Fighter& fighter) const;
        [[nodiscard]] std::uint8_t clipClassOf(const Fighter& fighter) const;
        [[nodiscard]] bool hasShield(const Fighter& fighter) const;
        [[nodiscard]] std::string_view meleeClip(
            openeden::sdk::game::ClipSlot slot, const Fighter& fighter, bool shieldSlot) const;
        [[nodiscard]] float shieldBlockBonus(const Fighter& fighter) const;
        void applyRealGear(Fighter& fighter);
        void syncPlayerTimingClass(const Fighter& player);
        void applyBeastToProfile(Fighter& player); ///< task 0259 (combat_werewolf.cpp)
        void applyPerksToHit(Fighter& attacker, Fighter& victim, bool power, MeleeHit& hit);
        /// Task 0227 (combat_sneak.cpp): the sneak attack multiplier of the player's blow or shot on `victim` (1:
        /// none).
        float sneakAttackFor(Fighter& attacker, Fighter& victim, const WeaponRecord* swung, bool ranged);
        [[nodiscard]] float perkMitigation(const Fighter& victim, bool& changed);
        [[nodiscard]] bool bowReady();
        // Task 0186 (stage S5-8): the magic runtime (combat_magic.cpp).
        struct Cast;
        struct Concentration;
        void updateMagic(openeden::sdk::game::CombatRequestWriter& writer, float step);
        void resolveTestCasts();
        void startTestCasts(openeden::sdk::game::CombatRequestWriter& writer);
        void runPlayerCast(openeden::sdk::game::CombatRequestWriter& writer);
        void runPlayerPower(openeden::sdk::game::CombatRequestWriter& writer);
        void runPlayerScroll(openeden::sdk::game::CombatRequestWriter& writer); // task 0234
        void applyPowerEffects(
            openeden::sdk::game::CombatRequestWriter& writer, Fighter& caster, const SpellRecord& spell);
        // Task 0205: the player's shout (combat_shout.cpp).
        void runPlayerShout(openeden::sdk::game::CombatRequestWriter& writer, float step);
        bool beginShoutCast(openeden::sdk::game::CombatRequestWriter& writer, Fighter& caster, const SpellRecord& spell,
            float recovery);
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
        /// Task 0236 (combat_effect_conditions.cpp): may this effect apply from `caster` to `target`? True with no
        /// conditions or with the test setting; else the effect's and its record's conditions are evaluated over the
        /// two actors (magic_conditions.hpp) and a list that cannot be (a damaged one, an unanswered function) says no.
        /// `unlistedPasses` (task 0238, for race powers): an effect whose conditions were counted but never decoded (a
        /// hand-built record: every real one is listed) is taken as met instead of skipped, as task 0228 did for all.
        [[nodiscard]] bool effectConditionsAllow(const Fighter& caster, const Fighter& target,
            const SpellEffect& effect, const MagicEffectRecord& record, bool unlistedPasses = false);
        [[nodiscard]] effectconditions::Participant effectParticipant(
            const Fighter& fighter, std::vector<FormId>& active, std::vector<FormId>& worn) const;
        void notifyPlayerHit(const Fighter& attacker, const Fighter& victim); // task 0240 (combat_disease.cpp)
        void applyPlayerValueModifiers(); // task 0240
        void applySkillPercent(const Fighter& attacker, const WeaponRecord* weapon, MeleeHit& hit) const; // task 0240
        /// Task 0295: the weapon's own enchantment on a landed blow (applyPoisonOnHit's twin).
        void applyEnchantOnHit(openeden::sdk::game::CombatRequestWriter& writer, Fighter& attacker, Fighter& victim,
            const WeaponRecord* weapon, const openeden::sdk::game::SweepHit& hit);
        /// Task 0295: the enchantment spell of a staff (null when it is not a staff or carries none).
        [[nodiscard]] const SpellRecord* staffSpellOf(const WeaponRecord* weapon);
        void applyPoisonOnHit(openeden::sdk::game::CombatRequestWriter& writer, Fighter& attacker, Fighter& victim,
            const openeden::sdk::game::SweepHit& hit); // task 0229 (combat_poison.cpp)
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
        // Task 0233 (combat_status.cpp): paralysis, frenzy, fear and weakness. Each hook call from the older files is
        // one of these; with no such effect running none of them does anything.
        [[nodiscard]] bool isParalyzed(const Fighter& fighter) const;
        [[nodiscard]] bool isFeared(const Fighter& fighter) const;
        /// The weakness in percent that the effects running on `target` give to a hostile effect of resistance
        /// `resistValue` (its own resistance and magic, summed, at most kMaxWeaknessPercent).
        [[nodiscard]] float weaknessOf(const Fighter& target, std::int32_t resistValue) const;
        /// Applies one effect of archetype Paralysis, Frenzy or Demoralize's flight. True when it handled it.
        bool applyStatusEffect(openeden::sdk::game::CombatRequestWriter& writer, Fighter& caster, Fighter& target,
            const SpellEffect& effect, const MagicEffectRecord& record);
        void runStatuses(openeden::sdk::game::CombatRequestWriter& writer);
        void statusNoteLost(Fighter& fighter);
        void endParalysis(openeden::sdk::game::CombatRequestWriter& writer, Fighter& fighter);
        void holdActor(openeden::sdk::game::ActorHandle handle);
        void frenzyRetarget(openeden::sdk::game::CombatRequestWriter& writer, Fighter& fighter);
        std::size_t mStatusesApplied = 0;
        [[nodiscard]] bool launchSpell(openeden::sdk::game::CombatRequestWriter& writer, Fighter& caster,
            const SpellRecord& spell, const openeden::sdk::game::SubjectState* target, float stickSeconds = 0.f);
        // Task 0242 (combat_delivery.cpp): target actor, target location and area delivery.
        struct Rune;
        void targetActorCast(
            openeden::sdk::game::CombatRequestWriter& writer, Fighter& caster, const SpellRecord& spell);
        /// An aimed or location spell's projectile ended at (x, y, z): an area spell explodes there, a rune is placed.
        /// True when it handled the end (the caller does nothing more).
        [[nodiscard]] bool deliverAtEnd(openeden::sdk::game::CombatRequestWriter& writer,
            const openeden::sdk::game::ProjectileResult& result, Fighter& caster, const SpellRecord& spell);
        /// Applies the spell's effects around (x, y, z): an effect with a radius (its area in feet, or the explosion
        /// its projectile names) reaches every living actor inside it but the caster; `direct` (the actor the shot hit,
        /// null for none) gets every effect. No falloff: the data has none.
        void explodeAt(openeden::sdk::game::CombatRequestWriter& writer, Fighter& caster, const SpellRecord& spell,
            double x, double y, double z, const openeden::sdk::game::SweepHit* direct);
        void placeRune(openeden::sdk::game::CombatRequestWriter& writer, Fighter& caster, const SpellRecord& spell,
            double x, double y, double z, std::uint32_t token);
        void tickRunes(openeden::sdk::game::CombatRequestWriter& writer);
        [[nodiscard]] bool runeTriggeredBy(const Fighter& caster, const Fighter& other) const;
        /// The radius of one effect in game units: its area (feet) or, with none, the radius of the explosion its
        /// projectile names; 0 for neither.
        [[nodiscard]] float effectRadius(const SpellRecord& spell, const SpellEffect& effect) const;
        [[nodiscard]] bool hasAreaEffect(const SpellRecord& spell) const;
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
            bool mFleeBarked = false; ///< task 0231: the flight's cry was told to the bark sink
            int mGoalFails = 0; ///< goals in a row that ended NoPath or Blocked outside a flee
            bool mGiveUp = false; ///< too many failed goals: it lets go of the fight
            /// Task 0197: who it fights (the player until the factions rules pick another).
            openeden::sdk::game::CombatSubject mTarget = openeden::sdk::game::CombatSubject::ofPlayer();
            bool mAssisted = false; ///< it joined a fight of another (it does not call for help itself)
            bool mByStatus = false; ///< task 0233: a fear or a frenzy engaged it (it is let go when that ends)
        };
        /// Task 0197: where a ready fighter stood at the last snapshot (every Factions::kSnapshotPeriod).
        struct AiSnap
        {
            openeden::sdk::game::CombatSubject mSubject;
            double mX = 0.0;
            double mY = 0.0;
            double mZ = 0.0;
        };
        struct AiWatcher
        {
            openeden::sdk::game::ActorHandle mHandle;
            float mNextSight = 0.f;
        };
        void runAi(openeden::sdk::game::CombatRequestWriter& writer);
        void aiScan();
        bool aiBegin(Fighter& fighter, const char* reason,
            openeden::sdk::game::CombatSubject target = openeden::sdk::game::CombatSubject::ofPlayer(),
            bool assisted = false);
        void aiForget(openeden::sdk::game::ActorHandle handle);
        void aiPickSpell(Fighter& fighter, AiActor& actor);
        void aiRelease(std::size_t index, const char* why);
        void aiProvoke(const Fighter& attacker, Fighter& victim);
        void aiRaiseGuard(openeden::sdk::game::CombatRequestWriter& writer, AiActor& actor, Fighter& fighter);
        void aiMove(openeden::sdk::game::CombatRequestWriter& writer, AiActor& actor, Fighter& fighter, AiMove move,
            const openeden::sdk::game::SubjectState& self, const openeden::sdk::game::SubjectState& target,
            float distance, float reach);
        void aiPushGoTo(AiActor& actor, double x, double y, double z, openeden::sdk::game::ActorSpeed speed);
        [[nodiscard]] bool aiGuardUp(openeden::sdk::game::ActorHandle handle) const;
        [[nodiscard]] AiActor* aiFind(openeden::sdk::game::ActorHandle handle);
        [[nodiscard]] const AiActor* aiFind(openeden::sdk::game::ActorHandle handle) const;
        // Task 0197 (combat_ai_run.cpp, combat_factions.*)
        bool mFactionsOn = false;
        float mFactionsDelay = 0.f;
        float mSightTraceAt[64] = {}; ///< the combat clock of the last 'no sight' trace line, per reference % 64
        std::function<bool(openeden::sdk::game::WorldId)> mFollowerQuery;
        std::string mTestAttacker; ///< lower case
        std::string mCrimeTestGuard; ///< task 0204, lower case
        /// Task 0204: actors the player provoked from calm in update N (so the crime rules still see them as calm).
        std::vector<std::pair<openeden::sdk::game::ActorHandle, std::size_t>> mFreshProvoked;
        [[nodiscard]] bool freshlyProvoked(openeden::sdk::game::ActorHandle handle) const;
        std::vector<AiSnap> mAiSnap;
        float mAiSnapAt = -1.0e9f;
        std::size_t mWatchCursor = 0;
        mutable std::map<std::uint64_t, std::uint8_t> mRelCache; ///< (base a, base b) -> ActorRelation; lookups only
        [[nodiscard]] Factions::Party partyOf(const Fighter& fighter) const;
        [[nodiscard]] openeden::sdk::game::ActorRelation relationOf(const Fighter& a, const Fighter& b) const;
        [[nodiscard]] bool factions() const { return mAiOn && mFactionsOn; }
        [[nodiscard]] bool isTestAttacker(openeden::sdk::game::ActorHandle handle) const;
        std::vector<openeden::sdk::game::ActorHandle> mAiHelpFor; ///< fighters that engaged: allies may join
        std::vector<std::pair<float, std::size_t>> mAiScratch; ///< candidate (distance, snapshot index)
        void aiSnapshot(openeden::sdk::game::CombatRequestWriter& writer);
        void aiLookForTarget(openeden::sdk::game::CombatRequestWriter& writer, AiWatcher& watcher);
        [[nodiscard]] bool aiHasSight(openeden::sdk::game::CombatRequestWriter& writer,
            const openeden::sdk::game::CombatSubject& from, const AiSnap& to);
        void aiCallForHelp(openeden::sdk::game::CombatRequestWriter& writer, openeden::sdk::game::ActorHandle engaged);
        [[nodiscard]] bool aiTargetsPlayerSide(const Fighter& fighter) const;
        bool mAiOn = false;
        SlotTable mGuardSlots;
        std::vector<AiActor> mAiActors;
        std::vector<AiWatcher> mAiWatchers;
        std::size_t mAiScanCursor = 0;
        std::map<openeden::sdk::game::WorldId, std::uint32_t> mAiEngagements;
        std::map<openeden::sdk::game::WorldId, float> mAiCooldown; ///< the combat clock before which it does not engage
        std::vector<AiMoveIntent> mAiMoves;
        BarkSink mBarkSink; ///< task 0231
        SpellEventSink mSpellEventSink; ///< task 0292
        void emitCastEvent(openeden::sdk::game::CombatSubject caster, const SpellRecord& spell) const
        {
            if (!mSpellEventSink)
                return;
            SpellEvent event;
            event.mKind = SpellEvent::Kind::Cast;
            event.mCaster = caster;
            event.mSpell = spell.mCommon.mId;
            mSpellEventSink(event);
        }
        void emitSpellEvent(const SpellEvent& event) const
        {
            if (mSpellEventSink)
                mSpellEventSink(event);
        }
        BleedoutSink mBleedoutSink; ///< task 0264
        bool mEssentialBleedout = false; ///< task 0264
        KillMoveHook mKillMoveHook; ///< task 0278
        bool mKillMoveActive = false; ///< the player and `mKillMoveVictim` take no damage
        openeden::sdk::game::ActorHandle mKillMoveVictim;
        bool mKillMoveFinishPending = false;
        void tickKillMove(openeden::sdk::game::CombatRequestWriter& writer);
        // Task 0253 (dragons).
        dragons::Settings mDragonSettings;
        std::vector<dragons::DragonState> mDragonStates;
        std::vector<dragons::DragonSave> mDragonRestores; ///< task 0280: waiting for their actor
        std::uint32_t mDragonSouls = 0;
        void updateDragons(openeden::sdk::game::CombatRequestWriter& writer, float step);
        [[nodiscard]] bool dragonHolds(const openeden::sdk::game::CombatSubject& subject) const;
        [[nodiscard]] bool dragonWalksDirect(openeden::sdk::game::ActorHandle handle) const;
        /// Task 0280: the navigator found no path for this dragon: it walks straight for kDirectFallbackSeconds.
        void dragonNoPath(openeden::sdk::game::ActorHandle handle);

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
        SlotTable mWolfSlots; ///< task 0212
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
        bool mRealGear = false; ///< task 0214
        bool mWeaponMagic = false; ///< task 0295
        SlotTable mGearSlots; ///< task 0214
        // Task 0184
        PlayerEquipment mEquip;
        bool mEquipDirty = false;
        float mPendingHeal = 0.f;
        float mBaseMitigation = 0.f; ///< the stand-in's, kept so that unequipping restores it
        bool mBaseMitigationKnown = false;
        std::vector<ArmourPiece> mBasePieces;
        void applyPlayerEquipment();
        [[nodiscard]] const WeaponRecord* weaponFor(const Fighter& fighter) const;
        /// Task 0212: the clip of a slot for this fighter: a creature's own, else the humanoid one-handed table.
        [[nodiscard]] std::string_view clipOf(const Fighter& fighter, openeden::sdk::game::ClipSlot slot) const;
        void applyPlayerHeal(openeden::sdk::game::CombatRequestWriter& writer);
        // Task 0200
        SkillUseHook mSkillHook;
        SneakMultiplierHook mSneakMultiplier; ///< task 0227
        SneakDoneHook mSneakDone;
        const progression::PlayerProgression* mProgression = nullptr;
        std::uint64_t mProgressionSynced = 0;
        bool mHaveStandProfile = false;
        FighterProfile mStandProfile; ///< the player's profile as ready() made it, before the progression
        void syncProgression(openeden::sdk::game::CombatRequestWriter& writer);
        void reportSkillUse(std::size_t slot, float amount);
        void reportSpellUse(const Fighter& caster, const SpellRecord& spell, float magicka);
        void reportHitSkills(const Fighter& attacker, const Fighter& victim, const WeaponRecord* weapon, float blow,
            float taken, bool blocked);
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
        std::uint32_t mPlayerSwingCount = 0; ///< task 0222
        std::string mPlayerSwingClip; ///< task 0222
        bool mBeast = false; ///< task 0259
        BeastForm mBeastForm; ///< task 0259
        FighterProfile mBeastSaved; ///< the human profile put back by setPlayerBeast(nullptr)
        std::function<void()> mBeastStart; ///< task 0259
        std::uint32_t mPlayerCastCount = 0; ///< task 0245
        std::string mPlayerCastClip; ///< task 0245
        std::uint32_t mBeastBodyCount = 0; ///< task 0279
        std::string mBeastBodyClip; ///< task 0279
        // Reserved for the usual cell; a cell with more actors makes onActorAdded grow it (an allocation per added
        // actor beyond that, never one per frame). kMaxCombatFighters bounds it.
        std::vector<Fighter> mFighters; ///< index 0 is the player
        std::vector<PendingHit> mPendingHits; ///< Hit events seen since the last combatUpdate
        std::vector<PendingBow> mPendingBow; ///< task 0178
        std::vector<Shot> mShots; ///< task 0178
        /// Task 0299: a trap's dart in flight: the token and what flies (the damage is worked out when it lands).
        struct TrapFlight
        {
            std::uint32_t mToken = 0;
            FormId mWeapon = 0;
            FormId mAmmo = 0;
        };
        std::vector<TrapFlight> mTrapShots;
        [[nodiscard]] bool resolveTrapShot(
            openeden::sdk::game::CombatRequestWriter& writer, const openeden::sdk::game::ProjectileResult& result);
        void applyTrapDamage(openeden::sdk::game::CombatRequestWriter& writer, Fighter& victim, float damage, double x,
            double y, double z);
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
            bool mShout = false; ///< task 0205: a shout, which costs no magicka and starts the cooldown when it goes
            float mRecovery = 0.f; ///< a shout's cooldown seconds
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
        struct Rune
        {
            openeden::sdk::game::CombatSubject mCaster;
            const SpellRecord* mSpell = nullptr;
            double mX = 0.0;
            double mY = 0.0;
            double mZ = 0.0;
            float mTrigger = kDefaultRuneProximity; ///< game units
            float mRadius = 0.f; ///< the explosion's, game units
            float mExpiresAt = 0.f; ///< combat clock
            std::uint32_t mToken = 0; ///< task 0262: the projectile that placed it (its glyph stays drawn)
        };
        std::vector<Rune> mRunes; ///< task 0242
        bool mSpellDelivery = false;
        // Task 0250 (combat_summons.cpp). Every entry point does nothing while mSummonsOn is false.
        bool mSummonsOn = false;
        bool mNightEyeView = false; ///< task 0257
        imagespace::NightEyeTracker mNightEyeTracker; ///< task 0257
        bool mNightEyeRecordsTried = false; ///< task 0257: the IMADs were looked up (once the data is ready)
        std::size_t mMaxSummons = kDefaultMaxSummons;
        std::uint32_t mSummonCount = 0; ///< summons made: the pick of a leveled list
        std::string mTestKill; ///< lower case; cleared once the actor was killed
        float mTestPlayerMagicka = 0.f;
        SummonHooks mSummonHooks;
        std::function<void(openeden::sdk::game::WorldId)> mCorpseRaised; // task 0254
        SummonLedger mSummons;
        bool applySummonEffect(openeden::sdk::game::CombatRequestWriter& writer, Fighter& caster,
            const SpellRecord& spell, const SpellEffect& effect, const MagicEffectRecord& record);
        bool reanimateInSights(
            openeden::sdk::game::CombatRequestWriter& writer, Fighter& caster, const SpellRecord& spell);
        void tickSummons(openeden::sdk::game::CombatRequestWriter& writer);
        void summonArrived(const openeden::sdk::game::ActorInfo& info);
        void summonGone(openeden::sdk::game::ActorHandle handle);
        void dismissSummon(
            openeden::sdk::game::CombatRequestWriter* writer, openeden::sdk::game::WorldId reference, const char* why);
        [[nodiscard]] const Fighter* summonMasterFighter(const Fighter& fighter) const;
        [[nodiscard]] bool spellReanimates(const SpellRecord& spell) const;
        [[nodiscard]] bool spellSummons(const SpellRecord& spell) const; ///< a Summon Creature effect, summons on
        std::size_t mRunesSetOff = 0;
        std::size_t mExplosionsDone = 0;
        std::vector<TestCaster> mTestCasters;
        std::vector<std::pair<openeden::sdk::game::ActorHandle, std::string>> mActorNames; ///< lower case editor ids
        bool mPlayerCastWanted = false; ///< a press waits for the cast clip's timing
        float mPlayerCastWantedAt = 0.f;
        // Task 0205: the player's shout.
        shouts::ShoutState* mShouts = nullptr; ///< not owned
        std::string mTestPlayerShout;
        float mShoutEvery = 0.f;
        float mShoutHold = 1.f;
        float mNextShout = 0.f; ///< the combat clock of the next automatic press
        bool mShoutHeld = false; ///< the control is down
        bool mShoutWanted = false; ///< a shout was chosen and its cast has not begun (the clip's timing is awaited)
        float mShoutHeldFor = 0.f;
        float mShoutAutoReleaseAt = -1.f;
        std::size_t mShoutWordsTold = 0; ///< words already traced during this hold
        std::size_t mShoutWordsChosen = 0;
        float mShoutWantedAt = 0.f;
        std::vector<bool> mShoutEdges;
        bool mTestShoutLooked = false;
        std::size_t mShoutAvail = 0; ///< the words of the equipped shout that were unlocked at the press
        bool mShoutAutoFired = false; ///< the last word was reached while held and went: the release does nothing
        shouts::Settings mShoutSettings;
        bool mHaveShoutSettings = false;
        std::string mShoutTraceName; ///< the shout being spoken, for the trace
        std::size_t mShoutsSpoken = 0;
        bool mPlayerCastHeld = false; ///< the attack control is down (a real key or an automatic press)
        std::vector<bool> mCastEdges; ///< attack-control edges for the spell (true: pressed)
        FormId mPendingPower = kNullFormId; ///< task 0228: a power waiting for its cast clip's timing
        float mPendingPowerAt = 0.f;
        FormId mPendingScroll = kNullFormId; ///< task 0234: a scroll waiting for its cast clip's timing
        float mPendingScrollAt = 0.f;
        std::function<void(FormId, bool)> mScrollOutcome;
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
        std::function<bool(effectconditions::Place&)> mPlaceSource; ///< task 0238
        effectconditions::Evaluator::GlobalLookup mGlobalSource; ///< task 0238
        std::unique_ptr<effectconditions::Evaluator> mEffectConditions; ///< task 0236, made on first use
        std::vector<FormId> mActiveOfTarget; ///< task 0236: scratch, the effects running on the target
        std::vector<FormId> mActiveOfCaster;
        std::vector<FormId>
            mWornOfTarget; ///< task 0238: scratch, the keywords of the worn pieces (WornApparelHasKeywordCount)
        std::vector<FormId> mWornOfCaster;
        std::vector<FormId> mWornOfPlayer;
        std::uint64_t mEffectConditionChecks = 0; ///< task 0236: lists evaluated (the seed of GetRandomPercent)
        std::uint64_t mEffectConditionsPassed = 0;
        std::uint64_t mEffectConditionsFailed = 0;
        std::uint64_t mEffectConditionsUnanswered = 0;
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
        std::vector<FormId> mRaceAbilities; ///< task 0225
        std::vector<FormId> mStandRaceAbilities; ///< task 0225
        bool mRaceAbilitiesSet = false; ///< task 0225
        std::vector<FormId> mExtraAbilities; ///< task 0237
        PlayerHitHook mPlayerHitHook; ///< task 0240
        PlayerValueModifiers mValueMods; ///< task 0240: what was asked
        PlayerValueModifiers mValueModsApplied; ///< what the profile carries now (0 after a progression rewrite)
        bool mValueModsDirty = false;
        std::vector<FormId> mAppliedExtra; ///< task 0237: what the profile's spell list holds of them now
        // Task 0241 (combat_abilities.cpp)
        void applyAbilityNumbers(Fighter& fighter, const PerkLibrary& library);
        void flushAbilityVitals(openeden::sdk::game::CombatRequestWriter& writer);
        // Task 0248 (combat_race_effects.cpp, combat_abilities.cpp)
        void applyVitalShare(Fighter& fighter);
        void applySkillPoints(Fighter& fighter);
        [[nodiscard]] float skillPowerOf(std::size_t slot) const;
        /// Handles an effect of archetype Script (by editor id), Calm or Cloak; false for any other.
        bool applyRaceEffect(openeden::sdk::game::CombatRequestWriter& writer, Fighter& caster, Fighter& target,
            const SpellRecord& spell, const SpellEffect& effect, const MagicEffectRecord& record, float seconds,
            EffectTiming timing);
        void addTimedPerk(Fighter& fighter, FormId perk);
        void removeTimedPerk(Fighter& fighter, FormId perk);
        void tickCloak(openeden::sdk::game::CombatRequestWriter& writer, const ActiveEffect& effect, float used);
        [[nodiscard]] float absorbChanceOf(const Fighter& target) const;
        [[nodiscard]] bool spellAbsorbed(Fighter& caster, Fighter& target, const SpellRecord& spell);
        struct AbsorbMemo
        {
            bool mValid = false;
            FormId mSpell = kNullFormId;
            openeden::sdk::game::CombatSubject mCaster;
            openeden::sdk::game::CombatSubject mTarget;
            float mAt = 0.f;
            bool mAbsorbed = false;
        } mAbsorbMemo;
        std::uint64_t mAbsorbRolls = 0;
        std::uint64_t mSkillModRevision = 0;
        std::array<int, 18> mAppliedSkillPoints{};
        std::vector<FormId> mExtraPerks;
        std::vector<FormId> mAppliedExtraPerks; ///< what the profile's perk list holds of them now
        float mAppliedVital[3] = { 0.f, 0.f, 0.f }; ///< the Fortify share the profile's maximums hold now
        float mPendingHealthDelta = 0.f; ///< the engine's health maximum must change by this (the next update says)
        bool mVitalReportPending = false;
        std::uint64_t mAbilityRevision = 0;
        std::vector<FormId> mNpcTestPerks;
        bool mPerksResolved = false;
        std::unique_ptr<PerkEngine> mPerkEngine;
        std::size_t mPerkEffectsApplied = 0;
        float mPlayerBowHold = 0.f;
        float mNextPlayerBow = 1.f;
        std::vector<bool> mPlayerEdges; ///< attack-control edges since the last combatUpdate (true: pressed)
        bool mPlayerDrawing = false;
        // Task 0249: the hands' view of the bow and of a held spell (combat_hands.cpp).
        void trackPlayerHands();
        mutable std::mutex mHandsMutex;
        PlayerHands mHands;
        HandsHoldClock mHandsClock;
        std::uint32_t mHandsBowDraws = 0;
        std::uint32_t mHandsBowReleases = 0;
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
        SpellRecord mPoisonSpell; ///< task 0229: the poison on the player's weapon
        int mPoisonCharges = 0;
        std::function<void(int remaining)> mPoisonSpent;
        // The timing of the player's two attack clips, once known: asking the engine again is then never needed.
        openeden::sdk::game::ClipTiming mKnownTiming[2];
        bool mHaveTiming[2] = { false, false };
        std::uint8_t mTimingClass = kWeaponAnim1HSword; ///< task 0214: the clip class the cached timing belongs to
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
            mCombat.barkNoteDamage(report); // task 0231
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
        bool reportDeath(openeden::sdk::game::ActorHandle actor) override
        {
            mCombat.barkNoteDeath(actor); // task 0231
            return mInner.reportDeath(actor);
        }
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
        bool cancelProjectile(std::uint32_t token) override { return mInner.cancelProjectile(token); } // task 0262
        bool playSound(const openeden::sdk::game::SoundRequest& request) override { return mInner.playSound(request); }

    private:
        openeden::sdk::game::CombatRequestWriter& mInner;
        SkyrimCombat& mCombat;
    };
}

#endif
