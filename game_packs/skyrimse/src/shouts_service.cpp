#include "game_skyrimse/shouts_service.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <span>
#include <vector>

// The shouts service (task 0205): see the header.

namespace game_skyrimse::shouts
{
    using openeden::sdk::BindingResult;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    namespace
    {
        constexpr std::string_view kNamespace = "skyrimse.shouts";

        class Fn final : public openeden::sdk::BindingHandler
        {
        public:
            using Body = std::function<BindingResult(std::span<const BindingValue>)>;
            explicit Fn(Body body)
                : mBody(std::move(body))
            {
            }
            BindingResult call(std::span<const BindingValue> args) override { return mBody(args); }

        private:
            Body mBody;
        };

        BindingValue integer(std::int64_t v)
        {
            BindingValue b;
            b.type = BindingType::Int32;
            b.integer = static_cast<std::int32_t>(std::clamp<std::int64_t>(v, 0, 0x7FFFFFFF));
            return b;
        }

        BindingValue number(double v)
        {
            BindingValue b;
            b.type = BindingType::Double;
            b.number = std::isfinite(v) ? v : 0.0;
            return b;
        }

        BindingValue boolean(bool v)
        {
            BindingValue b;
            b.type = BindingType::Bool;
            b.boolean = v;
            return b;
        }

        BindingResult resultOf(BindingValue v)
        {
            BindingResult r;
            r.value = v;
            return r;
        }

        std::vector<std::string> splitList(const std::string& text)
        {
            std::vector<std::string> out;
            std::size_t start = 0;
            while (start <= text.size())
            {
                std::size_t end = text.find(',', start);
                if (end == std::string::npos)
                    end = text.size();
                std::size_t b = start, e = end;
                while (b < e && (text[b] == ' ' || text[b] == '\t'))
                    ++b;
                while (e > b && (text[e - 1] == ' ' || text[e - 1] == '\t'))
                    --e;
                if (e > b)
                    out.push_back(text.substr(b, e - b));
                start = end + 1;
            }
            return out;
        }
    }

    namespace
    {
        // The editor id of a word without its "Word" prefix ("WordFus" gives "Fus"); the name the game shows when the
        // string table cannot be read.
        std::string wordShortName(const std::string& editorId)
        {
            if (editorId.size() > 4 && editorId.compare(0, 4, "Word") == 0 && editorId[4] >= 'A' && editorId[4] <= 'Z')
                return editorId.substr(4);
            return editorId;
        }
    }

    struct ShoutService::Impl
    {
        Hooks mHooks;
        ShoutState mState;
        ShoutChunk mChunk{ mState };
        TestSettings mSettings;
        bool mApplied = false;
        std::set<FormId> mWarned;
        std::string mNameScratch;
        std::vector<std::unique_ptr<Fn>> mHandlers;
        std::vector<openeden::sdk::FunctionDescriptor> mFunctions;
        std::vector<BindingType> mIntParam{ BindingType::Int32 };
        std::vector<BindingType> mStringParam{ BindingType::String };
        std::vector<BindingType> mTwoInts{ BindingType::Int32, BindingType::Int32 };
        // task 0266: the menu, rebuilt when the state revision moves (or while the records were not there)
        mutable std::vector<MenuShout> mMenu;
        mutable std::uint64_t mMenuRevision = 0;
        mutable bool mMenuValid = false;

        std::string text(const LString& value, FormId owner) const
        {
            if (!value.mPresent)
                return {};
            if (!value.mLocalized)
                return value.mText;
            return mHooks.mText ? mHooks.mText(value, owner) : std::string();
        }

        std::string wordName(const WordRecord& word) const
        {
            std::string name = text(word.mName, word.mCommon.mId);
            return name.empty() ? wordShortName(word.mCommon.mEditorId) : name;
        }

        const std::vector<MenuShout>& menu() const
        {
            const ShoutIndex* idx = index();
            if (idx == nullptr)
            {
                mMenu.clear();
                mMenuValid = false;
                return mMenu;
            }
            if (mMenuValid && mMenuRevision == mState.revision())
                return mMenu;
            mMenu.clear();
            for (const ShoutRecord& shout : idx->shouts())
            {
                bool any = false;
                for (std::size_t i = 0; i < shout.mWordCount; ++i)
                    any = any || mState.learned(shout.mWords[i].mWord);
                if (!any)
                    continue;
                MenuShout entry;
                entry.mId = shout.mCommon.mEditorId;
                entry.mName = prettyShoutName(shout.mCommon.mEditorId);
                entry.mEquipped = shout.mCommon.mId == mState.equipped();
                const std::size_t available = mState.availableWords(shout);
                entry.mCanEquip = available > 0;
                entry.mCanUnlock = available < shout.mWordCount && mState.learned(shout.mWords[available].mWord)
                    && mState.souls() > 0;
                for (std::size_t i = 0; i < shout.mWordCount; ++i)
                {
                    MenuWord word;
                    const FormId id = shout.mWords[i].mWord;
                    if (mState.unlocked(id))
                        word.mState = "unlocked";
                    else if (mState.learned(id))
                        word.mState = "learned";
                    else
                        word.mState = "unknown";
                    if (word.mState != "unknown")
                    {
                        const WordRecord* record = idx->word(id);
                        if (record != nullptr)
                        {
                            word.mName = wordName(*record);
                            word.mTranslation = text(record->mTranslation, id);
                        }
                    }
                    entry.mWords.push_back(std::move(word));
                }
                mMenu.push_back(std::move(entry));
            }
            mMenuRevision = mState.revision();
            mMenuValid = true;
            return mMenu;
        }

        const ShoutIndex* index() const { return mHooks.mIndex ? mHooks.mIndex() : nullptr; }
        void log(const std::string& line) const
        {
            if (mHooks.mLog)
                mHooks.mLog(line);
        }
    };

    ShoutService::ShoutService(Hooks hooks)
        : mImpl(std::make_unique<Impl>())
    {
        mImpl->mHooks = std::move(hooks);
    }

    ShoutService::~ShoutService() = default;

    void ShoutService::setTestSettings(TestSettings settings)
    {
        mImpl->mSettings = std::move(settings);
        mImpl->mApplied = false;
    }

    bool ShoutService::poll()
    {
        Impl& m = *mImpl;
        if (m.mApplied)
            return true;
        const ShoutIndex* index = m.index();
        if (index == nullptr)
            return false;
        m.mApplied = true;
        for (const std::string& name : splitList(m.mSettings.mLearn))
        {
            const WordRecord* word = index->wordByEditorId(name);
            if (word == nullptr)
                m.log("SHOUTS test learn: no word '" + name + "'");
            else
                (void)m.mState.learn(word->mCommon.mId);
        }
        if (m.mSettings.mSouls > 0)
            m.mState.addSouls(static_cast<std::uint32_t>(m.mSettings.mSouls));
        for (const std::string& name : splitList(m.mSettings.mUnlock))
        {
            const WordRecord* word = index->wordByEditorId(name);
            if (word == nullptr)
            {
                m.log("SHOUTS test unlock: no word '" + name + "'");
                continue;
            }
            (void)m.mState.learn(word->mCommon.mId);
            if (!m.mState.unlock(word->mCommon.mId))
            {
                m.mState.addSouls(1); // the setting asks for the word: it pays its own soul
                (void)m.mState.unlock(word->mCommon.mId);
            }
        }
        if (!m.mSettings.mShout.empty())
        {
            const ShoutRecord* shout = index->shoutByEditorId(m.mSettings.mShout);
            if (shout == nullptr)
                m.log("SHOUTS test shout: no shout '" + m.mSettings.mShout + "'");
            else
                m.mState.equip(shout->mCommon.mId);
        }
        m.log("SHOUTS test settings applied: " + std::to_string(m.mState.learnedCount()) + " words learned, "
            + std::to_string(m.mState.unlockedCount()) + " unlocked, " + std::to_string(m.mState.souls()) + " souls");
        return true;
    }

    ShoutState& ShoutService::state()
    {
        return mImpl->mState;
    }

    const ShoutState& ShoutService::state() const
    {
        return mImpl->mState;
    }

    ShoutChunk& ShoutService::chunk()
    {
        return mImpl->mChunk;
    }

    bool ShoutService::teachWord(FormId word)
    {
        Impl& m = *mImpl;
        const ShoutIndex* index = m.index();
        if (index == nullptr || index->word(word) == nullptr)
        {
            if (index != nullptr && m.mWarned.insert(word).second)
                m.log("SHOUTS TeachWord: form " + std::to_string(word) + " is not a word of power");
            return false;
        }
        const WordRecord* record = index->word(word);
        if (m.mState.learn(word))
        {
            // Task 0256: the message the game shows ("Word of Power learned: Fus"; the wording is ours). The name is the
            // editor id without its "Word" prefix (the FULL string is not read, as for the shout names).
            std::string name = record != nullptr ? record->mCommon.mEditorId : std::string();
            if (name.size() > 4 && name.compare(0, 4, "Word") == 0 && name[4] >= 'A' && name[4] <= 'Z')
                name.erase(0, 4);
            m.mState.announce("Word of Power learned: " + name);
        }
        return true;
    }

    bool ShoutService::unlockWord(FormId word)
    {
        Impl& m = *mImpl;
        const ShoutIndex* index = m.index();
        if (index == nullptr || index->word(word) == nullptr)
        {
            if (index != nullptr && m.mWarned.insert(word).second)
                m.log("SHOUTS UnlockWord: form " + std::to_string(word) + " is not a word of power");
            return false;
        }
        (void)m.mState.forceUnlock(word);
        return true;
    }

    bool ShoutService::isWordUnlocked(FormId word) const
    {
        return mImpl->mState.unlocked(word);
    }

    bool ShoutService::unlockWithSoul(std::string_view wordEditorId)
    {
        Impl& m = *mImpl;
        const ShoutIndex* index = m.index();
        if (index == nullptr || wordEditorId.empty())
            return false;
        const WordRecord* word = index->wordByEditorId(wordEditorId);
        return word != nullptr && m.mState.unlock(word->mCommon.mId);
    }

    std::vector<MenuShout> ShoutService::menu() const
    {
        return mImpl->menu();
    }

    MenuResult ShoutService::menuUnlockNext(std::string_view shoutEditorId)
    {
        Impl& m = *mImpl;
        const ShoutIndex* index = m.index();
        const ShoutRecord* shout
            = index == nullptr || shoutEditorId.empty() ? nullptr : index->shoutByEditorId(shoutEditorId);
        if (shout == nullptr)
            return MenuResult::NoShout;
        const std::string name = prettyShoutName(shout->mCommon.mEditorId);
        const std::size_t available = m.mState.availableWords(*shout);
        if (available >= shout->mWordCount)
        {
            m.mState.announce("All the words of " + name + " are unlocked.");
            return MenuResult::AllUnlocked;
        }
        const FormId next = shout->mWords[available].mWord;
        if (!m.mState.learned(next))
        {
            m.mState.announce("You have not learned the next word of " + name + ".");
            return MenuResult::NotLearned;
        }
        if (m.mState.souls() == 0)
        {
            m.mState.announce("You need a dragon soul to unlock a word.");
            return MenuResult::NoSoul;
        }
        if (!m.mState.unlock(next))
            return MenuResult::NoSoul;
        const WordRecord* record = index->word(next);
        m.mState.announce("Word of Power unlocked: " + (record != nullptr ? m.wordName(*record) : std::string()));
        return MenuResult::Done;
    }

    MenuResult ShoutService::menuEquip(std::string_view shoutEditorId)
    {
        Impl& m = *mImpl;
        const ShoutIndex* index = m.index();
        const ShoutRecord* shout
            = index == nullptr || shoutEditorId.empty() ? nullptr : index->shoutByEditorId(shoutEditorId);
        if (shout == nullptr)
            return MenuResult::NoShout;
        const std::string name = prettyShoutName(shout->mCommon.mEditorId);
        if (m.mState.availableWords(*shout) == 0)
        {
            m.mState.announce("You must unlock a word of " + name + " first.");
            return MenuResult::NothingUnlocked;
        }
        m.mState.equip(shout->mCommon.mId);
        m.mState.announce(name + " equipped");
        return MenuResult::Done;
    }

    std::string ShoutService::equippedName() const
    {
        const ShoutIndex* index = mImpl->index();
        if (index == nullptr || mImpl->mState.equipped() == kNullFormId)
            return {};
        const ShoutRecord* shout = index->shout(mImpl->mState.equipped());
        return shout == nullptr ? std::string() : prettyShoutName(shout->mCommon.mEditorId);
    }

    std::size_t ShoutService::equippedWords() const
    {
        const ShoutIndex* index = mImpl->index();
        if (index == nullptr || mImpl->mState.equipped() == kNullFormId)
            return 0;
        const ShoutRecord* shout = index->shout(mImpl->mState.equipped());
        return shout == nullptr ? 0 : mImpl->mState.availableWords(*shout);
    }

    openeden::sdk::DeclareResult ShoutService::declare(openeden::sdk::BindingsService& service)
    {
        Impl& m = *mImpl;
        ShoutService* self = this;
        const auto add = [&](std::string_view name, std::span<const BindingType> params, bool hasResult,
                             BindingType result, Fn::Body body,
                             openeden::sdk::BindingAccess access = openeden::sdk::BindingAccess::Read) {
            m.mHandlers.push_back(std::make_unique<Fn>(std::move(body)));
            openeden::sdk::FunctionDescriptor f;
            f.name = name;
            f.params = params;
            f.hasResult = hasResult;
            f.result = result;
            f.access = access;
            f.handler = m.mHandlers.back().get();
            m.mFunctions.push_back(f);
        };
        add("ready", {}, true, BindingType::Bool, [self](std::span<const BindingValue>) {
            return resultOf(boolean(self->poll()));
        });
        add("revision", {}, true, BindingType::Int32, [self](std::span<const BindingValue>) {
            (void)self->poll();
            return resultOf(integer(static_cast<std::int64_t>(self->state().revision() & 0x7FFFFFFF)));
        });
        add("equipped_name", {}, true, BindingType::String, [self, &m](std::span<const BindingValue>) {
            m.mNameScratch = self->equippedName();
            BindingValue v;
            v.type = BindingType::String;
            v.text = m.mNameScratch;
            return resultOf(v);
        });
        add("cooldown", {}, true, BindingType::Double, [self](std::span<const BindingValue>) {
            return resultOf(number(self->state().cooldown()));
        });
        add("cooldown_total", {}, true, BindingType::Double, [self](std::span<const BindingValue>) {
            return resultOf(number(self->state().cooldownTotal()));
        });
        add("unlocked_count", {}, true, BindingType::Int32, [self](std::span<const BindingValue>) {
            return resultOf(integer(static_cast<std::int64_t>(self->equippedWords())));
        });
        add("word_count", {}, true, BindingType::Int32, [self](std::span<const BindingValue>) {
            const ShoutIndex* index = self->mImpl->index();
            const ShoutRecord* shout = index == nullptr ? nullptr : index->shout(self->state().equipped());
            return resultOf(integer(shout == nullptr ? 0 : static_cast<std::int64_t>(shout->mWordCount)));
        });
        add("souls", {}, true, BindingType::Int32, [self](std::span<const BindingValue>) {
            return resultOf(integer(self->state().souls()));
        });
        // Task 0253: what the state announced ("Dragon soul absorbed"), numbered from 1 like diseases' messages.
        add("notification_count", {}, true, BindingType::Int32, [self](std::span<const BindingValue>) {
            return resultOf(integer(static_cast<std::int64_t>(self->state().noticeCount() & 0x7FFFFFFF)));
        });
        add("notification", m.mIntParam, true, BindingType::String, [self, &m](std::span<const BindingValue> a) {
            m.mNameScratch = !a.empty() && a[0].integer >= 1
                ? self->state().noticeAt(static_cast<std::uint64_t>(a[0].integer))
                : std::string();
            BindingValue v;
            v.type = BindingType::String;
            v.text = m.mNameScratch;
            return resultOf(v);
        });
        add("add_souls", m.mIntParam, false, BindingType::Bool,
            [self](std::span<const BindingValue> a) {
                if (!a.empty() && a[0].integer > 0)
                    self->state().addSouls(static_cast<std::uint32_t>(a[0].integer));
                return BindingResult();
            },
            openeden::sdk::BindingAccess::Mutate);
        // Task 0256: spends a dragon soul on a learned word (no shout menu exists yet to do it)
        add("unlock_word", m.mStringParam, false, BindingType::Bool,
            [self](std::span<const BindingValue> a) {
                if (!a.empty() && a[0].type == BindingType::String)
                    (void)self->unlockWithSoul(a[0].text);
                return BindingResult();
            },
            openeden::sdk::BindingAccess::Mutate);
        // Task 0266 (the shout menu): reads by index into the menu (an index out of range answers empty), writes by the
        // editor id of the shout so a menu that changed between the read and the call never touches another shout.
        const auto shoutAt = [self](std::int32_t i) -> const MenuShout* {
            const std::vector<MenuShout>& menu = self->mImpl->menu();
            return i >= 0 && static_cast<std::size_t>(i) < menu.size() ? &menu[static_cast<std::size_t>(i)] : nullptr;
        };
        const auto wordAt = [shoutAt](std::int32_t i, std::int32_t w) -> const MenuWord* {
            const MenuShout* shout = shoutAt(i);
            return shout != nullptr && w >= 0 && static_cast<std::size_t>(w) < shout->mWords.size()
                ? &shout->mWords[static_cast<std::size_t>(w)]
                : nullptr;
        };
        const auto textResult = [&m](const std::string& value) {
            m.mNameScratch = value;
            BindingValue v;
            v.type = BindingType::String;
            v.text = m.mNameScratch;
            return resultOf(v);
        };
        add("menu_count", {}, true, BindingType::Int32, [self](std::span<const BindingValue>) {
            return resultOf(integer(static_cast<std::int64_t>(self->mImpl->menu().size())));
        });
        add("menu_id", m.mIntParam, true, BindingType::String, [shoutAt, textResult](std::span<const BindingValue> a) {
            const MenuShout* s = shoutAt(a[0].integer);
            return textResult(s != nullptr ? s->mId : std::string());
        });
        add("menu_name", m.mIntParam, true, BindingType::String,
            [shoutAt, textResult](std::span<const BindingValue> a) {
                const MenuShout* s = shoutAt(a[0].integer);
                return textResult(s != nullptr ? s->mName : std::string());
            });
        add("menu_equipped", m.mIntParam, true, BindingType::Bool, [shoutAt](std::span<const BindingValue> a) {
            const MenuShout* s = shoutAt(a[0].integer);
            return resultOf(boolean(s != nullptr && s->mEquipped));
        });
        add("menu_can_equip", m.mIntParam, true, BindingType::Bool, [shoutAt](std::span<const BindingValue> a) {
            const MenuShout* s = shoutAt(a[0].integer);
            return resultOf(boolean(s != nullptr && s->mCanEquip));
        });
        add("menu_can_unlock", m.mIntParam, true, BindingType::Bool, [shoutAt](std::span<const BindingValue> a) {
            const MenuShout* s = shoutAt(a[0].integer);
            return resultOf(boolean(s != nullptr && s->mCanUnlock));
        });
        add("menu_word_count", m.mIntParam, true, BindingType::Int32, [shoutAt](std::span<const BindingValue> a) {
            const MenuShout* s = shoutAt(a[0].integer);
            return resultOf(integer(s != nullptr ? static_cast<std::int64_t>(s->mWords.size()) : 0));
        });
        add("menu_word_name", m.mTwoInts, true, BindingType::String,
            [wordAt, textResult](std::span<const BindingValue> a) {
                const MenuWord* w = wordAt(a[0].integer, a[1].integer);
                return textResult(w != nullptr ? w->mName : std::string());
            });
        add("menu_word_translation", m.mTwoInts, true, BindingType::String,
            [wordAt, textResult](std::span<const BindingValue> a) {
                const MenuWord* w = wordAt(a[0].integer, a[1].integer);
                return textResult(w != nullptr ? w->mTranslation : std::string());
            });
        add("menu_word_state", m.mTwoInts, true, BindingType::String,
            [wordAt, textResult](std::span<const BindingValue> a) {
                const MenuWord* w = wordAt(a[0].integer, a[1].integer);
                return textResult(w != nullptr ? w->mState : std::string());
            });
        add("menu_unlock", m.mStringParam, false, BindingType::Bool,
            [self](std::span<const BindingValue> a) {
                if (!a.empty() && a[0].type == BindingType::String)
                    (void)self->menuUnlockNext(a[0].text);
                return BindingResult();
            },
            openeden::sdk::BindingAccess::Mutate);
        add("menu_equip", m.mStringParam, false, BindingType::Bool,
            [self](std::span<const BindingValue> a) {
                if (!a.empty() && a[0].type == BindingType::String)
                    (void)self->menuEquip(a[0].text);
                return BindingResult();
            },
            openeden::sdk::BindingAccess::Mutate);
        openeden::sdk::NamespaceDescriptor ns;
        ns.name = kNamespace;
        ns.version = { 1, 0 };
        ns.functions = m.mFunctions;
        return service.declareNamespace(ns);
    }
}
