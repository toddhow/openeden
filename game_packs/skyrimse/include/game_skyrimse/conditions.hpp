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
//  - the meanings of the five functions below (names from upstream OpenMW's table, survey_names.cpp, agreed with by
//    the parameter shapes in note 0055 section 2.4).
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
        std::map<std::uint16_t, std::uint64_t> mUnknownFunctions; ///< by function index: conditions

    private:
        std::function<void(const std::string&)> mLog;
    };
}

#endif
