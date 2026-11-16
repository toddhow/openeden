#include "game_skyrimse/worldbooks.hpp"

// Books lying in the world and their script bindings: see the header (task 0234).

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

        /// BOOK DATA flag bit 2: the book cannot be taken (the Dragonborn black books and a few more).
        constexpr std::uint8_t kCannotTakeFlag = 0x02;
    }

    struct WorldBooksService::Impl
    {
        std::vector<std::unique_ptr<Fn>> mHandlers;
        std::vector<openeden::sdk::FunctionDescriptor> mFunctions;
    };

    WorldBooksService::WorldBooksService(LockBooksService& books, WorldBooksHooks hooks)
        : mImpl(std::make_unique<Impl>())
        , mBooks(books)
        , mHooks(std::move(hooks))
    {
    }

    WorldBooksService::~WorldBooksService() = default;

    bool WorldBooksService::isBook(FormId item) const
    {
        return mBooks.ready() && mBooks.book(item) != nullptr;
    }

    bool WorldBooksService::canTake(FormId item) const
    {
        const BookRecord* b = mBooks.ready() ? mBooks.book(item) : nullptr;
        return b != nullptr && (b->mFlags & kCannotTakeFlag) == 0;
    }

    bool WorldBooksService::read(std::uint64_t placement, FormId item)
    {
        const LockBooksService::ReadStatus status = mBooks.readWorldBook(item);
        if (status != LockBooksService::ReadStatus::Ok)
        {
            if (mHooks.mLog)
                mHooks.mLog("WORLDBOOKS read of " + inventory::formatId(item) + " refused ("
                    + (status == LockBooksService::ReadStatus::NotReady ? "not ready" : "not a book") + ")");
            return false;
        }
        ++mReadCount;
        if (mHooks.mLog)
            mHooks.mLog("WORLDBOOKS placement " + inventory::formatId(placement) + " read");
        if (mHooks.mRead)
            mHooks.mRead(placement);
        return true;
    }

    openeden::sdk::DeclareResult WorldBooksService::declare(openeden::sdk::BindingsService& service)
    {
        Impl& m = *mImpl;
        WorldBooksService* self = this;
        static const BindingType s1[] = { BindingType::String };
        static const BindingType s2[] = { BindingType::String, BindingType::String };
        const auto add = [&](std::string_view name, std::span<const BindingType> params, bool hasResult,
                             BindingType result, BindingAccess access, Fn::Body body) {
            m.mHandlers.push_back(std::make_unique<Fn>(std::move(body)));
            openeden::sdk::FunctionDescriptor f;
            f.name = name;
            f.params = params;
            f.hasResult = hasResult;
            f.result = result;
            f.access = access;
            f.handler = m.mHandlers.back().get();
            f.resultShape = BindingShape::Single;
            m.mFunctions.push_back(f);
        };
        const auto itemOf = [](const BindingValue& v, FormId& out) {
            std::uint64_t id = 0;
            if (!inventory::parseId(v.text, id) || id > 0xFFFFFFFFull)
                return false;
            out = static_cast<FormId>(id);
            return true;
        };
        const auto readBool = [&](std::string_view name, bool (WorldBooksService::*query)(FormId) const) {
            add(name, s1, true, BindingType::Bool, BindingAccess::Read,
                [self, query, itemOf](std::span<const BindingValue> a) {
                    FormId item = 0;
                    if (!itemOf(a[0], item))
                        return fail(BindingStatus::BadArgument, "not an item id");
                    BindingResult r = ok();
                    r.value.type = BindingType::Bool;
                    r.value.boolean = (self->*query)(item);
                    return r;
                });
        };
        readBool("is_book", &WorldBooksService::isBook);
        readBool("can_take", &WorldBooksService::canTake);
        add("read_count", {}, true, BindingType::Int32, BindingAccess::Read, [self](std::span<const BindingValue>) {
            BindingResult r = ok();
            r.value.type = BindingType::Int32;
            r.value.integer = static_cast<std::int32_t>(self->mReadCount);
            return r;
        });
        add("read", s2, false, BindingType::Bool, BindingAccess::Mutate,
            [self, itemOf](std::span<const BindingValue> a) {
                std::uint64_t placement = 0;
                FormId item = 0;
                if (!inventory::parseId(a[0].text, placement) || !itemOf(a[1], item))
                {
                    if (self->mHooks.mLog)
                        self->mHooks.mLog("WORLDBOOKS read refused: bad id");
                    return ok();
                }
                (void)self->read(placement, item);
                return ok();
            });

        openeden::sdk::NamespaceDescriptor ns;
        ns.name = "skyrimse.worldbooks";
        ns.version = { 1, 0 };
        ns.functions = m.mFunctions;
        return service.declareNamespace(ns);
    }
}
