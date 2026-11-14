#ifndef OPENEDEN_GAME_SKYRIMSE_QUEST_SCRIPT_H
#define OPENEDEN_GAME_SKYRIMSE_QUEST_SCRIPT_H

// Quest scripts and their variables at the start of a game (task 0156). GetVMQuestVariable (fn_629, 5,419 INFO and
// 671 PACK conditions in the five masters) asks for a variable or property of a script attached to a quest. The pack
// runs no Papyrus in a conversation, so the honest answer is the value the variable has BEFORE any script ran:
//   - the property value the quest's VMAD sets, else
//   - the initial value declared in the compiled script (.pex) of the script and its parents,
// and NO answer (`mKnown` false, the condition stays "unanswered") when
//   - the quest has no script, or no script of it names the variable,
//   - the variable is not an int, float or bool,
//   - the script's OnInit handler (or a function of the same script it calls on `self`) writes the variable, because
//     a real game would have run it before any dialogue.
// Writes made through ANOTHER script's call into this script are not seen (nothing runs in this session either).
//
// VMAD layout of a QUST (VERIFIED by the tests on generated records and by the aggregates in the task log):
// i16 version, i16 object format, u16 script count; per script: wstring name, u8 status, u16 property count; per
// property: wstring name, u8 type, u8 status, then the value: 1 object (8 bytes: formid and alias in the order of the
// object format), 2 wstring, 3 i32, 4 f32, 5 u8 bool, 11..15 u32 count then that many of the single kind. The
// fragments and aliases after the scripts are not read. RECALLED-VERIFY: the property type numbers and the VMAD
// field order (xEdit's wbVMAD); the data agrees (the walk ends exactly inside the subrecord for the aggregates).

#include <game_skyrimse/records.hpp>

#include <components/contentsource/plugin.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace game_skyrimse
{
    struct QuestScriptProperty
    {
        std::string mName;
        std::uint8_t mType = 0; ///< VMAD type number
        bool mScalar = false; ///< int, float or bool: `mValue` is set
        float mValue = 0.f;
    };

    struct QuestScriptRef
    {
        std::string mName;
        std::vector<QuestScriptProperty> mProperties;
    };

    struct QuestScriptEntry
    {
        FormId mQuest = kNullFormId;
        bool mDeleted = false;
        bool mOk = true; ///< false: the VMAD was damaged; `mScripts` holds what was read before the fault
        std::vector<QuestScriptRef> mScripts;
    };

    struct QuestScriptLimits
    {
        std::uint64_t mMaxRecordBytes = 16ull * 1024 * 1024;
        std::uint32_t mMaxScripts = 256;
        std::uint32_t mMaxProperties = 4096;
        std::uint32_t mMaxArray = 65536;
        std::uint32_t mMaxErrors = 256;
    };

    struct QuestScriptRecords
    {
        std::string mName;
        std::vector<QuestScriptEntry> mQuests;
        std::uint64_t mQuestsSeen = 0;
        std::uint64_t mWithScripts = 0;
        std::uint64_t mDamaged = 0;
        std::uint64_t mUnresolvedFormIds = 0;
        std::vector<Content::Error> mErrors;
        std::uint64_t mErrorsDropped = 0;
        std::optional<Content::Error> mWalkError;
    };

    [[nodiscard]] bool parseQuestScripts(const Content::Plugin& plugin, const LoadOrder& order,
        const QuestScriptLimits& limits, QuestScriptRecords& out);

    /// The VMAD script list of one subrecord; false when it is damaged (what was read stays in `out`).
    [[nodiscard]] bool parseVmadScripts(
        std::span<const std::byte> vmad, const QuestScriptLimits& limits, std::vector<QuestScriptRef>& out);

    /// A later plugin's QUST replaces an earlier one's scripts (a record without a VMAD has none).
    class QuestScriptIndex
    {
    public:
        void add(QuestScriptRecords records);
        [[nodiscard]] const QuestScriptEntry* find(FormId quest) const;
        [[nodiscard]] std::size_t count() const { return mQuests.size(); }

    private:
        std::unordered_map<FormId, QuestScriptEntry> mQuests;
    };

    /// Reads `scripts/<name>.pex` (lower case name without extension). False when the file is not there.
    using PexLoader = std::function<bool(const std::string& scriptName, std::vector<std::uint8_t>& bytes)>;

    struct QuestVariableValue
    {
        bool mKnown = false;
        float mValue = 0.f;
    };

    /// Not thread safe: it caches parsed scripts. Outlives nothing it is given (index and loader must outlive it).
    class QuestVariables
    {
    public:
        QuestVariables(const QuestScriptIndex& scripts, PexLoader loader);
        ~QuestVariables();
        QuestVariables(const QuestVariables&) = delete;
        QuestVariables& operator=(const QuestVariables&) = delete;

        /// `variable` as the condition's CIS2 string gives it: a property name, or "::name_var" of a script variable
        /// (compared without regard to case).
        [[nodiscard]] QuestVariableValue value(FormId quest, std::string_view variable) const;

        [[nodiscard]] std::size_t scriptsLoaded() const;
        [[nodiscard]] std::size_t scriptsMissing() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}

#endif
