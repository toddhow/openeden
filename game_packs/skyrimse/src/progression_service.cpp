#include "game_skyrimse/progression_service.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <span>

#include "game_skyrimse/race_effects.hpp"

// The progression service and its bindings: see the header (task 0200).

namespace game_skyrimse::progression
{
    using openeden::sdk::BindingAccess;
    using openeden::sdk::BindingResult;
    using openeden::sdk::BindingStatus;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    namespace
    {
        constexpr std::uint32_t kChunkVersion = 1;
        constexpr std::size_t kMaxChunkBytes = 64 * 1024;

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

        BindingValue integer(std::int64_t v)
        {
            BindingValue b;
            b.type = BindingType::Int32;
            b.integer = static_cast<std::int32_t>(std::clamp<std::int64_t>(v, 0, 0x7FFFFFFF));
            return b;
        }

        BindingValue boolean(bool v)
        {
            BindingValue b;
            b.type = BindingType::Bool;
            b.boolean = v;
            return b;
        }

        BindingValue text(std::string_view v)
        {
            BindingValue b;
            b.type = BindingType::String;
            b.text = v;
            return b;
        }

        std::string formatId(FormId id)
        {
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "%08x", id);
            return buffer;
        }

        bool parseId(std::string_view textId, FormId& out)
        {
            if (textId.size() != 8)
                return false;
            std::uint32_t value = 0;
            for (const char c : textId)
            {
                value <<= 4;
                if (c >= '0' && c <= '9')
                    value |= static_cast<std::uint32_t>(c - '0');
                else if (c >= 'a' && c <= 'f')
                    value |= static_cast<std::uint32_t>(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F')
                    value |= static_cast<std::uint32_t>(c - 'A' + 10);
                else
                    return false;
            }
            out = value;
            return true;
        }

        class Chunk final : public openeden::sdk::SaveChunkHandler
        {
        public:
            explicit Chunk(ProgressionService& service)
                : mService(service)
            {
            }
            std::string_view chunkName() const override { return "progression"; }
            std::uint32_t chunkVersion() const override { return kChunkVersion; }
            void write(openeden::sdk::ChunkWriter& writer) override { mService.writeChunk(writer); }
            bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override
            {
                return mService.readChunk(reader, version);
            }

        private:
            ProgressionService& mService;
        };
    }

    std::string prettyPerkName(const std::string& editorId)
    {
        std::string base = editorId;
        while (!base.empty() && base.back() >= '0' && base.back() <= '9')
            base.pop_back();
        if (base.empty())
            base = editorId;
        std::string out;
        for (std::size_t i = 0; i < base.size(); ++i)
        {
            const char c = base[i];
            if (i > 0 && c >= 'A' && c <= 'Z' && !(base[i - 1] >= 'A' && base[i - 1] <= 'Z'))
                out.push_back(' ');
            out.push_back(c);
        }
        return out;
    }

    struct ProgressionService::Impl
    {
        std::vector<std::unique_ptr<Fn>> mHandlers;
        std::vector<openeden::sdk::FunctionDescriptor> mFunctions;
        std::vector<std::string> mStrings;
        std::vector<BindingValue> mElements;
        std::string mText;
        std::unique_ptr<Chunk> mChunk;
    };

    ProgressionService::ProgressionService(ServiceHooks hooks)
        : mImpl(std::make_unique<Impl>())
        , mHooks(std::move(hooks))
    {
        mImpl->mChunk = std::make_unique<Chunk>(*this);
    }

    ProgressionService::~ProgressionService() = default;

    openeden::sdk::SaveChunkHandler& ProgressionService::chunk()
    {
        return *mImpl->mChunk;
    }

    bool ProgressionService::poll()
    {
        if (mProgression == nullptr)
        {
            if (!mHooks.mInputs)
                return false;
            Inputs inputs;
            if (!mHooks.mInputs(inputs) || inputs.mSkills == nullptr || !inputs.mSkills->complete())
                return false;
            mInputs = inputs;
            mProgression = std::make_unique<PlayerProgression>(*mInputs.mSkills, mInputs.mPerks, mInputs.mSettings);
            mProgression->setStart(mHooks.mStartLevel, mHooks.mStartSkill);
            if (mHavePendingRestore)
            {
                std::string error;
                if (!mProgression->restore(mPendingRestore, error) && mHooks.mLog)
                    mHooks.mLog("PROGRESSION the saved progression was refused: " + error);
                mHavePendingRestore = false;
                mPendingRestore.clear();
            }
            if (mHooks.mLog)
                mHooks.mLog("PROGRESSION ready: level " + std::to_string(mProgression->level()) + ", "
                    + std::to_string(inputs.mSkills->perkNodeCount()) + " perk nodes");
        }
        if (mHooks.mLevelUp)
            for (const std::uint16_t level : mProgression->takeLevelUps())
                mHooks.mLevelUp(level);
        return true;
    }

    bool ProgressionService::raiseSkill(std::size_t slot)
    {
        if (mProgression == nullptr || slot >= kSkillCount || mInputs.mSkills == nullptr)
            return false;
        const SkillRecord* record = mInputs.mSkills->skill(slot);
        if (record == nullptr || !(record->mUseMult > 0.f) || mProgression->skill(slot) >= kMaxSkill)
            return false;
        // useSkill adds mUseMult x amount + mUseOffset experience; ask for what the next level still needs, and a
        // little more on each of a few tries so that float rounding never leaves it one short.
        const std::uint8_t before = mProgression->skill(slot);
        float scale = 1.f;
        for (int attempt = 0; attempt < 4 && mProgression->skill(slot) == before; ++attempt, scale += 0.01f)
        {
            const float missing = std::max(mProgression->skillNeed(slot) - mProgression->skillXp(slot), 0.f);
            const float amount = (missing * scale - record->mUseOffset) / record->mUseMult;
            skillUsed(slot, std::max(amount, 1.0e-3f));
        }
        return mProgression->skill(slot) > before;
    }

    void ProgressionService::skillUsed(std::size_t slot, float amount)
    {
        if (mProgression == nullptr)
            return;
        const std::uint8_t before = mProgression->skill(slot);
        float gain = mGain ? mGain() : 1.f;
        if (!std::isfinite(gain) || !(gain > 0.f))
            gain = 1.f;
        if (mSkillScale)
        {
            const float scale = mSkillScale(slot); // task 0237
            if (std::isfinite(scale) && scale > 0.f)
                gain *= scale;
        }
        const std::uint32_t increases = mProgression->useSkill(slot, amount * gain);
        if (increases > 0 && mHooks.mLog)
            mHooks.mLog("PROGRESSION skill " + std::string(skillName(slot)) + " " + std::to_string(before) + " -> "
                + std::to_string(mProgression->skill(slot)) + " (level xp "
                + std::to_string(static_cast<int>(mProgression->levelXp())) + " of "
                + std::to_string(static_cast<int>(mProgression->levelNeed())) + ")");
    }

    bool ProgressionService::actorValue(std::uint32_t index, float& out) const
    {
        if (mProgression == nullptr || index < kFirstSkillValue || index >= kFirstSkillValue + kSkillCount)
            return false;
        out = static_cast<float>(mProgression->skill(index - kFirstSkillValue));
        return true;
    }

    void ProgressionService::setSkillModifiers(
        const std::array<float, kSkillCount>& points, const std::array<float, kSkillCount>& power)
    {
        mSkillPoints = points;
        mSkillPower = power;
    }

    bool ProgressionService::effectiveActorValue(std::uint32_t index, float& out) const
    {
        float base = 0.f;
        if (!actorValue(index, base))
            return false;
        const std::size_t slot = index - kFirstSkillValue;
        out = race_effects::effectiveSkill(base, mSkillPoints[slot], mSkillPower[slot]);
        return true;
    }

    bool ProgressionService::chooseLevelUp(Vital vital)
    {
        if (mProgression == nullptr)
            return false;
        const bool done = mProgression->chooseLevelUp(vital);
        mLastResult = done ? "Level " + std::to_string(mProgression->level()) : "not enough experience for a level up";
        if (mHooks.mLog)
            mHooks.mLog("PROGRESSION choose level up: " + mLastResult);
        (void)poll(); // raises the level hook now
        return done;
    }

    bool ProgressionService::buyPerk(FormId firstRank)
    {
        if (mProgression == nullptr)
            return false;
        std::string why;
        const bool done = mProgression->buyPerk(firstRank, why);
        mLastResult = done ? "perk bought" : why;
        if (mHooks.mLog)
            mHooks.mLog("PROGRESSION buy perk " + formatId(firstRank) + ": " + mLastResult + " (points left "
                + std::to_string(mProgression->perkPoints()) + ")");
        return done;
    }

    void ProgressionService::writeChunk(openeden::sdk::ChunkWriter& writer)
    {
        std::vector<std::uint8_t> bytes;
        if (mProgression != nullptr)
            bytes = mProgression->serialise();
        else if (mHavePendingRestore)
            bytes = mPendingRestore;
        writer.writeValue(static_cast<std::uint32_t>(bytes.size()));
        if (!bytes.empty())
            writer.write(bytes.data(), bytes.size());
    }

    bool ProgressionService::readChunk(openeden::sdk::ChunkReader& reader, std::uint32_t version)
    {
        mRefusal.clear();
        if (version != kChunkVersion)
        {
            mRefusal = "progression chunk: version " + std::to_string(version) + " is not understood";
            return false;
        }
        std::uint32_t size = 0;
        if (!reader.readValue(size) || size > kMaxChunkBytes || reader.remaining() != size)
        {
            mRefusal = "progression chunk: bad length";
            return false;
        }
        std::vector<std::uint8_t> bytes(size);
        if (size > 0 && !reader.read(bytes.data(), size))
        {
            mRefusal = "progression chunk: cut short";
            return false;
        }
        if (size == 0)
        {
            // a save made before the progression existed: a new character (also when a game is already running)
            if (mProgression != nullptr)
                mProgression->reset(mHooks.mStartLevel, mHooks.mStartSkill);
            return true;
        }
        if (mProgression != nullptr)
        {
            if (!mProgression->restore(bytes, mRefusal))
                return false;
            (void)mProgression->takeLevelUps(); // a loaded level is not a new one
            return true;
        }
        mPendingRestore = std::move(bytes);
        mHavePendingRestore = true;
        return true;
    }

    openeden::sdk::DeclareResult ProgressionService::declare(openeden::sdk::BindingsService& service)
    {
        Impl& m = *mImpl;
        ProgressionService* self = this;
        static const BindingType i1[] = { BindingType::Int32 };
        static const BindingType s1[] = { BindingType::String };
        constexpr BindingType kStr = BindingType::String;

        const auto add = [&](std::string_view name, std::span<const BindingType> params, bool hasResult,
                             BindingType result, BindingAccess access, Fn::Body body,
                             openeden::sdk::BindingShape shape = openeden::sdk::BindingShape::Single) {
            m.mHandlers.push_back(std::make_unique<Fn>(std::move(body)));
            openeden::sdk::FunctionDescriptor f;
            f.name = name;
            f.params = params;
            f.hasResult = hasResult;
            f.result = result;
            f.access = access;
            f.handler = m.mHandlers.back().get();
            f.resultShape = shape;
            m.mFunctions.push_back(f);
        };
        const auto intFn = [&](std::string_view name, std::function<std::int64_t(const PlayerProgression&)> get) {
            add(name, {}, true, BindingType::Int32, BindingAccess::Read,
                [self, get = std::move(get)](std::span<const BindingValue>) {
                    (void)self->poll();
                    BindingResult r = ok();
                    r.value = integer(self->progression() != nullptr ? get(*self->progression()) : 0);
                    return r;
                });
        };
        using SlotGetter = std::function<std::int64_t(const PlayerProgression&, std::size_t)>;
        const auto slotFn = [&](std::string_view name, SlotGetter get) {
            add(name, i1, true, BindingType::Int32, BindingAccess::Read,
                [self, get = std::move(get)](std::span<const BindingValue> a) {
                    if (a[0].integer < 0 || a[0].integer >= static_cast<std::int32_t>(kSkillCount))
                        return fail(BindingStatus::BadArgument, "not a skill slot");
                    BindingResult r = ok();
                    r.value = integer(self->progression() != nullptr
                            ? get(*self->progression(), static_cast<std::size_t>(a[0].integer))
                            : 0);
                    return r;
                });
        };
        const auto perkOf = [](const BindingValue& v, FormId& out) { return parseId(v.text, out); };

        add("ready", {}, true, BindingType::Bool, BindingAccess::Read, [self](std::span<const BindingValue>) {
            BindingResult r = ok();
            r.value = boolean(self->poll());
            return r;
        });
        intFn("revision",
            [](const PlayerProgression& p) { return static_cast<std::int64_t>(p.revision() & 0x7FFFFFFF); });
        intFn("level", [](const PlayerProgression& p) { return static_cast<std::int64_t>(p.level()); });
        intFn("level_xp", [](const PlayerProgression& p) { return static_cast<std::int64_t>(p.levelXp()); });
        intFn("level_need", [](const PlayerProgression& p) { return static_cast<std::int64_t>(p.levelNeed()); });
        intFn("level_up_ready", [](const PlayerProgression& p) { return p.levelUpReady() ? 1 : 0; });
        intFn("perk_points", [](const PlayerProgression& p) { return static_cast<std::int64_t>(p.perkPoints()); });
        intFn("skill_count", [](const PlayerProgression&) { return static_cast<std::int64_t>(kSkillCount); });
        slotFn("skill_level",
            [](const PlayerProgression& p, std::size_t s) { return static_cast<std::int64_t>(p.skill(s)); });
        slotFn("skill_xp",
            [](const PlayerProgression& p, std::size_t s) { return static_cast<std::int64_t>(p.skillXp(s)); });
        slotFn("skill_need",
            [](const PlayerProgression& p, std::size_t s) { return static_cast<std::int64_t>(p.skillNeed(s)); });
        add("skill_name", i1, true, kStr, BindingAccess::Read, [](std::span<const BindingValue> a) {
            BindingResult r = ok();
            r.value = text(a[0].integer >= 0 ? skillName(static_cast<std::size_t>(a[0].integer)) : std::string_view());
            return r;
        });
        add("vital_bonus", s1, true, BindingType::Int32, BindingAccess::Read, [self](std::span<const BindingValue> a) {
            BindingResult r = ok();
            const std::string_view name = a[0].text;
            const PlayerProgression* p = self->progression();
            const Vital vital = name == "magicka" ? Vital::Magicka : name == "stamina" ? Vital::Stamina : Vital::Health;
            r.value = integer(p != nullptr ? static_cast<std::int64_t>(p->vitalBonus(vital)) : 0);
            return r;
        });
        add("last_result", {}, true, kStr, BindingAccess::Read, [self, &m](std::span<const BindingValue>) {
            m.mText = self->lastResult();
            BindingResult r = ok();
            r.value = text(m.mText);
            return r;
        });
        add(
            "perks", i1, true, kStr, BindingAccess::Read,
            [self, &m](std::span<const BindingValue> a) {
                m.mStrings.clear();
                if (self->progression() != nullptr && a[0].integer >= 0)
                    for (const NodeView& node : self->progression()->nodes(static_cast<std::size_t>(a[0].integer)))
                        m.mStrings.push_back(formatId(node.mPerk));
                m.mElements.clear();
                for (const std::string& s : m.mStrings)
                    m.mElements.push_back(text(s));
                BindingResult r = ok();
                r.value.type = BindingType::String;
                r.value.elements = m.mElements.data();
                r.value.count = m.mElements.size();
                return r;
            },
            openeden::sdk::BindingShape::Array);
        // what a perk function reads about one node of the trees
        struct NodeCtx
        {
            const PlayerProgression& mProgression;
            const PerkNode& mNode;
            FormId mPerk;
            const PerkRecord* mRecord;
            std::string& mScratch;
        };
        using NodeGetter = std::function<BindingValue(const NodeCtx&)>;
        const auto nodeFn = [&](std::string_view name, BindingType type, NodeGetter get) {
            add(name, s1, true, type, BindingAccess::Read,
                [self, perkOf, &m, type, get = std::move(get)](std::span<const BindingValue> a) {
                    FormId perk = 0;
                    if (!perkOf(a[0], perk))
                        return fail(BindingStatus::BadArgument, "not a perk id");
                    const PlayerProgression* p = self->progression();
                    const PerkNode* node = p != nullptr ? self->nodeOf(perk) : nullptr;
                    BindingResult r = ok();
                    if (node == nullptr)
                    {
                        r.value.type = type;
                        r.value.text = {};
                        return r;
                    }
                    r.value = get(NodeCtx{ *p, *node, perk, self->perkRecord(perk), m.mText });
                    return r;
                });
        };
        nodeFn("perk_name", kStr, [](const NodeCtx& c) {
            c.mScratch = c.mRecord != nullptr ? prettyPerkName(c.mRecord->mCommon.mEditorId) : std::string();
            return text(c.mScratch);
        });
        nodeFn("perk_rank", BindingType::Int32,
            [](const NodeCtx& c) { return integer(c.mProgression.perkRank(c.mPerk)); });
        nodeFn("perk_ranks", BindingType::Int32, [](const NodeCtx& c) {
            std::int64_t n = 0;
            for (std::size_t s = 0; s < kSkillCount; ++s)
                for (const NodeView& v : c.mProgression.nodes(s))
                    if (v.mPerk == c.mPerk)
                        n = v.mRanks;
            return integer(n);
        });
        nodeFn("perk_can_buy", BindingType::Bool, [](const NodeCtx& c) {
            std::string why;
            return boolean(c.mProgression.canBuyPerk(c.mPerk, why));
        });
        nodeFn("perk_reason", kStr, [](const NodeCtx& c) {
            c.mScratch.clear();
            if (c.mProgression.canBuyPerk(c.mPerk, c.mScratch))
                c.mScratch.clear();
            return text(c.mScratch);
        });
        nodeFn("perk_col", BindingType::Int32, [](const NodeCtx& c) { return integer(c.mNode.mColumn); });
        nodeFn("perk_row", BindingType::Int32, [](const NodeCtx& c) { return integer(c.mNode.mRow); });
        add("choose_level_up", s1, false, BindingType::Bool, BindingAccess::Mutate,
            [self](std::span<const BindingValue> a) {
                const std::string_view name = a[0].text;
                if (name != "health" && name != "magicka" && name != "stamina")
                    return ok();
                (void)self->chooseLevelUp(name == "magicka" ? Vital::Magicka
                        : name == "stamina"                 ? Vital::Stamina
                                                            : Vital::Health);
                return ok();
            });
        add("buy_perk", s1, false, BindingType::Bool, BindingAccess::Mutate,
            [self, perkOf](std::span<const BindingValue> a) {
                FormId perk = 0;
                if (perkOf(a[0], perk))
                    (void)self->buyPerk(perk);
                return ok();
            });

        openeden::sdk::NamespaceDescriptor ns;
        ns.name = "skyrimse.progression";
        ns.version = { 1, 0 };
        ns.functions = m.mFunctions;
        return service.declareNamespace(ns);
    }

    bool skillActorValue(const ProgressionService& service, std::string_view key, float& out)
    {
        const std::size_t slot = slotOfActorValueName(key);
        return slot < kSkillCount && service.actorValue(kFirstSkillValue + static_cast<std::uint32_t>(slot), out);
    }
}
