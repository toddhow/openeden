#include "game_skyrimse/lockbooks_service.hpp"

#include <cmath>
#include <cstdio>

// The lock picking and book service and its bindings: see the header (task 0203).

namespace game_skyrimse::lockbooks
{
    using openeden::sdk::BindingAccess;
    using openeden::sdk::BindingResult;
    using openeden::sdk::BindingShape;
    using openeden::sdk::BindingStatus;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    namespace
    {
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

        BindingResult ok()
        {
            return BindingResult{};
        }

        BindingResult fail(BindingStatus status, std::string_view message)
        {
            BindingResult r;
            r.status = status;
            r.message = message;
            return r;
        }

        BindingValue boolean(bool v)
        {
            BindingValue b;
            b.type = BindingType::Bool;
            b.boolean = v;
            return b;
        }
        BindingValue integer(std::int32_t v)
        {
            BindingValue b;
            b.type = BindingType::Int32;
            b.integer = v;
            return b;
        }
        BindingValue number(double v)
        {
            BindingValue b;
            b.type = BindingType::Double;
            b.number = v;
            return b;
        }
        BindingValue text(std::string_view v)
        {
            BindingValue b;
            b.type = BindingType::String;
            b.text = v;
            return b;
        }

        constexpr std::size_t kLockpickingSlot = 8; // progression::Slot::Lockpicking
        constexpr std::size_t kMaxCachedBooks = 256;
    }

    struct LockBooksService::Impl
    {
        std::vector<std::unique_ptr<Fn>> mHandlers;
        std::vector<openeden::sdk::FunctionDescriptor> mFunctions;
        // result buffers: valid until the handler is called again
        std::string mText;
        std::vector<std::string> mStrings;
        std::vector<BindingValue> mElements;
        // per book, main thread
        std::map<FormId, std::vector<std::string>> mPages;
        std::map<FormId, std::string> mTitles;
        std::map<FormId, std::string> mResults;
        std::string mNoResult;
        std::vector<std::string> mNoPages;
        bool mRefused = false;
    };

    LockBooksService::LockBooksService(
        inventory::InventoryService& inventory, ServiceHooks hooks, PickSettings settings)
        : mImpl(std::make_unique<Impl>())
        , mInventory(inventory)
        , mHooks(std::move(hooks))
        , mSettings(settings)
        , mChunk(mModel)
    {
    }

    LockBooksService::~LockBooksService()
    {
        if (mThread.joinable())
            mThread.join();
    }

    bool LockBooksService::poll()
    {
        if (mReady)
            return true;
        const int state = mState.load(std::memory_order_acquire);
        if (state == 0)
        {
            auto inputs = std::make_shared<BookInputs>();
            if (!mHooks.mInputs || !mHooks.mInputs(*inputs))
                return false;
            mState.store(1, std::memory_order_release);
            mThread = std::thread([this, inputs]() {
                BookIndex index;
                try
                {
                    const BookLimits limits;
                    std::uint32_t pluginIndex = 0;
                    for (const std::filesystem::path& path : inputs->mPaths)
                    {
                        Content::Error openError;
                        const std::unique_ptr<Content::Plugin> file = Content::Plugin::openFile(path, openError);
                        if (file != nullptr)
                        {
                            BookRecords records;
                            (void)parseBookRecords(*file, inputs->mOrder, limits, records);
                            index.add(std::move(records), pluginIndex);
                        }
                        ++pluginIndex;
                    }
                }
                catch (...)
                {
                    // what was read stays: the index is then partial, never undefined
                }
                mBuilt = std::move(index);
                mState.store(2, std::memory_order_release);
            });
            return false;
        }
        if (state == 1)
            return false;
        mThread.join();
        mBooks = std::move(mBuilt);
        mReady = true;
        if (mHooks.mLog)
            mHooks.mLog("LOCKBOOKS the book index is built on a worker: " + std::to_string(mBooks.size()) + " books, "
                + std::to_string(mBooks.odd()) + " odd");
        return true;
    }

    // ---- locks ---------------------------------------------------------------------------------------------------

    std::int32_t LockBooksService::picks() const
    {
        return mInventory.model().count(inventory::kPlayerOwner, kLockpickItem);
    }

    bool LockBooksService::pickBegin(std::uint64_t placement, std::int32_t level)
    {
        mSerial = mSerial == 0x7FFFFFFF ? 1 : mSerial + 1;
        const auto refuse = [&](std::string why) {
            mReason = std::move(why);
            mImpl->mRefused = true;
            if (mHooks.mLog)
                mHooks.mLog("LOCKBOOKS pick refused: " + mReason);
            return false;
        };
        if (!mReady)
        {
            mReason = "lock picking is not ready (the records are still being read)";
            return false;
        }
        if (mModel.isPicked(placement))
            return refuse("the lock is already open");
        if (!pickable(level))
            return refuse("this lock needs its key");
        if (picks() <= 0)
            return refuse("you have no lockpick");
        const float skill = mHooks.mSkill ? mHooks.mSkill() : 0.f;
        if (!mAttempt.begin(placement, level, skill, mSettings))
            return refuse("this lock needs its key");
        mImpl->mRefused = false;
        mReason.clear();
        if (mHooks.mLog)
            mHooks.mLog("LOCKBOOKS pick begins on " + inventory::formatId(placement) + " level "
                + std::to_string(level) + " (" + std::string(tierName(mAttempt.tier())) + "), window "
                + std::to_string(mAttempt.window()) + " degrees, " + std::to_string(picks()) + " lockpicks");
        return true;
    }

    void LockBooksService::pickInput(float angle, bool turning, float dt)
    {
        if (mAttempt.state() != Attempt::State::Picking)
            return;
        mAttempt.update(dt, angle, turning);
        if (mAttempt.state() == Attempt::State::Opened)
        {
            (void)mModel.markPicked(mAttempt.placement());
            const float xp = unlockExperience(mAttempt.tier(), mSettings);
            if (mHooks.mSkillUsed)
                mHooks.mSkillUsed(kLockpickingSlot, xp);
            if (mHooks.mLog)
                mHooks.mLog("LOCKBOOKS lock " + inventory::formatId(mAttempt.placement()) + " picked, experience "
                    + std::to_string(xp));
        }
        else if (mAttempt.state() == Attempt::State::Broke)
        {
            (void)mInventory.model().consume(inventory::kPlayerOwner, kLockpickItem, 1);
            mModel.addBroken();
            if (mHooks.mLog)
                mHooks.mLog("LOCKBOOKS a lockpick broke, " + std::to_string(picks()) + " left");
        }
    }

    void LockBooksService::pickCancel()
    {
        if (mAttempt.state() != Attempt::State::Picking)
            return;
        mAttempt.cancel();
        if (mHooks.mLog)
            mHooks.mLog("LOCKBOOKS pick cancelled");
    }

    // ---- books ---------------------------------------------------------------------------------------------------

    const BookRecord* LockBooksService::book(FormId item) const
    {
        return mReady ? mBooks.book(item) : nullptr;
    }

    std::string LockBooksService::bookTitle(FormId item)
    {
        const BookRecord* b = book(item);
        if (b == nullptr)
            return {};
        const auto cached = mImpl->mTitles.find(item);
        if (cached != mImpl->mTitles.end())
            return cached->second;
        std::string title;
        if (b->mName.mLocalized)
            title = mHooks.mResolve ? mHooks.mResolve(b->mName, b->mPlugin, StringTableKind::Strings) : std::string();
        else
            title = b->mName.mText;
        if (!title.empty())
        {
            // Windows-1252 bytes to UTF-8, markup and runs of white space out (the text renderer's rules)
            const std::vector<std::string> lines = renderBookText(title);
            title = lines.empty() ? std::string() : lines.front();
        }
        if (title.empty() && mHooks.mName)
            title = mHooks.mName(item);
        if (title.empty())
            title = "<" + inventory::formatId(item) + ">";
        if (mImpl->mTitles.size() < kMaxCachedBooks)
            mImpl->mTitles[item] = title;
        return title;
    }

    const std::vector<std::string>& LockBooksService::bookPages(FormId item)
    {
        const BookRecord* b = book(item);
        if (b == nullptr)
            return mImpl->mNoPages;
        const auto cached = mImpl->mPages.find(item);
        if (cached != mImpl->mPages.end())
            return cached->second;
        std::string raw;
        if (b->mDesc.mLocalized)
            raw = mHooks.mResolve ? mHooks.mResolve(b->mDesc, b->mPlugin, StringTableKind::DlStrings) : std::string();
        else
            raw = b->mDesc.mText;
        std::vector<std::string> pages = renderBookText(raw);
        if (mImpl->mPages.size() >= kMaxCachedBooks)
        {
            mImpl->mNoPages = std::move(pages); // the cache is full: the answer is valid until the next call
            return mImpl->mNoPages;
        }
        return mImpl->mPages.emplace(item, std::move(pages)).first->second;
    }

    std::string LockBooksService::bookTeaches(FormId item)
    {
        const BookRecord* b = book(item);
        if (b == nullptr)
            return {};
        if (b->kind() == BookKind::Skill)
        {
            const int slot = b->skillSlot();
            return slot >= 0 && mHooks.mSkillName ? mHooks.mSkillName(static_cast<std::size_t>(slot)) : std::string();
        }
        if (b->kind() == BookKind::Spell)
        {
            std::string name = mHooks.mSpellName ? mHooks.mSpellName(b->mTeaches) : std::string();
            return name.empty() ? inventory::formatId(b->mTeaches) : name;
        }
        return {};
    }

    const std::string& LockBooksService::bookResult(FormId item) const
    {
        const auto it = mImpl->mResults.find(item);
        return it == mImpl->mResults.end() ? mImpl->mNoResult : it->second;
    }

    LockBooksService::ReadStatus LockBooksService::readBook(FormId item)
    {
        if (!mReady)
            return ReadStatus::NotReady;
        const BookRecord* b = mBooks.book(item);
        if (b == nullptr)
            return ReadStatus::NotABook;
        if (mInventory.model().count(inventory::kPlayerOwner, item) <= 0)
            return ReadStatus::NotHeld;
        std::string result;
        const std::string title = bookTitle(item);
        if (b->kind() == BookKind::Skill)
        {
            const int slot = b->skillSlot();
            std::string skill;
            if (slot >= 0 && mHooks.mSkillName)
                skill = mHooks.mSkillName(static_cast<std::size_t>(slot));
            if (mModel.isRead(item))
                result = "You have already read this book.";
            else if (slot < 0 || !mHooks.mRaiseSkill || !mHooks.mRaiseSkill(static_cast<std::size_t>(slot)))
                result = "Nothing is learned from it.";
            else
            {
                (void)mModel.markRead(item);
                result = skill + " skill increased by one level.";
            }
        }
        else if (b->kind() == BookKind::Spell)
        {
            std::string name = mHooks.mSpellName ? mHooks.mSpellName(b->mTeaches) : std::string();
            if (name.empty())
                name = inventory::formatId(b->mTeaches);
            if (mModel.knowsSpell(b->mTeaches))
                result = "You already know " + name + ".";
            else if (mModel.learnSpell(b->mTeaches))
            {
                (void)mModel.markRead(item);
                (void)mInventory.model().consume(inventory::kPlayerOwner, item, 1);
                if (mHooks.mSpellLearned)
                    mHooks.mSpellLearned(b->mTeaches);
                result = "Learned " + name + ".";
            }
            else
                result = "Nothing is learned from it.";
        }
        mImpl->mResults[item] = result;
        if (mHooks.mLog)
            mHooks.mLog("LOCKBOOKS read '" + title + "' (" + std::string(bookKindName(b->kind())) + ")"
                + (result.empty() ? std::string() : ": " + result));
        return ReadStatus::Ok;
    }

    // ---- bindings ------------------------------------------------------------------------------------------------

    openeden::sdk::DeclareResult LockBooksService::declare(openeden::sdk::BindingsService& service)
    {
        Impl& m = *mImpl;
        LockBooksService* self = this;
        static const BindingType s1[] = { BindingType::String };
        static const BindingType beginParams[] = { BindingType::String, BindingType::Int32 };
        static const BindingType inputParams[] = { BindingType::Double, BindingType::Bool, BindingType::Double };

        const auto add = [&](std::string_view name, std::span<const BindingType> params, bool hasResult,
                             BindingType result, BindingAccess access, Fn::Body body,
                             BindingShape resultShape = BindingShape::Single) {
            m.mHandlers.push_back(std::make_unique<Fn>(std::move(body)));
            openeden::sdk::FunctionDescriptor f;
            f.name = name;
            f.params = params;
            f.hasResult = hasResult;
            f.result = result;
            f.access = access;
            f.handler = m.mHandlers.back().get();
            f.resultShape = resultShape;
            m.mFunctions.push_back(f);
        };
        constexpr BindingType kStr = BindingType::String;
        const auto itemOf = [](const BindingValue& v, FormId& out) {
            std::uint64_t id = 0;
            if (!inventory::parseId(v.text, id) || id > 0xFFFFFFFFull)
                return false;
            out = static_cast<FormId>(id);
            return true;
        };
        const auto log = [self](const std::string& line) {
            if (self->mHooks.mLog)
                self->mHooks.mLog(line);
        };
        const auto readBool = [&](std::string_view name, std::function<bool()> value) {
            add(name, {}, true, BindingType::Bool, BindingAccess::Read,
                [value = std::move(value)](std::span<const BindingValue>) {
                    BindingResult r = ok();
                    r.value = boolean(value());
                    return r;
                });
        };
        const auto readText = [&](std::string_view name, std::function<std::string()> value) {
            add(name, {}, true, kStr, BindingAccess::Read,
                [&m, value = std::move(value)](std::span<const BindingValue>) {
                    m.mText = value();
                    BindingResult r = ok();
                    r.value = text(m.mText);
                    return r;
                });
        };
        const auto readNumber = [&](std::string_view name, std::function<double()> value) {
            add(name, {}, true, BindingType::Double, BindingAccess::Read,
                [value = std::move(value)](std::span<const BindingValue>) {
                    BindingResult r = ok();
                    r.value = number(value());
                    return r;
                });
        };
        const auto readInt = [&](std::string_view name, std::function<std::int32_t()> value) {
            add(name, {}, true, BindingType::Int32, BindingAccess::Read,
                [value = std::move(value)](std::span<const BindingValue>) {
                    BindingResult r = ok();
                    r.value = integer(value());
                    return r;
                });
        };
        const auto itemText = [&](std::string_view name, std::function<std::string(FormId)> value) {
            add(name, s1, true, kStr, BindingAccess::Read,
                [&m, itemOf, value = std::move(value)](std::span<const BindingValue> a) {
                    FormId item = 0;
                    if (!itemOf(a[0], item))
                        return fail(BindingStatus::BadArgument, "not an item id");
                    m.mText = value(item);
                    BindingResult r = ok();
                    r.value = text(m.mText);
                    return r;
                });
        };
        const auto stateName = [self, &m]() -> std::string {
            if (m.mRefused)
                return "refused";
            switch (self->mAttempt.state())
            {
                case Attempt::State::Idle:
                    return "none";
                case Attempt::State::Picking:
                    return "picking";
                case Attempt::State::Opened:
                    return "opened";
                case Attempt::State::Broke:
                    return "broke";
                case Attempt::State::Cancelled:
                    return "cancelled";
                case Attempt::State::Refused:
                    return "refused";
            }
            return "none";
        };

        readBool("ready", [self]() { return self->mReady; });
        readInt("revision", [self]() { return static_cast<std::int32_t>(self->mModel.revision() & 0x7FFFFFFF); });
        readInt("picks", [self]() { return self->picks(); });
        readInt("lock_serial", [self]() { return self->mSerial; });
        readText("lock_state", stateName);
        readText("lock_reason", [self]() { return self->mReason; });
        readText("lock_tier", [self]() { return std::string(tierName(self->mAttempt.tier())); });
        readInt("lock_level", [self]() { return self->mAttempt.level(); });
        readNumber("lock_progress", [self]() { return static_cast<double>(self->mAttempt.progress()); });
        readNumber("lock_strain", [self]() { return static_cast<double>(self->mAttempt.strain()); });
        readNumber("lock_window", [self]() { return static_cast<double>(self->mAttempt.window()); });
        readText("lock_placement", [self]() {
            char buffer[24];
            std::snprintf(buffer, sizeof(buffer), "%llx", static_cast<unsigned long long>(self->mAttempt.placement()));
            return std::string(buffer);
        });
        add("lock_picked", s1, true, BindingType::Bool, BindingAccess::Read, [self](std::span<const BindingValue> a) {
            std::uint64_t id = 0;
            if (!inventory::parseId(a[0].text, id))
                return fail(BindingStatus::BadArgument, "not a placement id");
            BindingResult r = ok();
            r.value = boolean(self->mModel.isPicked(id));
            return r;
        });
        add("book_known", s1, true, BindingType::Bool, BindingAccess::Read,
            [self, itemOf](std::span<const BindingValue> a) {
                FormId item = 0;
                if (!itemOf(a[0], item))
                    return fail(BindingStatus::BadArgument, "not an item id");
                BindingResult r = ok();
                r.value = boolean(self->book(item) != nullptr);
                return r;
            });
        add("book_read", s1, true, BindingType::Bool, BindingAccess::Read,
            [self, itemOf](std::span<const BindingValue> a) {
                FormId item = 0;
                if (!itemOf(a[0], item))
                    return fail(BindingStatus::BadArgument, "not an item id");
                BindingResult r = ok();
                r.value = boolean(self->mModel.isRead(item));
                return r;
            });
        itemText("book_title", [self](FormId item) { return self->bookTitle(item); });
        itemText("book_kind", [self](FormId item) {
            const BookRecord* b = self->book(item);
            return std::string(bookKindName(b != nullptr ? b->kind() : BookKind::Plain));
        });
        itemText("book_teaches", [self](FormId item) { return self->bookTeaches(item); });
        itemText("book_result", [self](FormId item) { return self->bookResult(item); });
        add("book_pages", s1, true, kStr, BindingAccess::Read,
            [self, &m, itemOf](std::span<const BindingValue> a) {
                FormId item = 0;
                if (!itemOf(a[0], item))
                    return fail(BindingStatus::BadArgument, "not an item id");
                const std::vector<std::string>& pages = self->bookPages(item);
                m.mStrings.assign(pages.begin(), pages.end());
                if (m.mStrings.size() > openeden::sdk::kMaxBindingArrayElements)
                    m.mStrings.resize(openeden::sdk::kMaxBindingArrayElements);
                m.mElements.clear();
                for (const std::string& s : m.mStrings)
                    m.mElements.push_back(text(s));
                BindingResult r = ok();
                r.value.type = kStr;
                r.value.elements = m.mElements.data();
                r.value.count = m.mElements.size();
                return r;
            },
            BindingShape::Array);
        add("spells", {}, true, kStr, BindingAccess::Read, [self, &m](std::span<const BindingValue>) {
            m.mStrings.clear();
            for (const FormId spell : self->mModel.spells())
                m.mStrings.push_back(inventory::formatId(spell));
            if (m.mStrings.size() > openeden::sdk::kMaxBindingArrayElements)
                m.mStrings.resize(openeden::sdk::kMaxBindingArrayElements);
            m.mElements.clear();
            for (const std::string& s : m.mStrings)
                m.mElements.push_back(text(s));
            BindingResult r = ok();
            r.value.type = kStr;
            r.value.elements = m.mElements.data();
            r.value.count = m.mElements.size();
            return r;
        }, BindingShape::Array);

        // ---- Mutate: no result; a refusal is logged
        add("pick_begin", beginParams, false, BindingType::Bool, BindingAccess::Mutate,
            [self, log](std::span<const BindingValue> a) {
                std::uint64_t placement = 0;
                if (!inventory::parseId(a[0].text, placement))
                {
                    log("LOCKBOOKS pick refused: bad placement id");
                    return ok();
                }
                (void)self->pickBegin(placement, a[1].integer);
                return ok();
            });
        add("pick_input", inputParams, false, BindingType::Bool, BindingAccess::Mutate,
            [self](std::span<const BindingValue> a) {
                self->pickInput(
                    static_cast<float>(a[0].number), a[1].boolean, static_cast<float>(a[2].number / 1000.0));
                return ok();
            });
        add("pick_cancel", {}, false, BindingType::Bool, BindingAccess::Mutate, [self](std::span<const BindingValue>) {
            self->pickCancel();
            return ok();
        });
        add("read_book", s1, false, BindingType::Bool, BindingAccess::Mutate,
            [self, itemOf, log](std::span<const BindingValue> a) {
                FormId item = 0;
                if (!itemOf(a[0], item))
                {
                    log("LOCKBOOKS read refused: bad item id");
                    return ok();
                }
                const ReadStatus s = self->readBook(item);
                if (s != ReadStatus::Ok)
                    log("LOCKBOOKS read refused: " + inventory::formatId(item)
                        + (s == ReadStatus::NotReady ? " (not ready)"
                                : s == ReadStatus::NotHeld ? " (not held)"
                                                           : " (not a book)"));
                return ok();
            });

        openeden::sdk::NamespaceDescriptor ns;
        ns.name = "skyrimse.lockbooks";
        ns.version = { 1, 0 };
        ns.functions = m.mFunctions;
        return service.declareNamespace(ns);
    }
}
