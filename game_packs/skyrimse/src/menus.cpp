#include "game_skyrimse/menus.hpp"

#include "game_skyrimse/pack_core.hpp"

#include <algorithm>
#include <cstring>

// Layout and decisions: see the header and docs/tasks/0207-skyrim-menus-and-loading-screens.md.

namespace game_skyrimse::menus
{
    namespace
    {
        std::uint32_t le32(const std::byte* p)
        {
            std::uint32_t v;
            std::memcpy(&v, p, 4);
            return v;
        }

        std::uint64_t splitmix(std::uint64_t seed)
        {
            std::uint64_t z = seed + 0x9E3779B97F4A7C15ull;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }

        // A tip has no tags worth showing (<Alias=...> and the like): they are dropped.
        std::string stripTags(const std::string& text)
        {
            std::string out;
            out.reserve(text.size());
            bool inTag = false;
            for (const char c : text)
            {
                if (c == '<')
                    inTag = true;
                else if (c == '>' && inTag)
                    inTag = false;
                else if (!inTag)
                    out.push_back(c);
            }
            return inTag ? text : out; // a '<' that never closes is text
        }

        Content::Error makeError(const std::string& file, std::uint64_t offset, std::uint32_t type, std::string message)
        {
            Content::Error error;
            error.mFile = file;
            error.mOffset = offset;
            error.mRecordType = type;
            error.mMessage = std::move(message);
            return error;
        }

        class ScreenParser final : public Content::Visitor
        {
        public:
            ScreenParser(const Content::Plugin& plugin, const FormIdResolver& resolver,
                const LoadingScreenLimits& limits, LoadingScreenRecords& out)
                : mPlugin(plugin)
                , mResolver(resolver)
                , mLimits(limits)
                , mOut(out)
                , mLocalized(plugin.header().isLocalized())
            {
            }

            bool onRecord(const Content::RawRecord& record) override
            {
                if (record.mType != Content::fourCC("LSCR"))
                    return true;
                const bool deleted = (record.mFlags & RecordFlags::Deleted) != 0;
                std::span<const std::byte> bytes;
                if (!deleted)
                {
                    bytes = record.mData;
                    if (record.isCompressed())
                    {
                        if (record.mUncompressedSize > mLimits.mMaxRecordBytes)
                        {
                            fail(record, "the record inflates past the limit");
                            return true;
                        }
                        Content::Error error;
                        if (!inflateRecord(record, mPlugin.name(), mInflated, error))
                        {
                            addError(std::move(error));
                            return true;
                        }
                        bytes = mInflated;
                    }
                    else if (record.mDataSize > mLimits.mMaxRecordBytes)
                    {
                        fail(record, "the record is over the size limit");
                        return true;
                    }
                }
                if (mOut.mScreens.size() >= mLimits.mMaxScreens)
                {
                    fail(record, "too many loading screens");
                    return true;
                }
                LoadingScreen screen;
                screen.mCommon.mStoredId = record.mFormId;
                screen.mCommon.mFlags = record.mFlags;
                screen.mCommon.mOffset = record.mOffset;
                screen.mCommon.mId = resolved(record.mFormId);
                if (screen.mCommon.mId == kNullFormId || screen.mCommon.mId == kUnresolvedFormId)
                    return true;
                if (!deleted)
                {
                    survey::Ctda* last = nullptr;
                    const bool ok = survey::forEachSubrecord(bytes, [&](const survey::SubrecordView& sub) {
                        switch (sub.mType)
                        {
                            case Content::fourCC("EDID"):
                            {
                                const char* begin = reinterpret_cast<const char*>(sub.mData.data());
                                const void* nul
                                    = sub.mData.empty() ? nullptr : std::memchr(begin, '\0', sub.mData.size());
                                screen.mCommon.mEditorId.assign(begin,
                                    nul != nullptr ? static_cast<const char*>(nul) : begin + sub.mData.size());
                                break;
                            }
                            case Content::fourCC("DESC"):
                                readText(sub.mData, screen.mText);
                                break;
                            case Content::fourCC("NNAM"):
                                if (sub.mData.size() != 4)
                                    ++mOut.mOddSubrecords;
                                else
                                    screen.mObject = le32(sub.mData.data());
                                break;
                            case Content::fourCC("CTDA"):
                            {
                                if (screen.mConditions.size() >= mLimits.mMaxConditions)
                                {
                                    screen.mConditionsOk = false;
                                    last = nullptr;
                                    break;
                                }
                                survey::Ctda c;
                                if (!survey::decodeCtda(sub.mData, c))
                                {
                                    screen.mConditionsOk = false;
                                    last = nullptr;
                                    break;
                                }
                                screen.mConditions.push_back(std::move(c));
                                last = &screen.mConditions.back();
                                break;
                            }
                            case Content::fourCC("CIS1"):
                            case Content::fourCC("CIS2"):
                            {
                                if (last == nullptr)
                                    break;
                                const char* begin = reinterpret_cast<const char*>(sub.mData.data());
                                const void* nul
                                    = sub.mData.empty() ? nullptr : std::memchr(begin, '\0', sub.mData.size());
                                const bool first = sub.mType == Content::fourCC("CIS1");
                                std::string& target = first ? last->mString1 : last->mString2;
                                target.assign(
                                    begin, nul != nullptr ? static_cast<const char*>(nul) : begin + sub.mData.size());
                                break;
                            }
                            default:
                                break;
                        }
                    });
                    if (!ok)
                    {
                        fail(record, "a subrecord is cut short or claims more bytes than the record holds");
                        return true;
                    }
                }
                mOut.mScreens.push_back(std::move(screen));
                return true;
            }

        private:
            void readText(std::span<const std::byte> data, LString& out)
            {
                out.mPresent = true;
                if (mLocalized)
                {
                    if (data.size() != 4)
                    {
                        ++mOut.mOddSubrecords;
                        out.mPresent = false;
                        return;
                    }
                    out.mLocalized = true;
                    out.mId = le32(data.data());
                    return;
                }
                const char* begin = reinterpret_cast<const char*>(data.data());
                const void* nul = data.empty() ? nullptr : std::memchr(begin, '\0', data.size());
                out.mText.assign(begin, nul != nullptr ? static_cast<const char*>(nul) : begin + data.size());
            }

            FormId resolved(std::uint32_t stored)
            {
                if (stored == 0)
                    return kNullFormId;
                const std::optional<FormId> id = mResolver.resolve(stored);
                if (!id)
                {
                    ++mOut.mUnresolvedFormIds;
                    return kUnresolvedFormId;
                }
                return *id;
            }

            void addError(Content::Error error)
            {
                ++mOut.mFailed;
                if (mOut.mErrors.size() < mLimits.mMaxErrors)
                    mOut.mErrors.push_back(std::move(error));
            }

            void fail(const Content::RawRecord& record, std::string message)
            {
                addError(makeError(mPlugin.name(), record.mOffset, record.mType, std::move(message)));
            }

            const Content::Plugin& mPlugin;
            const FormIdResolver& mResolver;
            const LoadingScreenLimits& mLimits;
            LoadingScreenRecords& mOut;
            const bool mLocalized;
            std::vector<std::byte> mInflated;
        };
    }

    bool parseLoadingScreens(const Content::Plugin& plugin, const LoadOrder& order, const LoadingScreenLimits& limits,
        LoadingScreenRecords& out)
    {
        out = LoadingScreenRecords();
        out.mName = plugin.name();
        if (!isSkyrimSeHeader(plugin.header()))
        {
            out.mWalkError = makeError(plugin.name(), 0, 0, "this plugin is not a Skyrim Special Edition one");
            return false;
        }
        const FormIdResolver resolver(plugin.header(), plugin.name(), order);
        ScreenParser parser(plugin, resolver, limits, out);
        const std::optional<Content::Error> error = plugin.walk(parser);
        if (error)
        {
            out.mWalkError = *error;
            return false;
        }
        return true;
    }

    // ---- the index -------------------------------------------------------------------------------------------------

    void LoadingScreenIndex::add(LoadingScreenRecords&& records, FormIdResolver resolver)
    {
        const std::uint32_t plugin = static_cast<std::uint32_t>(mResolvers.size());
        mResolvers.push_back(std::move(resolver));
        for (LoadingScreen& s : records.mScreens)
        {
            const auto same = [&s](const LoadingScreen& other) { return other.mCommon.mId == s.mCommon.mId; };
            const auto it = std::find_if(mScreens.begin(), mScreens.end(), same);
            if (s.mCommon.deleted())
            {
                if (it != mScreens.end())
                    mScreens.erase(it);
                continue;
            }
            s.mPlugin = plugin;
            if (it != mScreens.end())
                *it = std::move(s);
            else
                mScreens.push_back(std::move(s));
        }
        records.mScreens.clear();
    }

    std::vector<const LoadingScreen*> LoadingScreenIndex::screens() const
    {
        std::vector<const LoadingScreen*> out;
        out.reserve(mScreens.size());
        for (const LoadingScreen& s : mScreens)
            out.push_back(&s);
        std::sort(out.begin(), out.end(),
            [](const LoadingScreen* a, const LoadingScreen* b) { return a->mCommon.mId < b->mCommon.mId; });
        return out;
    }

    // ---- the pick --------------------------------------------------------------------------------------------------

    bool pickTip(const TipInputs& inputs, std::uint64_t seed, Tip& out)
    {
        out = Tip();
        if (inputs.mScreens == nullptr || !inputs.mText)
            return false;
        std::vector<std::pair<const LoadingScreen*, std::string>> candidates;
        for (const LoadingScreen* screen : inputs.mScreens->screens())
        {
            if (!screen->mText.mPresent || !screen->mConditionsOk)
                continue;
            std::string text = inputs.mText(*screen);
            if (text.empty())
                continue;
            if (inputs.mPasses && !inputs.mPasses(*screen))
                continue;
            candidates.emplace_back(screen, std::move(text));
        }
        out.mCandidates = candidates.size();
        if (candidates.empty())
            return false;
        auto& [screen, text] = candidates[splitmix(seed) % candidates.size()];
        if (text.size() > kMaxTipBytes)
        {
            std::size_t cut = kMaxTipBytes;
            // the first dropped byte is the middle of a character: drop the whole character
            while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
                --cut;
            text.resize(cut);
        }
        out.mScreen = screen->mCommon.mId;
        out.mText = std::move(text);
        return true;
    }

    // ---- the service -----------------------------------------------------------------------------------------------

    namespace
    {
        using openeden::sdk::BindingResult;
        using openeden::sdk::BindingType;
        using openeden::sdk::BindingValue;

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
    }

    struct MenusService::Impl
    {
        std::vector<std::unique_ptr<Fn>> mHandlers;
        std::vector<openeden::sdk::FunctionDescriptor> mFunctions;
        std::string mTipText; // the text of the last answer: a binding result views it until the next call
        int mTipsLogged = 0; // the first few answers are logged (the in-game check reads them)
    };

    MenusService::MenusService(Hooks hooks)
        : mImpl(std::make_unique<Impl>())
        , mHooks(std::move(hooks))
    {
    }

    MenusService::~MenusService() = default;

    bool MenusService::poll()
    {
        if (mReady)
            return true;
        Access access;
        if (mHooks.mAccess && mHooks.mAccess(access) && access.mScreens != nullptr)
        {
            mAccess = std::move(access);
            mReady = true;
            if (mHooks.mLog)
                mHooks.mLog("MENUS ready: " + std::to_string(mAccess.mScreens->size()) + " loading screens");
        }
        return mReady;
    }

    std::size_t MenusService::screenCount() const
    {
        return mReady && mAccess.mScreens != nullptr ? mAccess.mScreens->size() : 0;
    }

    bool MenusService::tip(std::uint64_t seed, Tip& out)
    {
        out = Tip();
        if (!poll())
            return false;
        const LoadingScreenIndex& index = *mAccess.mScreens;
        TipInputs in;
        in.mScreens = &index;
        std::size_t noText = 0, failed = 0;
        std::size_t kindHits[3] = { 0, 0, 0 }; // which table gave the text: .DLSTRINGS, .STRINGS, .ILSTRINGS
        in.mText = [this, &noText, &kindHits](const LoadingScreen& s) {
            std::string text;
            if (!s.mText.mLocalized || !mAccess.mText)
                text = s.mText.mText;
            else
            {
                // the table that holds an LSCR's DESC is tried first, then the other two (an unknown layout is read
                // the way the journal reads a QUST text)
                std::size_t which = 0;
                for (const StringTableKind kind :
                    { StringTableKind::DlStrings, StringTableKind::Strings, StringTableKind::IlStrings })
                {
                    text = mAccess.mText(s.mText, s.mPlugin, kind);
                    if (!text.empty())
                    {
                        ++kindHits[which];
                        break;
                    }
                    ++which;
                }
            }
            text = stripTags(text);
            if (text.empty())
                ++noText;
            return text;
        };
        in.mPasses = [this, &index, &failed](const LoadingScreen& s) {
            // a screen with no conditions always passes; without an evaluator only those can be chosen
            const bool passes = s.mConditions.empty()
                || (mAccess.mEvaluate && mAccess.mEvaluate(s.mConditions, index.resolverOf(s)));
            if (!passes)
                ++failed;
            return passes;
        };
        const bool picked = pickTip(in, seed, out);
        if (mImpl->mTipsLogged < 3 && mHooks.mLog)
        {
            ++mImpl->mTipsLogged;
            mHooks.mLog("MENUS tip " + std::to_string(seed) + ": " + std::to_string(out.mCandidates) + " candidates of "
                + std::to_string(index.size()) + " (no text " + std::to_string(noText) + ", conditions failed "
                + std::to_string(failed) + ", text from .DLSTRINGS/.STRINGS/.ILSTRINGS " + std::to_string(kindHits[0])
                + "/" + std::to_string(kindHits[1]) + "/" + std::to_string(kindHits[2]) + ")"
                + (picked ? ", \"" + out.mText.substr(0, 60) + "\"" : ", none"));
        }
        return picked;
    }

    openeden::sdk::DeclareResult MenusService::declare(openeden::sdk::BindingsService& service)
    {
        Impl& m = *mImpl;
        MenusService* self = this;
        static const BindingType seedParam[] = { BindingType::Int32 };
        const auto add = [&](std::string_view name, std::span<const BindingType> params, BindingType result,
                             Fn::Body body) {
            m.mHandlers.push_back(std::make_unique<Fn>(std::move(body)));
            openeden::sdk::FunctionDescriptor f;
            f.name = name;
            f.params = params;
            f.hasResult = true;
            f.result = result;
            f.access = openeden::sdk::BindingAccess::Read;
            f.handler = m.mHandlers.back().get();
            m.mFunctions.push_back(f);
        };
        add("ready", {}, BindingType::Bool, [self](std::span<const BindingValue>) {
            BindingResult r;
            r.value.type = BindingType::Bool;
            r.value.boolean = self->poll();
            return r;
        });
        add("tip_count", {}, BindingType::Int32, [self](std::span<const BindingValue>) {
            (void)self->poll();
            BindingResult r;
            r.value.type = BindingType::Int32;
            r.value.integer = static_cast<std::int32_t>(self->screenCount());
            return r;
        });
        add("tip", seedParam, BindingType::String, [self, &m](std::span<const BindingValue> a) {
            Tip tip;
            m.mTipText.clear();
            if (self->tip(static_cast<std::uint64_t>(static_cast<std::int64_t>(a[0].integer)), tip))
                m.mTipText = std::move(tip.mText);
            BindingResult r;
            r.value.type = BindingType::String;
            r.value.text = m.mTipText;
            return r;
        });

        openeden::sdk::NamespaceDescriptor ns;
        ns.name = kNamespace;
        ns.version = { 1, 0 };
        ns.functions = m.mFunctions;
        ns.menuSafe = true;
        return service.declareNamespace(ns);
    }
}
