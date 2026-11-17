#include "game_skyrimse/map.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace game_skyrimse::map
{
    void buildTargets(const TargetInputs& in, std::vector<Target>& out)
    {
        out.clear();
        if (in.mQuests == nullptr || in.mState == nullptr || in.mPlaced == nullptr || !in.mObjective)
            return;
        for (const FormId questId : in.mQuests->questIds())
        {
            if (!in.mState->running(questId))
                continue;
            const QuestRecord* quest = in.mQuests->quest(questId);
            if (quest == nullptr)
                continue;
            for (const QuestObjective& objective : quest->mObjectives)
            {
                const journal::ObjectiveFlags flags = in.mObjective(questId, objective.mIndex);
                if (!flags.mDisplayed || flags.mCompleted)
                    continue;
                for (const QuestObjectiveTarget& target : objective.mTargets)
                {
                    if (out.size() >= kMaxTargets)
                        return;
                    const AliasFill fill = in.mState->aliasFill(questId, target.mAlias);
                    if (!fill.mKnown)
                        continue;
                    FormId reference = fill.mReference;
                    if (reference == kNullFormId && fill.mActor != kNullFormId)
                    {
                        const PlacedRefIndex::Range range = in.mPlaced->byBase(fill.mActor);
                        for (const PlacedEntry* const* it = range.mBegin; it != range.mEnd; ++it)
                            if (!in.mPlaced->initiallyDisabled(**it))
                            {
                                reference = (*it)->mId;
                                break;
                            }
                    }
                    const PlacedEntry* placed = reference != kNullFormId ? in.mPlaced->find(reference) : nullptr;
                    if (placed == nullptr)
                        continue;
                    Target t;
                    t.mQuest = questId;
                    t.mObjective = objective.mIndex;
                    t.mCell = placed->mCell;
                    t.mX = placed->mX;
                    t.mY = placed->mY;
                    t.mZ = placed->mZ;
                    FormId world = kNullFormId;
                    bool interior = false;
                    if (in.mCellWorld && in.mCellWorld(placed->mCell, world, interior) && !interior)
                        t.mWorld = world;
                    if (in.mText)
                        t.mText
                            = journal::expandTags(in.mText(objective.mText, quest->mPlugin, StringTableKind::Strings));
                    out.push_back(std::move(t));
                }
            }
        }
    }

    // ---- the chunk -------------------------------------------------------------------------------------------------

    void MapChunk::write(openeden::sdk::ChunkWriter& writer)
    {
        const std::vector<FormId>& found = mState.found();
        writer.writeValue(static_cast<std::uint32_t>(found.size()));
        for (const FormId id : found)
            writer.writeValue(static_cast<std::uint32_t>(id));
    }

    bool MapChunk::read(openeden::sdk::ChunkReader& reader, std::uint32_t version)
    {
        std::string error;
        std::vector<FormId> found;
        if (version != kVersion)
            error = "the map chunk has version " + std::to_string(version);
        else
        {
            std::uint32_t count = 0;
            if (!reader.readValue(count) || count > kMaxDiscovered
                || static_cast<std::uint64_t>(count) * 4 != reader.remaining())
                error = "the map chunk's count does not fit its size";
            else
            {
                found.reserve(count);
                FormId previous = 0;
                for (std::uint32_t i = 0; i < count && error.empty(); ++i)
                {
                    std::uint32_t id = 0;
                    if (!reader.readValue(id))
                        error = "the map chunk is cut short";
                    else if (i > 0 && id <= previous)
                        error = "the map chunk's ids are not ascending";
                    else
                    {
                        found.push_back(id);
                        previous = id;
                    }
                }
            }
        }
        if (!error.empty())
        {
            if (mSink)
                mSink(error);
            return false;
        }
        mState.restore(std::move(found));
        if (mRestored)
            mRestored("restored: " + std::to_string(mState.count()) + " discovered markers");
        return true;
    }

    // ---- the service -----------------------------------------------------------------------------------------------

    namespace
    {
        using openeden::sdk::BindingResult;
        using openeden::sdk::BindingShape;
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
        std::int32_t world32(const BindingValue& v)
        {
            return v.integer;
        }
    }

    struct MapService::Impl
    {
        std::vector<std::unique_ptr<Fn>> mHandlers;
        std::vector<openeden::sdk::FunctionDescriptor> mFunctions;
        std::vector<BindingValue> mElements;
        std::vector<std::string> mStrings;
        std::vector<View> mViews;
        std::vector<std::int32_t> mRow;
        std::int32_t mBounds[4] = { 0, 0, 0, 0 };
    };

    MapService::MapService(Hooks hooks)
        : mImpl(std::make_unique<Impl>())
        , mHooks(std::move(hooks))
    {
    }

    MapService::~MapService() = default;

    bool MapService::poll()
    {
        if (mReady)
            return true;
        Access access;
        if (mHooks.mAccess && mHooks.mAccess(access) && access.mMarkers != nullptr)
        {
            mAccess = std::move(access);
            mReady = true;
            if (mHooks.mLog)
                mHooks.mLog("MAP ready: " + std::to_string(mAccess.mMarkers->size()) + " exterior map markers");
        }
        return mReady;
    }

    std::uint64_t MapService::revision() const
    {
        if (!mReady)
            return 0;
        const std::uint64_t quests = mAccess.mState != nullptr ? mAccess.mState->revision() : 0;
        return 1 + mDiscovery.revision() + quests + (mHooks.mObjectiveRevision ? mHooks.mObjectiveRevision() : 0);
    }

    const std::vector<Target>& MapService::targets()
    {
        if (!poll())
            return mTargets;
        const std::uint64_t now = revision();
        if (now != mTargetsRevision)
        {
            mTargetsRevision = now;
            TargetInputs in;
            in.mQuests = mAccess.mQuests;
            in.mState = mAccess.mState;
            in.mPlaced = mAccess.mPlaced;
            in.mObjective = mHooks.mObjective;
            in.mText = mAccess.mText;
            in.mCellWorld = mAccess.mCellWorld;
            buildTargets(in, mTargets);
        }
        return mTargets;
    }

    std::vector<FormId> MapService::markerWorlds(FormId world) const
    {
        std::vector<FormId> out{ world };
        if (!mReady || !mAccess.mParentOf)
            return out;
        FormId current = world;
        for (int i = 0; i < 3; ++i) // a short chain; a cycle in bad data ends at the repeat
        {
            const FormId parent = mAccess.mParentOf(current);
            if (parent == kNullFormId || std::find(out.begin(), out.end(), parent) != out.end())
                break;
            out.push_back(parent);
            current = parent;
        }
        return out;
    }

    std::size_t MapService::visit(FormId world, float x, float y)
    {
        if (!poll())
            return 0;
        const std::size_t added = mDiscovery.visit(*mAccess.mMarkers, markerWorlds(world), x, y, kDiscoveryRadius);
        if (added > 0 && mHooks.mLog)
            mHooks.mLog("MAP discovered " + std::to_string(added) + " markers (" + std::to_string(mDiscovery.count())
                + " in all)");
        return added;
    }

    openeden::sdk::DeclareResult MapService::declare(openeden::sdk::BindingsService& service)
    {
        Impl& m = *mImpl;
        MapService* self = this;
        // (world, x, y, range, hint range, page) of the marker queries
        static const BindingType queryParams[] = { BindingType::Int32, BindingType::Double, BindingType::Double,
            BindingType::Double, BindingType::Double, BindingType::Int32 };
        static const BindingType worldParam[] = { BindingType::Int32 };
        static const BindingType nameParam[] = { BindingType::String };
        static const BindingType rowParams[] = { BindingType::Int32, BindingType::Int32 };
        static const BindingType visitParams[] = { BindingType::Int32, BindingType::Double, BindingType::Double };

        const auto add = [&](std::string_view name, std::span<const BindingType> params, bool hasResult,
                             BindingType result, Fn::Body body, BindingShape shape = BindingShape::Single,
                             openeden::sdk::BindingAccess access = openeden::sdk::BindingAccess::Read) {
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
        const auto arrayResult = [&m](BindingType type) {
            BindingResult r;
            r.value.type = type;
            r.value.elements = m.mElements.data();
            r.value.count = m.mElements.size();
            return r;
        };
        const auto runQuery = [self, &m](std::span<const BindingValue> a) {
            m.mViews.clear();
            if (!self->poll())
                return;
            const std::vector<FormId> worlds = self->markerWorlds(static_cast<FormId>(world32(a[0])));
            const std::size_t page = a[5].integer < 0 ? 0 : static_cast<std::size_t>(a[5].integer);
            query(*self->mAccess.mMarkers, self->mDiscovery, worlds, static_cast<float>(a[1].number),
                static_cast<float>(a[2].number), static_cast<float>(a[3].number), static_cast<float>(a[4].number),
                page * kPageSize, kPageSize, m.mViews);
        };

        add("ready", {}, true, BindingType::Bool, [self](std::span<const BindingValue>) {
            BindingResult r;
            r.value = boolean(self->poll());
            return r;
        });
        add("revision", {}, true, BindingType::Int32, [self](std::span<const BindingValue>) {
            (void)self->poll();
            BindingResult r;
            r.value = integer(static_cast<std::int32_t>(self->revision() & 0x7FFFFFFF));
            return r;
        });
        add(
            "marker_x", queryParams, true, BindingType::Double,
            [&m, runQuery, arrayResult](std::span<const BindingValue> a) {
                runQuery(a);
                m.mElements.clear();
                for (const View& v : m.mViews)
                    m.mElements.push_back(number(v.mMarker->mX));
                return arrayResult(BindingType::Double);
            },
            BindingShape::Array);
        add(
            "marker_y", queryParams, true, BindingType::Double,
            [&m, runQuery, arrayResult](std::span<const BindingValue> a) {
                runQuery(a);
                m.mElements.clear();
                for (const View& v : m.mViews)
                    m.mElements.push_back(number(v.mMarker->mY));
                return arrayResult(BindingType::Double);
            },
            BindingShape::Array);
        add(
            "marker_type", queryParams, true, BindingType::Int32,
            [&m, runQuery, arrayResult](std::span<const BindingValue> a) {
                runQuery(a);
                m.mElements.clear();
                for (const View& v : m.mViews)
                    m.mElements.push_back(integer(v.mMarker->mType));
                return arrayResult(BindingType::Int32);
            },
            BindingShape::Array);
        add(
            "marker_discovered", queryParams, true, BindingType::Bool,
            [&m, runQuery, arrayResult](std::span<const BindingValue> a) {
                runQuery(a);
                m.mElements.clear();
                for (const View& v : m.mViews)
                    m.mElements.push_back(boolean(v.mDiscovered));
                return arrayResult(BindingType::Bool);
            },
            BindingShape::Array);
        // the name of an undiscovered marker is not given (an empty text)
        add(
            "marker_name", queryParams, true, BindingType::String,
            [self, &m, runQuery, arrayResult](std::span<const BindingValue> a) {
                runQuery(a);
                m.mStrings.clear();
                for (const View& v : m.mViews)
                {
                    std::string name = v.mMarker->mName.mText;
                    if (v.mDiscovered && v.mMarker->mName.mLocalized && self->mAccess.mText)
                        name = self->mAccess.mText(v.mMarker->mName, v.mMarker->mPlugin, StringTableKind::Strings);
                    m.mStrings.push_back(v.mDiscovered ? name : std::string());
                }
                m.mElements.clear();
                for (const std::string& s : m.mStrings)
                    m.mElements.push_back(text(s));
                return arrayResult(BindingType::String);
            },
            BindingShape::Array);

        const auto targetArray = [self, &m, arrayResult](BindingType type, const auto& pick) {
            const std::vector<Target>& targets = self->targets();
            m.mStrings.clear();
            m.mElements.clear();
            for (const Target& t : targets)
                pick(t, m);
            return arrayResult(type);
        };
        add(
            "target_x", {}, true, BindingType::Double,
            [targetArray](std::span<const BindingValue>) {
                return targetArray(
                    BindingType::Double, [](const Target& t, Impl& impl) { impl.mElements.push_back(number(t.mX)); });
            },
            BindingShape::Array);
        add(
            "target_y", {}, true, BindingType::Double,
            [targetArray](std::span<const BindingValue>) {
                return targetArray(
                    BindingType::Double, [](const Target& t, Impl& impl) { impl.mElements.push_back(number(t.mY)); });
            },
            BindingShape::Array);
        add(
            "target_world", {}, true, BindingType::Int32,
            [targetArray](std::span<const BindingValue>) {
                return targetArray(BindingType::Int32, [](const Target& t, Impl& impl) {
                    impl.mElements.push_back(integer(static_cast<std::int32_t>(t.mWorld)));
                });
            },
            BindingShape::Array);
        add(
            "target_text", {}, true, BindingType::String,
            [self, &m, arrayResult](std::span<const BindingValue>) {
                const std::vector<Target>& targets = self->targets();
                m.mStrings.clear();
                for (const Target& t : targets)
                    m.mStrings.push_back(t.mText);
                m.mElements.clear();
                for (const std::string& s : m.mStrings)
                    m.mElements.push_back(text(s));
                return arrayResult(BindingType::String);
            },
            BindingShape::Array);

        add(
            "raster_info", worldParam, true, BindingType::Int32,
            [self, &m, arrayResult](std::span<const BindingValue> a) {
                m.mElements.clear();
                std::int32_t minX = 0, minY = 0, width = 0, height = 0;
                if (self->poll() && self->mAccess.mLandBounds
                    && self->mAccess.mLandBounds(static_cast<FormId>(world32(a[0])), minX, minY, width, height))
                {
                    width = std::min<std::int32_t>(width, static_cast<std::int32_t>(kPageSize));
                    for (const std::int32_t v : { minX, minY, width, height })
                        m.mElements.push_back(integer(v));
                }
                return arrayResult(BindingType::Int32);
            },
            BindingShape::Array);
        add(
            "raster_row", rowParams, true, BindingType::Int32,
            [self, &m, arrayResult](std::span<const BindingValue> a) {
                m.mElements.clear();
                m.mRow.clear();
                if (self->poll() && self->mAccess.mLandRow
                    && self->mAccess.mLandRow(static_cast<FormId>(world32(a[0])), a[1].integer, m.mRow))
                {
                    if (m.mRow.size() > kPageSize)
                        m.mRow.resize(kPageSize);
                    for (const std::int32_t v : m.mRow)
                        m.mElements.push_back(integer(v));
                }
                return arrayResult(BindingType::Int32);
            },
            BindingShape::Array);
        // task 0223: the exterior world an interior belongs to by its location (the map is shown indoors for it)
        add("interior_world", nameParam, true, BindingType::Int32, [self](std::span<const BindingValue> a) {
            BindingResult r;
            FormId world = kNullFormId;
            if (!a[0].text.empty() && self->poll() && self->mAccess.mInteriorWorld)
                world = self->mAccess.mInteriorWorld(a[0].text);
            r.value = integer(static_cast<std::int32_t>(world));
            return r;
        });
        add(
            "visit", visitParams, false, BindingType::Bool,
            [self](std::span<const BindingValue> a) {
                (void)self->visit(static_cast<FormId>(world32(a[0])), static_cast<float>(a[1].number),
                    static_cast<float>(a[2].number));
                return BindingResult();
            },
            BindingShape::Single, openeden::sdk::BindingAccess::Mutate);

        openeden::sdk::NamespaceDescriptor ns;
        ns.name = kNamespace;
        ns.version = { 1, 1 };
        ns.functions = m.mFunctions;
        return service.declareNamespace(ns);
    }
}
