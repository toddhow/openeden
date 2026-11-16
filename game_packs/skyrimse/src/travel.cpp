#include "game_skyrimse/travel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace game_skyrimse::travel
{
    std::string_view refusalName(Refusal refusal)
    {
        switch (refusal)
        {
            case Refusal::None:
                return "";
            case Refusal::NotReady:
                return "not_ready";
            case Refusal::Interior:
                return "interior";
            case Refusal::UnknownMarker:
                return "unknown";
            case Refusal::NotDiscovered:
                return "undiscovered";
            case Refusal::InCombat:
                return "combat";
            case Refusal::EnemiesNear:
                return "enemies";
            case Refusal::NoDestination:
                return "no_destination";
        }
        return "";
    }

    Refusal decideTravel(const Inputs& in)
    {
        if (!in.mReady)
            return Refusal::NotReady;
        if (in.mInterior)
            return Refusal::Interior;
        if (!in.mKnown)
            return Refusal::UnknownMarker;
        if (!in.mDiscovered)
            return Refusal::NotDiscovered;
        if (in.mInCombat)
            return Refusal::InCombat;
        if (in.mEnemiesNear)
            return Refusal::EnemiesNear;
        if (!in.mHasDestination)
            return Refusal::NoDestination;
        return Refusal::None;
    }

    int travelHours(double distance)
    {
        if (!std::isfinite(distance) || distance <= 0.0)
            return kMinTravelHours;
        const double hours = std::ceil(distance / kUnitsPerGameHour);
        return static_cast<int>(std::clamp(hours, static_cast<double>(kMinTravelHours),
            static_cast<double>(kMaxTravelHours)));
    }

    double yawToward(double fromX, double fromY, double toX, double toY)
    {
        if (!std::isfinite(fromX) || !std::isfinite(fromY) || !std::isfinite(toX) || !std::isfinite(toY))
            return 0.0;
        const double dx = toX - fromX;
        const double dy = toY - fromY;
        if (dx == 0.0 && dy == 0.0)
            return 0.0;
        return std::atan2(dx, dy); // 0 faces +Y and the angle grows clockwise, as the engine stores a heading
    }

    Destination chooseDestination(const map::Marker& marker, const PlacedEntry* link, bool linkUsable)
    {
        Destination d;
        if (link != nullptr && linkUsable)
        {
            d.mX = link->mX;
            d.mY = link->mY;
            d.mZ = link->mZ;
            d.mYaw = yawToward(link->mX, link->mY, marker.mX, marker.mY);
            d.mFromLink = true;
            return d;
        }
        d.mX = marker.mX;
        d.mY = marker.mY;
        d.mZ = marker.mZ;
        return d;
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

        /// Hex of 1 to 8 digits to a form id; false for anything else (an empty text, a stray character, too long).
        bool parseFormId(std::string_view s, FormId& out)
        {
            if (s.empty() || s.size() > 8)
                return false;
            std::uint32_t v = 0;
            for (const char c : s)
            {
                int d = -1;
                if (c >= '0' && c <= '9')
                    d = c - '0';
                else if (c >= 'a' && c <= 'f')
                    d = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F')
                    d = c - 'A' + 10;
                if (d < 0)
                    return false;
                v = (v << 4) | static_cast<std::uint32_t>(d);
            }
            out = v;
            return true;
        }

        std::string hex8(FormId id)
        {
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "%08x", static_cast<unsigned>(id));
            return buffer;
        }

        bool contains(const std::vector<FormId>& list, FormId id)
        {
            return std::find(list.begin(), list.end(), id) != list.end();
        }
    }

    struct TravelService::Impl
    {
        std::vector<std::unique_ptr<Fn>> mHandlers;
        std::vector<openeden::sdk::FunctionDescriptor> mFunctions;
        std::vector<BindingValue> mElements;
        std::vector<std::string> mStrings;
        std::vector<map::View> mViews;
        std::string mText;
    };

    TravelService::TravelService(map::MapService& map, Hooks hooks)
        : mImpl(std::make_unique<Impl>())
        , mMap(map)
        , mHooks(std::move(hooks))
    {
    }

    TravelService::~TravelService() = default;

    bool TravelService::plan(FormId world, FormId marker, double fromX, double fromY, Plan& out)
    {
        if (!mMap.poll())
            return false;
        const map::Access& access = mMap.access();
        const map::Marker* m = marker != kNullFormId ? access.mMarkers->find(marker) : nullptr;
        if (m == nullptr || !contains(mMap.markerWorlds(world), m->mWorld))
            return false;
        const PlacedEntry* link = (m->mLinked != kNullFormId && access.mPlaced != nullptr)
            ? access.mPlaced->find(m->mLinked)
            : nullptr;
        bool usable = false;
        if (link != nullptr && !link->mDeleted && access.mCellWorld)
        {
            FormId linkWorld = kNullFormId;
            bool interior = false;
            usable = access.mCellWorld(link->mCell, linkWorld, interior) && !interior
                && contains(mMap.markerWorlds(m->mWorld), linkWorld);
        }
        out.mWhere = chooseDestination(*m, link, usable);
        const double dx = static_cast<double>(out.mWhere.mX) - fromX;
        const double dy = static_cast<double>(out.mWhere.mY) - fromY;
        out.mDistance = std::isfinite(dx) && std::isfinite(dy) ? std::hypot(dx, dy) : 0.0;
        out.mHours = travelHours(out.mDistance);
        return true;
    }

    Refusal TravelService::refusal(FormId world, bool interior, FormId marker, double fromX, double fromY)
    {
        Inputs in;
        in.mReady = mMap.poll() && mHooks.mReady && mHooks.mReady();
        in.mInterior = interior;
        if (in.mReady)
        {
            const map::Access& access = mMap.access();
            const map::Marker* m = marker != kNullFormId ? access.mMarkers->find(marker) : nullptr;
            in.mKnown = m != nullptr && contains(mMap.markerWorlds(world), m->mWorld);
            in.mDiscovered
                = in.mKnown && (mMap.discovery().discovered(marker) || (m->mFlags & map::MarkerFlags::CanTravel) != 0);
            in.mInCombat = mHooks.mInCombat && mHooks.mInCombat();
            in.mEnemiesNear = mHooks.mEnemiesNear && mHooks.mEnemiesNear();
            Plan unused;
            in.mHasDestination = in.mKnown && plan(world, marker, fromX, fromY, unused);
        }
        return decideTravel(in);
    }

    bool TravelService::commit(FormId world, bool interior, FormId marker, double fromX, double fromY)
    {
        const Refusal why = refusal(world, interior, marker, fromX, fromY);
        if (why != Refusal::None)
        {
            ++mRefused;
            if (mHooks.mLog)
                mHooks.mLog("TRAVEL refused: " + std::string(refusalName(why)));
            return false;
        }
        Plan plan;
        if (!this->plan(world, marker, fromX, fromY, plan))
            return false; // refusal() has just said it could be planned
        const map::Marker* m = mMap.access().mMarkers->find(marker);
        std::string name = m->mName.mText;
        if (m->mName.mLocalized && mMap.access().mText)
            name = mMap.access().mText(m->mName, m->mPlugin, StringTableKind::Strings);
        (void)mMap.visit(world, plan.mWhere.mX, plan.mWhere.mY);
        ++mPerformed;
        if (mHooks.mLog)
        {
            const std::size_t followers = mHooks.mFollowerCount ? mHooks.mFollowerCount() : 0;
            mHooks.mLog("TRAVEL to " + name + " at (" + std::to_string(static_cast<long>(plan.mWhere.mX)) + ", "
                + std::to_string(static_cast<long>(plan.mWhere.mY)) + ", "
                + std::to_string(static_cast<long>(plan.mWhere.mZ)) + ") in " + std::to_string(plan.mHours)
                + " hours, " + std::to_string(followers) + " followers" + (plan.mWhere.mFromLink ? ", from the link" : ""));
        }
        return true;
    }

    // ---- task 0223: travel to a placed reference --------------------------------------------------------------------

    bool TravelService::planToRef(FormId world, FormId destination, double fromX, double fromY, Plan& out)
    {
        if (destination == kNullFormId || !mMap.poll())
            return false;
        const map::Access& access = mMap.access();
        const PlacedEntry* ref = access.mPlaced != nullptr ? access.mPlaced->find(destination) : nullptr;
        if (ref == nullptr || ref->mDeleted || !access.mCellWorld)
            return false;
        FormId refWorld = kNullFormId;
        bool interior = false;
        if (!access.mCellWorld(ref->mCell, refWorld, interior) || interior)
            return false;
        // the same world, or one that borrows its land from the other (the teleport lands in the player's world)
        if (!contains(mMap.markerWorlds(world), refWorld) && !contains(mMap.markerWorlds(refWorld), world))
            return false;
        out.mWhere = Destination();
        out.mWhere.mX = ref->mX;
        out.mWhere.mY = ref->mY;
        out.mWhere.mZ = ref->mZ;
        out.mWhere.mYaw = yawToward(fromX, fromY, ref->mX, ref->mY);
        const double dx = static_cast<double>(ref->mX) - fromX;
        const double dy = static_cast<double>(ref->mY) - fromY;
        out.mDistance = std::isfinite(dx) && std::isfinite(dy) ? std::hypot(dx, dy) : 0.0;
        out.mHours = travelHours(out.mDistance);
        return true;
    }

    Refusal TravelService::refusalToRef(FormId world, bool interior, FormId destination, double fromX, double fromY)
    {
        Inputs in;
        in.mReady = mMap.poll() && mHooks.mReady && mHooks.mReady();
        in.mInterior = interior;
        if (in.mReady)
        {
            const map::Access& access = mMap.access();
            const PlacedEntry* ref = destination != kNullFormId && access.mPlaced != nullptr
                ? access.mPlaced->find(destination)
                : nullptr;
            in.mKnown = ref != nullptr && !ref->mDeleted;
            in.mDiscovered = in.mKnown; // a carriage goes to a place whether or not the map knows it
            in.mInCombat = mHooks.mInCombat && mHooks.mInCombat();
            in.mEnemiesNear = mHooks.mEnemiesNear && mHooks.mEnemiesNear();
            Plan unused;
            in.mHasDestination = in.mKnown && planToRef(world, destination, fromX, fromY, unused);
        }
        return decideTravel(in);
    }

    bool TravelService::commitToRef(FormId world, bool interior, FormId destination, double fromX, double fromY)
    {
        const Refusal why = refusalToRef(world, interior, destination, fromX, fromY);
        if (why != Refusal::None)
        {
            ++mRefused;
            if (mHooks.mLog)
                mHooks.mLog("TRAVEL refused: " + std::string(refusalName(why)));
            return false;
        }
        Plan plan;
        if (!planToRef(world, destination, fromX, fromY, plan))
            return false; // refusalToRef() has just said it could be planned
        (void)mMap.visit(world, plan.mWhere.mX, plan.mWhere.mY);
        ++mPerformed;
        if (mHooks.mLog)
        {
            const std::size_t followers = mHooks.mFollowerCount ? mHooks.mFollowerCount() : 0;
            mHooks.mLog("TRAVEL to " + hex8(destination) + " at (" + std::to_string(static_cast<long>(plan.mWhere.mX))
                + ", " + std::to_string(static_cast<long>(plan.mWhere.mY)) + ", "
                + std::to_string(static_cast<long>(plan.mWhere.mZ)) + ") in " + std::to_string(plan.mHours)
                + " hours, " + std::to_string(followers) + " followers");
        }
        return true;
    }

    bool TravelService::queueRequest(FormId destination)
    {
        if (destination == kNullFormId)
            return false;
        ++mRequestSeq;
        mRequested = destination;
        return true;
    }

    openeden::sdk::DeclareResult TravelService::declare(openeden::sdk::BindingsService& service)
    {
        Impl& m = *mImpl;
        TravelService* self = this;
        // (world, x, y, range, hint range, page): the same query as the map's marker arrays
        static const BindingType queryParams[] = { BindingType::Int32, BindingType::Double, BindingType::Double,
            BindingType::Double, BindingType::Double, BindingType::Int32 };
        // (world, interior, marker, x, y)
        static const BindingType refusalParams[] = { BindingType::Int32, BindingType::Bool, BindingType::String,
            BindingType::Double, BindingType::Double };
        // (world, marker, x, y)
        static const BindingType planParams[]
            = { BindingType::Int32, BindingType::String, BindingType::Double, BindingType::Double };

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
        const auto markerOf = [](const BindingValue& v) {
            FormId id = kNullFormId;
            return parseFormId(v.text, id) ? id : kNullFormId;
        };

        add("ready", {}, true, BindingType::Bool, [self](std::span<const BindingValue>) {
            BindingResult r;
            r.value = boolean(self->mMap.poll() && self->mHooks.mReady && self->mHooks.mReady());
            return r;
        });
        add("marker_ref", queryParams, true, BindingType::String,
            [self, &m, arrayResult](std::span<const BindingValue> a) {
                m.mViews.clear();
                m.mStrings.clear();
                m.mElements.clear();
                if (self->mMap.poll())
                {
                    const std::vector<FormId> worlds = self->mMap.markerWorlds(static_cast<FormId>(a[0].integer));
                    const std::size_t page = a[5].integer < 0 ? 0 : static_cast<std::size_t>(a[5].integer);
                    map::query(*self->mMap.access().mMarkers, self->mMap.discovery(), worlds,
                        static_cast<float>(a[1].number), static_cast<float>(a[2].number),
                        static_cast<float>(a[3].number), static_cast<float>(a[4].number), page * map::kPageSize,
                        map::kPageSize, m.mViews);
                }
                for (const map::View& v : m.mViews)
                    m.mStrings.push_back(hex8(v.mMarker->mRef));
                for (const std::string& s : m.mStrings)
                    m.mElements.push_back(text(s));
                return arrayResult(BindingType::String);
            },
            BindingShape::Array);
        add("refusal", refusalParams, true, BindingType::String, [self, &m, markerOf](std::span<const BindingValue> a) {
            m.mText = std::string(refusalName(self->refusal(static_cast<FormId>(a[0].integer), a[1].boolean,
                markerOf(a[2]), a[3].number, a[4].number)));
            BindingResult r;
            r.value = text(m.mText);
            return r;
        });
        add("plan", planParams, true, BindingType::Double,
            [self, &m, markerOf, arrayResult](std::span<const BindingValue> a) {
                m.mElements.clear();
                Plan p;
                if (self->plan(static_cast<FormId>(a[0].integer), markerOf(a[1]), a[2].number, a[3].number, p))
                {
                    for (const double v : { static_cast<double>(p.mWhere.mX), static_cast<double>(p.mWhere.mY),
                             static_cast<double>(p.mWhere.mZ), p.mWhere.mYaw, static_cast<double>(p.mHours),
                             p.mDistance })
                        m.mElements.push_back(number(v));
                }
                return arrayResult(BindingType::Double);
            },
            BindingShape::Array);
        add("follower_count", {}, true, BindingType::Int32, [self](std::span<const BindingValue>) {
            BindingResult r;
            const std::size_t n = self->mHooks.mFollowerCount ? self->mHooks.mFollowerCount() : 0;
            r.value = integer(static_cast<std::int32_t>(std::min<std::size_t>(n, 0x7FFFFFFF)));
            return r;
        });
        add("commit", refusalParams, false, BindingType::Bool,
            [self, markerOf](std::span<const BindingValue> a) {
                (void)self->commit(static_cast<FormId>(a[0].integer), a[1].boolean, markerOf(a[2]), a[3].number,
                    a[4].number);
                return BindingResult();
            },
            BindingShape::Single, openeden::sdk::BindingAccess::Mutate);

        // task 0223: the carriage's Game.FastTravel(reference). The global script reads `request_seq` and `request_ref`,
        // asks `refusal_ref` and `plan_ref`, and calls `commit_ref`.
        add("request_seq", {}, true, BindingType::Int32, [self](std::span<const BindingValue>) {
            BindingResult r;
            r.value = integer(static_cast<std::int32_t>(std::min<std::uint32_t>(self->mRequestSeq, 0x7FFFFFFF)));
            return r;
        });
        add("request_ref", {}, true, BindingType::String, [self, &m](std::span<const BindingValue>) {
            m.mText = self->mRequested != kNullFormId ? hex8(self->mRequested) : std::string();
            BindingResult r;
            r.value = text(m.mText);
            return r;
        });
        add("refusal_ref", refusalParams, true, BindingType::String,
            [self, &m, markerOf](std::span<const BindingValue> a) {
                m.mText = std::string(refusalName(self->refusalToRef(static_cast<FormId>(a[0].integer), a[1].boolean,
                    markerOf(a[2]), a[3].number, a[4].number)));
                BindingResult r;
                r.value = text(m.mText);
                return r;
            });
        add("plan_ref", planParams, true, BindingType::Double,
            [self, &m, markerOf, arrayResult](std::span<const BindingValue> a) {
                m.mElements.clear();
                Plan p;
                if (self->planToRef(static_cast<FormId>(a[0].integer), markerOf(a[1]), a[2].number, a[3].number, p))
                {
                    for (const double v : { static_cast<double>(p.mWhere.mX), static_cast<double>(p.mWhere.mY),
                             static_cast<double>(p.mWhere.mZ), p.mWhere.mYaw, static_cast<double>(p.mHours),
                             p.mDistance })
                        m.mElements.push_back(number(v));
                }
                return arrayResult(BindingType::Double);
            },
            BindingShape::Array);
        add("commit_ref", refusalParams, false, BindingType::Bool,
            [self, markerOf](std::span<const BindingValue> a) {
                (void)self->commitToRef(static_cast<FormId>(a[0].integer), a[1].boolean, markerOf(a[2]), a[3].number,
                    a[4].number);
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
