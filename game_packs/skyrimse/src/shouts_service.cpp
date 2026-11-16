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
        (void)m.mState.learn(word);
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
        openeden::sdk::NamespaceDescriptor ns;
        ns.name = kNamespace;
        ns.version = { 1, 0 };
        ns.functions = m.mFunctions;
        return service.declareNamespace(ns);
    }
}
