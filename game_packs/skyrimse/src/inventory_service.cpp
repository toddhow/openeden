#include "game_skyrimse/inventory_service.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>

// The inventory service and its bindings: see the header (task 0182).

namespace game_skyrimse::inventory
{
    using openeden::sdk::BindingAccess;
    using openeden::sdk::BindingResult;
    using openeden::sdk::BindingShape;
    using openeden::sdk::BindingStatus;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    std::string formatId(std::uint64_t id)
    {
        char buffer[24];
        std::snprintf(buffer, sizeof(buffer), "%08llx", static_cast<unsigned long long>(id));
        return buffer;
    }

    bool parseId(std::string_view text, std::uint64_t& out)
    {
        if (text.empty() || text.size() > 16)
            return false;
        std::uint64_t value = 0;
        for (const char c : text)
        {
            int digit;
            if (c >= '0' && c <= '9')
                digit = c - '0';
            else if (c >= 'a' && c <= 'f')
                digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                digit = c - 'A' + 10;
            else
                return false;
            value = (value << 4) | static_cast<std::uint64_t>(digit);
        }
        out = value;
        return true;
    }

    bool parseOwner(std::string_view text, std::uint64_t& out)
    {
        if (text == "player")
        {
            out = kPlayerOwner;
            return true;
        }
        return parseId(text, out) && out != kPlayerOwner;
    }

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
    }

    struct InventoryService::Impl
    {
        std::vector<std::unique_ptr<Fn>> mHandlers;
        std::vector<openeden::sdk::FunctionDescriptor> mFunctions;
        // result buffers: valid until the handler is called again
        std::string mText;
        std::vector<std::string> mStrings;
        std::vector<BindingValue> mElements;
    };

    InventoryService::InventoryService(Hooks hooks, float startingGold, std::uint16_t level)
        : mImpl(std::make_unique<Impl>())
        , mHooks(std::move(hooks))
        , mChunk(mModel)
        , mLevel(level)
    {
        std::vector<Stack> start;
        if (startingGold >= 1.f)
            start.push_back({ kGoldItem, static_cast<std::int32_t>(std::min(startingGold, 1.0e6f)) });
        mModel.setPlayer(std::move(start));
    }

    InventoryService::~InventoryService()
    {
        if (mThread.joinable())
            mThread.join();
    }

    bool InventoryService::poll()
    {
        if (mReady)
            return true;
        const int state = mState.load(std::memory_order_acquire);
        if (state == 0)
        {
            auto inputs = std::make_shared<Inputs>();
            if (!mHooks.mInputs || !mHooks.mInputs(*inputs))
                return false;
            mState.store(1, std::memory_order_release);
            mThread = std::thread([this, inputs]() {
                const auto started = std::chrono::steady_clock::now();
                ItemTable table;
                try
                {
                    const InventoryLimits limits;
                    for (const std::filesystem::path& path : inputs->mPaths)
                    {
                        Content::Error openError;
                        const std::unique_ptr<Content::Plugin> file = Content::Plugin::openFile(path, openError);
                        if (file == nullptr)
                            continue;
                        InventoryRecords records;
                        (void)parseInventoryRecords(*file, inputs->mOrder, limits, records);
                        table.add(std::move(records));
                    }
                    table.addLeveledLists(inputs->mLeveled);
                }
                catch (...)
                {
                    // what was read stays: the table is then partial, never undefined
                }
                mBuilt = std::move(table);
                mMillis = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started)
                                                         .count());
                mState.store(2, std::memory_order_release);
            });
            return false;
        }
        if (state == 1)
            return false;
        mThread.join();
        mTable = std::move(mBuilt);
        mReady = true;
        if (mHooks.mLog)
            mHooks.mLog("INV item table built on a worker in " + std::to_string(mMillis) + " ms: "
                + std::to_string(mTable.itemCount()) + " items, " + std::to_string(mTable.containerCount())
                + " containers, " + std::to_string(mTable.leveledCount()) + " leveled lists, "
                + std::to_string(mTable.oddSubrecords()) + " odd subrecords");
        return true;
    }

    bool InventoryService::open(std::uint64_t owner, FormId base, std::string_view kind)
    {
        if (!poll() || owner == kPlayerOwner)
            return false;
        if (mModel.hasOwner(owner))
            return true;
        std::vector<InventoryEntry> entries;
        if (kind == "container")
        {
            const ContainerStat* c = mTable.container(base);
            if (c == nullptr)
                return false;
            entries = c->mItems;
        }
        else if (kind == "actor")
        {
            if (!mHooks.mNpcContents || !mHooks.mNpcContents(base, entries))
                return false;
        }
        else
            return false;
        ResolveReport report;
        std::vector<Stack> stacks = resolveContents(entries, mTable, contentsSeed(owner), mLevel, &report);
        if (mHooks.mLog)
            mHooks.mLog("INV open " + std::string(kind) + " " + formatId(owner) + " base " + formatId(base) + ": "
                + std::to_string(stacks.size()) + " stacks, " + std::to_string(report.mUnknownForms)
                + " unknown forms");
        return mModel.ensureOwner(owner, std::move(stacks));
    }

    openeden::sdk::DeclareResult InventoryService::declare(openeden::sdk::BindingsService& service)
    {
        Impl& m = *mImpl;
        InventoryService* self = this;
        static const BindingType s1[] = { BindingType::String };
        static const BindingType s2[] = { BindingType::String, BindingType::String };
        static const BindingType s3[] = { BindingType::String, BindingType::String, BindingType::String };
        static const BindingType transferParams[]
            = { BindingType::String, BindingType::String, BindingType::String, BindingType::Int32 };

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

        const auto owner = [](const BindingValue& v, std::uint64_t& out) { return parseOwner(v.text, out); };
        const auto itemOf = [](const BindingValue& v, FormId& out) {
            std::uint64_t id = 0;
            if (!parseId(v.text, id) || id > 0xFFFFFFFFull)
                return false;
            out = static_cast<FormId>(id);
            return true;
        };
        const auto nameOf = [self](FormId id) {
            std::string n = self->mHooks.mName ? self->mHooks.mName(id) : std::string();
            return n.empty() ? "<" + formatId(id) + ">" : n;
        };
        constexpr BindingType kStr = BindingType::String;

        add("ready", {}, true, BindingType::Bool, BindingAccess::Read, [self](std::span<const BindingValue>) {
            BindingResult r = ok();
            r.value = boolean(self->mReady);
            return r;
        });
        add("revision", {}, true, BindingType::Int32, BindingAccess::Read, [self](std::span<const BindingValue>) {
            BindingResult r = ok();
            r.value = integer(static_cast<std::int32_t>(self->mModel.revision() & 0x7FFFFFFF));
            return r;
        });
        add("known", s1, true, BindingType::Bool, BindingAccess::Read,
            [self, owner](std::span<const BindingValue> a) {
                std::uint64_t o = 0;
                if (!owner(a[0], o))
                    return fail(BindingStatus::BadArgument, "not an owner id");
                BindingResult r = ok();
                r.value = boolean(self->mModel.hasOwner(o));
                return r;
            });
        add("items", s1, true, kStr, BindingAccess::Read,
            [self, &m, owner, nameOf](std::span<const BindingValue> a) {
                std::uint64_t o = 0;
                if (!owner(a[0], o))
                    return fail(BindingStatus::BadArgument, "not an owner id");
                std::vector<Stack> stacks = self->mModel.stacks(o);
                struct Row
                {
                    int category;
                    std::string name;
                    FormId id;
                };
                std::vector<Row> rows;
                rows.reserve(stacks.size());
                for (const Stack& s : stacks)
                {
                    const ItemStat* stat = self->mTable.item(s.mItem);
                    std::string n = nameOf(s.mItem);
                    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return std::tolower(c); });
                    rows.push_back({ stat != nullptr ? static_cast<int>(stat->mCategory) : 99, std::move(n), s.mItem });
                }
                std::sort(rows.begin(), rows.end(), [](const Row& x, const Row& y) {
                    if (x.category != y.category)
                        return x.category < y.category;
                    if (x.name != y.name)
                        return x.name < y.name;
                    return x.id < y.id;
                });
                if (rows.size() > openeden::sdk::kMaxBindingArrayElements)
                    rows.resize(openeden::sdk::kMaxBindingArrayElements);
                m.mStrings.clear();
                m.mElements.clear();
                for (const Row& row : rows)
                    m.mStrings.push_back(formatId(row.id));
                for (const std::string& s : m.mStrings)
                    m.mElements.push_back(text(s));
                BindingResult r = ok();
                r.value.type = kStr;
                r.value.elements = m.mElements.data();
                r.value.count = m.mElements.size();
                return r;
            },
            BindingShape::Array);
        const BindingType s2Types[] = { kStr, kStr };
        (void)s2Types;
        add("count", s2, true, BindingType::Int32, BindingAccess::Read,
            [self, owner, itemOf](std::span<const BindingValue> a) {
                std::uint64_t o = 0;
                FormId item = 0;
                if (!owner(a[0], o) || !itemOf(a[1], item))
                    return fail(BindingStatus::BadArgument, "not an id");
                BindingResult r = ok();
                r.value = integer(self->mModel.count(o, item));
                return r;
            });
        add("name", s1, true, kStr, BindingAccess::Read, [&m, itemOf, nameOf](std::span<const BindingValue> a) {
            FormId item = 0;
            if (!itemOf(a[0], item))
                return fail(BindingStatus::BadArgument, "not an item id");
            m.mText = nameOf(item);
            BindingResult r = ok();
            r.value = text(m.mText);
            return r;
        });
        add("category", s1, true, kStr, BindingAccess::Read, [self, &m, itemOf](std::span<const BindingValue> a) {
            FormId item = 0;
            if (!itemOf(a[0], item))
                return fail(BindingStatus::BadArgument, "not an item id");
            const ItemStat* stat = self->mTable.item(item);
            m.mText = stat != nullptr ? std::string(categoryName(stat->mCategory)) : std::string("misc");
            BindingResult r = ok();
            r.value = text(m.mText);
            return r;
        });
        add("weight", s1, true, BindingType::Double, BindingAccess::Read,
            [self, itemOf](std::span<const BindingValue> a) {
                FormId item = 0;
                if (!itemOf(a[0], item))
                    return fail(BindingStatus::BadArgument, "not an item id");
                const ItemStat* stat = self->mTable.item(item);
                BindingResult r = ok();
                r.value = number(stat != nullptr ? stat->mWeight : 0.0);
                return r;
            });
        add("value", s1, true, BindingType::Int32, BindingAccess::Read,
            [self, itemOf](std::span<const BindingValue> a) {
                FormId item = 0;
                if (!itemOf(a[0], item))
                    return fail(BindingStatus::BadArgument, "not an item id");
                const ItemStat* stat = self->mTable.item(item);
                BindingResult r = ok();
                r.value = integer(stat != nullptr ? stat->mValue : 0);
                return r;
            });
        add("gold_item", {}, true, kStr, BindingAccess::Read, [&m](std::span<const BindingValue>) {
            m.mText = formatId(kGoldItem);
            BindingResult r = ok();
            r.value = text(m.mText);
            return r;
        });
        add("carried", {}, true, BindingType::Double, BindingAccess::Read, [self](std::span<const BindingValue>) {
            BindingResult r = ok();
            r.value = number(self->mModel.weightOf(kPlayerOwner, self->mTable));
            return r;
        });
        add("capacity", {}, true, BindingType::Double, BindingAccess::Read, [](std::span<const BindingValue>) {
            BindingResult r = ok();
            r.value = number(kBaseCarryWeight);
            return r;
        });
        // ---- Mutate: no result; a refusal is logged
        add("open", s3, false, BindingType::Bool, BindingAccess::Mutate, [self](std::span<const BindingValue> a) {
            std::uint64_t o = 0, b = 0;
            if (!parseOwner(a[0].text, o) || !parseId(a[1].text, b) || b > 0xFFFFFFFFull)
            {
                if (self->mHooks.mLog)
                    self->mHooks.mLog("INV open refused: bad id");
                return ok();
            }
            if (!self->open(o, static_cast<FormId>(b), a[2].text) && self->mHooks.mLog)
                self->mHooks.mLog("INV open refused: owner " + formatId(o) + " kind " + std::string(a[2].text)
                    + " (not ready or unknown)");
            return ok();
        });
        add("transfer", transferParams, false, BindingType::Bool, BindingAccess::Mutate,
            [self, owner, itemOf](std::span<const BindingValue> a) {
                std::uint64_t from = 0, to = 0;
                FormId item = 0;
                if (!owner(a[0], from) || !owner(a[1], to) || !itemOf(a[2], item))
                {
                    if (self->mHooks.mLog)
                        self->mHooks.mLog("INV transfer refused: bad id");
                    return ok();
                }
                const TransferStatus s = self->mModel.transfer(from, to, item, a[3].integer);
                if (self->mHooks.mLog)
                    self->mHooks.mLog("INV transfer " + formatId(from) + " -> " + formatId(to) + " item "
                        + formatId(item) + " x" + std::to_string(a[3].integer) + ": "
                        + std::string(transferStatusName(s)) + ", revision " + std::to_string(self->mModel.revision()));
                return ok();
            });
        add("take_all", s2, false, BindingType::Bool, BindingAccess::Mutate,
            [self, owner](std::span<const BindingValue> a) {
                std::uint64_t from = 0, to = 0;
                if (!owner(a[0], from) || !owner(a[1], to))
                    return ok();
                std::int64_t moved = 0;
                const TransferStatus s = self->mModel.transferAll(from, to, moved);
                if (self->mHooks.mLog)
                    self->mHooks.mLog("INV take_all " + formatId(from) + " -> " + formatId(to) + ": "
                        + std::string(transferStatusName(s)) + ", " + std::to_string(moved) + " items, revision "
                        + std::to_string(self->mModel.revision()));
                return ok();
            });

        openeden::sdk::NamespaceDescriptor ns;
        ns.name = "skyrimse.inventory";
        ns.version = { 1, 0 };
        ns.functions = m.mFunctions;
        return service.declareNamespace(ns);
    }
}
