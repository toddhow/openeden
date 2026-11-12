#ifndef OPENEDEN_GAME_SKYRIMSE_CONDITIONS_H
#define OPENEDEN_GAME_SKYRIMSE_CONDITIONS_H

// The Skyrim SE conditions engine, v1 framework (task 0122, docs/design/skyrim-se-implementation-plan.md section 8
// item 9, research note 0055 sections 2 and 6.1). Pure code: it evaluates a decoded list of CTDA conditions
// (survey::Ctda, whose layout the survey verified) against a `ConditionWorld` that the caller implements; it reads no
// file and owns no game state.
//
// What is RECALLED-VERIFY (the data shows the shapes, not these meanings; each is stated once here):
//  - compare operator codes: 0 ==, 1 !=, 2 >, 3 >=, 4 <, 5 <= (84 percent of uses are 0, which fits ==);
//  - the OR flag (bit 0x01 of the flags) means "OR with the next condition", and OR binds tighter than AND: a list is
//    a series of clauses, each a maximal run of conditions joined by OR, and the clauses are ANDed. The data cannot
//    show the precedence (no INFO has a text form to compare with);
//  - the use-global flag (0x04): the comparison value is the value of the global whose form id is stored in the
//    comparison field;
//  - run-on values 0 to 7 (subject, target, reference, combat target, linked reference, quest alias, package data,
//    event data): the caller's `resolveRunOn` decides what each means; the condition's reference field is passed
//    along, it holds a form id of a placed reference for some functions;
//  - the meanings of the functions in conditions.cpp (names from xEdit's table, survey_names_xedit.cpp, agreed with
//    by the parameter shapes in note 0055 sections 2.4 and 2.5; the five first ones also from upstream OpenMW's).
// VERIFIED from xEdit (wbDefinitionsCommon.pas wbConditionTypeToStr, wbDefinitionsTES5.pas wbConditions): the compare
// operators 0 to 5 are ==, !=, >, >=, <, <=; the flag bits are 0x01 Or, 0x02 Use Aliases, 0x04 Use Global, 0x08 Use
// Packdata, 0x10 Swap Subject and Target; CIS1/CIS2 carry the string forms of parameters 1 and 2. The flags 0x02 and
// 0x08 change what a reference, actor or package parameter means; no implemented function takes one, so they are
// ignored here; 0x10 is not applied and fails the list closed.
//
// Form ids are passed to the world exactly as stored in the CTDA (the plugin-local id); resolving them to the load
// order is the caller's business.
//
// Fail-closed policy: a list that holds a function this engine does not know (fn_566, fn_426, fn_629 and fn_560 of
// the nine most used have no clean source for their meaning yet and are deliberately NOT guessed), a compare code
// above 5, or that was not decodable, evaluates to FALSE as a whole, whatever the other conditions say. The event is
// counted per function index (`Evaluator::mUnknownFunctions`) and logged once per index through `setLog`.

#include <game_skyrimse/survey.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace game_skyrimse::conditions
{
    /// An opaque handle for the object a condition runs on; 0 means "nobody".
    using Subject = std::uint64_t;

    /// What the functions ask. The caller implements it; every answer is for the current moment of its world.
    class ConditionWorld
    {
    public:
        virtual ~ConditionWorld() = default;

        /// The object a run-on value (CTDA run-on, 0 to 7) names, or 0. `reference` is the condition's reference field.
        virtual Subject resolveRunOn(std::uint32_t runOn, std::uint32_t reference) const = 0;
        /// fn_72 GetIsID: is the subject's base object (or its own id) this form?
        virtual bool hasBaseId(Subject subject, std::uint32_t formId) const = 0;
        /// fn_58 GetStage: the current stage of a quest (0 for an unknown or not started quest).
        virtual float questStage(std::uint32_t quest) const = 0;
        /// fn_59 GetStageDone: has the quest ever reached this stage?
        virtual bool questStageDone(std::uint32_t quest, std::uint32_t stage) const = 0;
        /// fn_71 GetInFaction: is the subject a member of the faction?
        virtual bool inFaction(Subject subject, std::uint32_t faction) const = 0;
        /// fn_74 GetGlobalValue.
        virtual float globalValue(std::uint32_t global) const = 0;

        // Added by the "condnames" follow-up. Each has a default body that answers "no / zero", so a world written
        // for the five functions above still compiles and simply knows nothing of these. Every meaning below is named
        // by xEdit's table (survey_names_xedit.cpp) and is otherwise RECALLED-VERIFY (the Creation Kit wiki was
        // unreachable, HTTP 403, on 2026-11-11). The subject is never 0 when these are asked (the evaluator answers
        // the "nobody" case itself), except where stated.

        /// What the evaluator calls to find the subject of a condition. Run-on 5 (quest alias) and 7 (event data)
        /// keep their value in the condition's third parameter, not in the reference field (xEdit, wbConditions:
        /// "Parameter #3" is the quest alias or the event data in those cases), so the whole condition is passed.
        /// The default forwards to the two-argument `resolveRunOn`. (A different name, not an overload, so the
        /// worlds that override only the two-argument form do not hide it.)
        virtual Subject resolveSubject(const survey::Ctda& condition) const
        {
            return resolveRunOn(condition.mRunOn, condition.mReference);
        }
        /// fn_566 GetIsAliasRef: is the subject the reference that fills alias `alias` of the quest that owns the
        /// condition list being evaluated? The world owns that quest context.
        virtual bool isAliasRef(Subject subject, std::uint32_t alias) const
        {
            (void)subject, (void)alias;
            return false;
        }
        /// fn_426 GetIsVoiceType: is the subject's voice type this VTYP, or in this form list of voice types?
        virtual bool hasVoiceType(Subject subject, std::uint32_t voiceOrList) const
        {
            (void)subject, (void)voiceOrList;
            return false;
        }
        /// fn_629 GetVMQuestVariable: the value of the Papyrus variable `name` on the quest's script (0 if none).
        virtual float questVariable(std::uint32_t quest, const std::string& name) const
        {
            (void)quest, (void)name;
            return 0.f;
        }
        /// fn_359 GetInCurrentLoc: is the subject's current location this one (or inside it)?
        virtual bool inLocation(Subject subject, std::uint32_t location) const
        {
            (void)subject, (void)location;
            return false;
        }
        /// fn_77 GetRandomPercent: a whole number 0 to 99. The world owns the generator (seeded, deterministic).
        virtual float randomPercent() const { return 0.f; }
        /// fn_47 GetItemCount: how many of this item or form list the subject carries.
        virtual float itemCount(Subject subject, std::uint32_t item) const
        {
            (void)subject, (void)item;
            return 0.f;
        }
        /// fn_67 GetInCell: is the subject in this cell?
        virtual bool inCell(Subject subject, std::uint32_t cell) const
        {
            (void)subject, (void)cell;
            return false;
        }
        /// fn_14 GetActorValue: the current value of an actor value (the table index of the value as stored).
        virtual float actorValue(Subject subject, std::uint32_t actorValueIndex) const
        {
            (void)subject, (void)actorValueIndex;
            return 0.f;
        }
        /// The object that is the player, or 0.
        virtual Subject playerSubject() const { return 0; }
        /// fn_70 GetIsSex and fn_131 GetPCIsSex: 0 male, 1 female (xEdit's sex enum), -1 not known.
        virtual int sexOf(Subject subject) const
        {
            (void)subject;
            return -1;
        }
        /// fn_46 GetDead.
        virtual bool isDead(Subject subject) const
        {
            (void)subject;
            return false;
        }
        /// fn_249 IsInDialogueWithPlayer.
        virtual bool inDialogueWithPlayer(Subject subject) const
        {
            (void)subject;
            return false;
        }
        /// fn_579 GetEquippedShout: is this shout the subject's equipped shout?
        virtual bool hasEquippedShout(Subject subject, std::uint32_t shout) const
        {
            (void)subject, (void)shout;
            return false;
        }
        /// fn_543 GetQuestCompleted.
        virtual bool questCompleted(std::uint32_t quest) const
        {
            (void)quest;
            return false;
        }
        /// fn_560 HasKeyword: does the subject (its base form, worn gear included, as the game decides) have it?
        virtual bool hasKeyword(Subject subject, std::uint32_t keyword) const
        {
            (void)subject, (void)keyword;
            return false;
        }
        /// fn_73 GetFactionRank: the subject's rank in the faction, -1 if it is not a member.
        virtual float factionRank(Subject subject, std::uint32_t faction) const
        {
            (void)subject, (void)faction;
            return -1.f;
        }
        /// fn_562 LocationHasKeyword: does the subject's current location have this keyword?
        virtual bool locationHasKeyword(Subject subject, std::uint32_t keyword) const
        {
            (void)subject, (void)keyword;
            return false;
        }
        /// fn_606 GetKeywordDataForLocation(location, keyword): the data value scripts stored for that keyword on that
        /// location (LCTN), 0 when none was set (toddhow, 2026-11-11).
        virtual float keywordDataForLocation(std::uint32_t location, std::uint32_t keyword) const
        {
            (void)location, (void)keyword;
            return 0.f;
        }
        /// fn_403 GetRelationshipRank(target): the subject's relationship rank with the target actor, -4 (enemy) to 4
        /// (lover), 0 neutral (toddhow, 2026-11-11).
        virtual float relationshipRank(Subject subject, std::uint32_t target) const
        {
            (void)subject, (void)target;
            return 0.f;
        }
        /// fn_69 GetIsRace(race): is the subject's race this RACE? (toddhow, 2026-11-11)
        virtual bool isRace(Subject subject, std::uint32_t race) const
        {
            (void)subject, (void)race;
            return false;
        }
        /// fn_35 GetDisabled: is the subject reference disabled? (toddhow, 2026-11-11)
        virtual bool isDisabled(Subject subject) const
        {
            (void)subject;
            return false;
        }
        /// fn_286 IsSneaking: is the subject actor sneaking? (toddhow, 2026-11-11)
        virtual bool isSneaking(Subject subject) const
        {
            (void)subject;
            return false;
        }
        /// fn_310 GetInWorldspace(worldspace): is the subject in an exterior cell of this WRLD? Always false in an
        /// interior cell (CK wiki, shown by toddhow, 2026-11-11).
        virtual bool inWorldspace(Subject subject, std::uint32_t worldspace) const
        {
            (void)subject, (void)worldspace;
            return false;
        }
        /// fn_182 GetEquipped(object): has the subject equipped this object? The original condition does not see items
        /// in the left hand (CK wiki, shown by toddhow, 2026-11-11); a world that wants parity keeps that.
        virtual bool isEquipped(Subject subject, std::uint32_t object) const
        {
            (void)subject, (void)object;
            return false;
        }
        /// fn_1 GetDistance(reference): the distance in game units from the subject to the reference (CK wiki, shown
        /// by toddhow, 2026-11-11). A world that cannot tell (other worldspace, reference not loaded) answers a large
        /// value.
        virtual float distanceTo(Subject subject, std::uint32_t reference) const
        {
            (void)subject, (void)reference;
            return std::numeric_limits<float>::max();
        }
        /// fn_161 GetIsCurrentPackage(package): is the subject running this package? Combat and flight interrupts do
        /// not change the answer (CK wiki, shown by toddhow, 2026-11-11).
        virtual bool runsPackage(Subject subject, std::uint32_t package) const
        {
            (void)subject, (void)package;
            return false;
        }
        /// fn_84 GetDeadCount(actor base): how many actors of this base have been killed (CK wiki, 2026-11-11).
        virtual float deadCount(std::uint32_t actorBase) const
        {
            (void)actorBase;
            return 0.f;
        }
        /// fn_255 GetOffersServicesNow: is the subject's current AI package set to offer services? (CK wiki, shown by
        /// toddhow, 2026-11-11)
        virtual bool offersServicesNow(Subject subject) const
        {
            (void)subject;
            return false;
        }
        /// fn_149 GetIsCurrentWeather(weather): 1 if the current weather is this WTHR, also from the moment a transition
        /// into it starts (CK wiki, shown by toddhow, 2026-11-11).
        virtual bool isCurrentWeather(std::uint32_t weather) const
        {
            (void)weather;
            return false;
        }
        /// fn_300 IsInInterior: is the subject in an interior cell? (CK wiki, shown by toddhow, 2026-11-11)
        virtual bool inInterior(Subject subject) const
        {
            (void)subject;
            return false;
        }
        /// fn_314 IsActorAVictim: the current crime victim, or 0; one actor at most (CK wiki, shown by toddhow,
        /// 2026-11-11).
        virtual Subject crimeVictim() const { return 0; }
        /// fn_145 IsTrespassing: is the subject trespassing? (CK wiki, shown by toddhow, 2026-11-11)
        virtual bool isTrespassing(Subject subject) const
        {
            (void)subject;
            return false;
        }
        /// fn_48 GetGold: the subject's gold (CK wiki, shown by toddhow, 2026-11-11).
        virtual float goldOf(Subject subject) const
        {
            (void)subject;
            return 0.f;
        }
        /// fn_605 LocAliasIsLocation(alias, location): is location alias `alias` of the quest that owns the list filled
        /// with this LCTN? The world owns that quest context (toddhow, 2026-11-11).
        virtual bool locAliasIs(std::uint32_t alias, std::uint32_t location) const
        {
            (void)alias, (void)location;
            return false;
        }
        /// fn_360 GetInCurrentLocAlias(alias): is the subject in the location that alias holds? Counts the subject's
        /// persistence location as well as where it stands (toddhow, 2026-11-11).
        virtual bool inLocationAlias(Subject subject, std::uint32_t alias) const
        {
            (void)subject, (void)alias;
            return false;
        }
        /// fn_453 GetPlayerTeammate: is the subject flagged as the player's teammate? (toddhow, 2026-11-11)
        virtual bool isPlayerTeammate(Subject subject) const
        {
            (void)subject;
            return false;
        }
        /// fn_596 SpellHasKeyword(casting source, keyword): does the spell the subject has equipped at that source
        /// (left hand, right hand, voice, instant; the enum as stored) have the keyword? (toddhow, 2026-11-11)
        virtual bool spellHasKeyword(Subject subject, std::uint32_t castingSource, std::uint32_t keyword) const
        {
            (void)subject, (void)castingSource, (void)keyword;
            return false;
        }
        /// fn_277 GetBaseActorValue(actor value): the base value of the actor value (fn_14 is the current, modified
        /// one) (CK wiki, shown by toddhow, 2026-11-11).
        virtual float baseActorValue(Subject subject, std::uint32_t actorValueIndex) const
        {
            (void)subject, (void)actorValueIndex;
            return 0.f;
        }
        /// fn_50 GetTalkedToPC: has the subject ever spoken to the player? (CK wiki, shown by toddhow, 2026-11-11)
        virtual bool talkedToPlayer(Subject subject) const
        {
            (void)subject;
            return false;
        }
        /// fn_655 GetIntimidateSuccess: can the player intimidate the subject? The formula is the "Mod Player
        /// Intimidation" perk entry point (CK wiki, shown by toddhow, 2026-11-11); the world owns it.
        virtual bool playerCanIntimidate(Subject subject) const
        {
            (void)subject;
            return false;
        }
        /// fn_66 GetShouldAttack(target): the subject's desire to attack the target; above 0 means it attacks under
        /// normal circumstances (CK wiki, shown by toddhow, 2026-11-11).
        virtual float attackDesire(Subject subject, std::uint32_t target) const
        {
            (void)subject, (void)target;
            return 0.f;
        }
        /// fn_640 GetActorValuePercent(actor value): the current value as a fraction of its maximum, 0 to 1 (CK wiki,
        /// shown by toddhow, 2026-11-11).
        virtual float actorValuePercent(Subject subject, std::uint32_t actorValueIndex) const
        {
            (void)subject, (void)actorValueIndex;
            return 0.f;
        }
        /// fn_459 GetCrimeGold(faction, optional): the player's crime gold with the faction; with faction 0, with the
        /// subject's crime faction (subject 0 then means none) (CK wiki, shown by toddhow, 2026-11-11).
        virtual float crimeGold(Subject subject, std::uint32_t faction) const
        {
            (void)subject, (void)faction;
            return 0.f;
        }
        /// fn_289 IsInCombat(flag): is the subject in combat? With `notIgnoring` (the flag non-zero), an actor that
        /// is ignoring combat is not (CK wiki, shown by toddhow, 2026-11-11).
        virtual bool inCombat(Subject subject, bool notIgnoring) const
        {
            (void)subject, (void)notIgnoring;
            return false;
        }
        /// fn_49 GetSleeping: the subject's sleep state: 0 not sleeping, 1 loading the sleeping idle, 2 getting ready
        /// to sleep, 3 sleeping, 4 getting ready to wake up. Also the player's sleep animation state (CK wiki, shown by
        /// toddhow, 2026-11-11).
        virtual float sleepState(Subject subject) const
        {
            (void)subject;
            return 0.f;
        }
        /// fn_264 HasSpell(spell or shout): does the subject know it? Being under its effect is not enough (that is
        /// HasMagicEffect) (CK wiki, Papyrus Actor.HasSpell, shown by toddhow, 2026-11-11).
        virtual bool knowsSpell(Subject subject, std::uint32_t spellOrShout) const
        {
            (void)subject, (void)spellOrShout;
            return false;
        }
        /// fn_654 GetBribeSuccess: would the player's bribe of the subject succeed? No parameters, runs on the subject
        /// (CK condition dialog, shown by toddhow, 2026-11-11); the meaning is INFERRED from GetIntimidateSuccess, no
        /// page describes it.
        virtual bool playerCanBribe(Subject subject) const
        {
            (void)subject;
            return false;
        }
        /// fn_144 GetTrespassWarningLevel: how many warnings the subject (on a guard package, or responding to
        /// trespassing or an aggro radius violation) has given; it counts on the warner, not the trespasser (CK wiki,
        /// shown by toddhow, 2026-11-11).
        virtual float trespassWarnings(Subject subject) const
        {
            (void)subject;
            return 0.f;
        }
        /// fn_248 IsScenePlaying(scene): is the scene playing? The original can answer true for a scene that is not
        /// (CK wiki, shown by toddhow, 2026-11-11); a world decides whether to copy that.
        virtual bool scenePlaying(std::uint32_t scene) const
        {
            (void)scene;
            return false;
        }
        /// fn_101 IsWeaponMagicOut: has the subject a weapon, spell or fists out? (CK wiki, toddhow 2026-11-11)
        virtual bool weaponOrMagicOut(Subject subject) const
        {
            (void)subject;
            return false;
        }
        /// fn_80 GetLevel: the subject's level (CK wiki, toddhow 2026-11-11).
        virtual float levelOf(Subject subject) const
        {
            (void)subject;
            return 0.f;
        }
        /// fn_214 HasMagicEffect(effect): is the subject affected by a spell that can potentially apply the MGEF? True
        /// when the effect-side conditions hold even if the spell-side ones do not and the effect is not active
        /// (CK wiki, shown by toddhow, 2026-11-11).
        virtual bool hasMagicEffect(Subject subject, std::uint32_t effect) const
        {
            (void)subject, (void)effect;
            return false;
        }
        /// fn_580 IsBleedingOut: is the subject bleeding out? (CK wiki, shown by toddhow, 2026-11-11)
        virtual bool isBleedingOut(Subject subject) const
        {
            (void)subject;
            return false;
        }
        /// fn_624 GetInContainer(reference): is the subject inside that container reference? One reference parameter,
        /// runs on the subject (CK condition dialog, shown by toddhow, 2026-11-11); the meaning is INFERRED from the
        /// name, no page describes it.
        virtual bool inContainer(Subject subject, std::uint32_t container) const
        {
            (void)subject, (void)container;
            return false;
        }
        /// fn_61 GetAlarmed: is the subject alarmed? (CK wiki, shown by toddhow, 2026-11-11)
        virtual bool isAlarmed(Subject subject) const
        {
            (void)subject;
            return false;
        }
        /// fn_136 GetIsReference(reference): is the subject this (persistent) placed reference? GetIsID compares the
        /// base object instead (CK wiki, shown by toddhow, 2026-11-11).
        virtual bool isReference(Subject subject, std::uint32_t reference) const
        {
            (void)subject, (void)reference;
            return false;
        }
        /// fn_159 GetSitting: the subject's sitting state: 0 not sitting, 1 loading the sitting idle, 2 getting ready
        /// to sit, 3 sitting, 4 getting ready to stand; 11 to 14 the same for mounting a horse. Never 1 or 11 for the
        /// player; never 1 for a bed or leaning on a wall; the original may not return 11 to 14 reliably (CK wiki,
        /// shown by toddhow, 2026-11-11).
        virtual float sitState(Subject subject) const
        {
            (void)subject;
            return 0.f;
        }
        /// fn_237 GetIsGhost: is the subject in the ghost state? (CK wiki, shown by toddhow, 2026-11-11)
        virtual bool isGhost(Subject subject) const
        {
            (void)subject;
            return false;
        }
        /// fn_630 GetVMScriptVariable(object, "variable"): the value of a Conditional property of a script on the
        /// object (0 if none); the original does not work while the object is in a container (CK wiki, shown by
        /// toddhow, 2026-11-11). The shape (object in parameter 1, name in CIS2) is taken from fn_629, RECALLED-VERIFY.
        virtual float scriptVariable(std::uint32_t object, const std::string& name) const
        {
            (void)object, (void)name;
            return 0.f;
        }
        /// fn_258 HasAssociationType(actor, association type): do the subject and the actor have a relationship of
        /// this ASTP (for example spouse)? (CK wiki, shown by toddhow, 2026-11-11)
        virtual bool hasAssociation(Subject subject, std::uint32_t other, std::uint32_t associationType) const
        {
            (void)subject, (void)other, (void)associationType;
            return false;
        }
        /// fn_635 IsInFavorState (CK wiki, toddhow 2026-11-11: doing something the player asked).
        virtual bool inFavorState(Subject subject) const
        {
            (void)subject;
            return false;
        }
        /// fn_18 GetCurrentTime: the game hour as a decimal, 4:30 am = 4.5, 7:45 pm = 19.75 (CK wiki, shown by toddhow,
        /// 2026-11-11).
        virtual float gameHour() const { return 0.f; }
        /// fn_610 LocAliasHasKeyword(location alias, keyword): does the location that alias of the owning quest holds
        /// have the keyword? Parameters from the CK condition dialog (shown by toddhow, 2026-11-11); the meaning is
        /// INFERRED from the name and fn_605.
        virtual bool locAliasHasKeyword(std::uint32_t alias, std::uint32_t keyword) const
        {
            (void)alias, (void)keyword;
            return false;
        }
        /// fn_288 GetFriendHit(target actor): the number of friendly hits the subject has taken from the target (CK
        /// wiki, shown by toddhow, 2026-11-11).
        virtual float friendHits(Subject subject, std::uint32_t target) const
        {
            (void)subject, (void)target;
            return 0.f;
        }
        /// fn_415 GetGroupMemberCount (obsolete, identical to GetCombatGroupMemberCount): the number of actors in the
        /// subject's combat group (its allies in the fight); 0 when not in combat or with no combat target; the dead
        /// leave the group (CK wiki, shown by toddhow, 2026-11-11).
        virtual float combatGroupSize(Subject subject) const
        {
            (void)subject;
            return 0.f;
        }
        /// fn_594 GetIsFlying (CK wiki, toddhow 2026-11-11: flying, 0 once the landing animation ends).
        virtual bool isFlying(Subject subject) const
        {
            (void)subject;
            return false;
        }
        /// fn_125 IsGuard (CK wiki, toddhow 2026-11-11: the NPC's class is marked as a Guard class).
        virtual bool isGuardClass(Subject subject) const
        {
            (void)subject;
            return false;
        }
        /// fn_274 IsSmallBump (CK wiki, toddhow 2026-11-11: an actor walked into the subject; false if they ran in and caused a stagger).
        virtual bool isSmallBump(Subject subject) const
        {
            (void)subject;
            return false;
        }
        /// fn_56 GetQuestRunning(quest): is the quest running (not stopped, completed or failed)? (toddhow, 2026-11-11)
        virtual bool questRunning(std::uint32_t quest) const
        {
            (void)quest;
            return false;
        }
    };

    /// The CTDA subrecords of a record's (inflated) data, in order. False if the data is malformed (a subrecord
    /// framing fault or a CTDA that is not exactly survey::kCtdaSize bytes); `out` then holds what was decoded.
    [[nodiscard]] bool decodeConditions(std::span<const std::byte> recordData, std::vector<survey::Ctda>& out);

    /// A function: the value the condition compares. It gets the condition, the subject its run-on resolved to and
    /// the world.
    using ConditionFunction = float (*)(const survey::Ctda&, Subject, const ConditionWorld&);

    /// The function for an index, or nullptr when this engine does not know it.
    [[nodiscard]] ConditionFunction findFunction(std::uint16_t index);

    /// Applies compare operator code 0 to 5 (see above); `valid` is false for any other code.
    [[nodiscard]] bool compareValues(std::uint8_t op, float lhs, float rhs, bool& valid);

    class Evaluator
    {
    public:
        /// Called once per unknown function index with a one-line text.
        void setLog(std::function<void(const std::string&)> log) { mLog = std::move(log); }

        /// True if the whole list passes. An empty list passes. Every condition is evaluated (none has a side
        /// effect), so the counters below do not depend on the order of the list.
        bool evaluate(std::span<const survey::Ctda> list, const ConditionWorld& world);

        std::uint64_t mLists = 0;
        std::uint64_t mListsFailedClosed = 0; ///< lists made false by an unknown function or a bad compare code
        std::uint64_t mConditions = 0;
        std::uint64_t mConditionsUnknown = 0; ///< conditions whose function is unknown
        std::uint64_t mConditionsBadCompare = 0;
        /// Conditions with the "swap subject and target" flag (0x10, xEdit's condition type flags), which this engine
        /// does not apply: their list fails closed (26 uses in the five masters).
        std::uint64_t mConditionsSwapped = 0;
        std::map<std::uint16_t, std::uint64_t> mUnknownFunctions; ///< by function index: conditions

    private:
        std::function<void(const std::string&)> mLog;
    };
}

#endif
