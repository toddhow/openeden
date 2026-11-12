#include <game_skyrimse/survey_scripts.hpp>

#include <algorithm>

namespace game_skyrimse::survey
{
    namespace
    {
        // "On" followed by an upper-case letter, in any case for the "On" (Papyrus ignores case).
        bool looksLikeEvent(std::string_view name)
        {
            return name.size() > 2 && (name[0] == 'O' || name[0] == 'o') && (name[1] == 'N' || name[1] == 'n')
                && name[2] >= 'A' && name[2] <= 'Z';
        }
    }

    void EventSurvey::addScript(const ScriptPapyrus::Pex::File& file)
    {
        using ScriptPapyrus::Pex::Function;
        for (const ScriptPapyrus::Pex::Object& object : file.mObjects)
        {
            ++mObjects;
            std::set<std::string> seenHere;
            for (const ScriptPapyrus::Pex::State& state : object.mStates)
            {
                const bool inState = !file.str(state.mName).empty();
                for (const Function& function : state.mFunctions)
                {
                    ++mFunctions;
                    if (function.mName == Function::kNoName)
                        continue;
                    const std::string_view name = file.str(function.mName);
                    if (function.isNative())
                    {
                        ++mNativeFunctions;
                        if (looksLikeEvent(name))
                            mDeclared.insert(ScriptPapyrus::Pex::lowered(name));
                        continue;
                    }
                    if (!looksLikeEvent(name))
                        continue;
                    const std::string key = ScriptPapyrus::Pex::lowered(name);
                    Acc& acc = mHandled[key];
                    ++acc.mHandlers;
                    if (inState)
                        ++acc.mInStates;
                    if (seenHere.insert(key).second)
                        ++acc.mScripts;
                }
            }
            if (!seenHere.empty())
                ++mObjectsWithHandlers;
        }
    }

    std::vector<EventRow> EventSurvey::events() const
    {
        std::vector<EventRow> rows;
        rows.reserve(mHandled.size());
        for (const auto& [name, acc] : mHandled)
        {
            EventRow row;
            row.mName = name;
            row.mHandlers = acc.mHandlers;
            row.mScripts = acc.mScripts;
            row.mInStates = acc.mInStates;
            row.mDeclaredNative = mDeclared.count(name) != 0;
            rows.push_back(std::move(row));
        }
        std::stable_sort(rows.begin(), rows.end(), [](const EventRow& a, const EventRow& b) {
            return a.mScripts != b.mScripts ? a.mScripts > b.mScripts : a.mName < b.mName;
        });
        return rows;
    }
}
