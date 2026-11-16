#include "neutralfollowers.hpp"

#include <algorithm>
#include <cmath>

#include <components/debug/debuglog.hpp>
#include <components/worldmodel/store.hpp>

#include <openeden/game/follower_rules.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/extensionmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwphysics/physicssystem.hpp"

#include "cellstore.hpp"
#include "neutraldoor.hpp"
#include "neutralstore.hpp"
#include "scene.hpp"
#include "worldimp.hpp"
#include "worldmodel.hpp"

namespace MWWorld
{
    namespace
    {
        openeden::sdk::game::FollowerRules* followerRulesOf(const std::string& gameId)
        {
            if (!MWBase::Environment::exists())
                return nullptr;
            MWBase::ExtensionManager* extensions = MWBase::Environment::get().getExtensionManager();
            NeutralWorld::NeutralWorldStore* store = extensions != nullptr ? extensions->neutralWorldStore() : nullptr;
            return dynamic_cast<openeden::sdk::game::FollowerRules*>(store != nullptr ? store->source(gameId) : nullptr);
        }
    }

    std::vector<ESM::Position> planFollowerArrivals(const ESM::Position& arrival, std::size_t count)
    {
        std::vector<ESM::Position> out;
        if (count == 0 || !std::isfinite(arrival.pos[0]) || !std::isfinite(arrival.pos[1])
            || !std::isfinite(arrival.pos[2]) || !std::isfinite(arrival.rot[2]))
            return out;
        const double yaw = arrival.rot[2];
        const double fx = std::sin(yaw); // forward, clockwise from +Y
        const double fy = std::cos(yaw);
        const double rx = std::cos(yaw); // right of forward
        const double ry = -std::sin(yaw);
        out.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            const double side = (static_cast<double>(i) - (static_cast<double>(count) - 1.0) / 2.0)
                * static_cast<double>(kFollowerArrivalSpread);
            ESM::Position p = arrival;
            p.pos[0] = static_cast<float>(arrival.pos[0] + fx * kFollowerArrivalForward + rx * side);
            p.pos[1] = static_cast<float>(arrival.pos[1] + fy * kFollowerArrivalForward + ry * side);
            out.push_back(p);
        }
        return out;
    }

    std::vector<Ptr> collectNeutralFollowers(const std::string& gameId, const Ptr& player, float range)
    {
        std::vector<Ptr> found;
        openeden::sdk::game::FollowerRules* rules = followerRulesOf(gameId);
        if (rules == nullptr || player.isEmpty())
            return found;
        openeden::sdk::game::WorldId ids[kMaxCarriedFollowers];
        const std::size_t count = std::min(rules->followers(ids, kMaxCarriedFollowers), kMaxCarriedFollowers);
        if (count == 0)
            return found;
        auto* world = dynamic_cast<World*>(&*MWBase::Environment::get().getWorld());
        if (world == nullptr)
            return found;
        const MWPhysics::PhysicsSystem* physics = world->getPhysicsSystem();
        const auto playerPosition = player.getRefData().getPosition().asVec3();
        std::vector<std::pair<openeden::sdk::game::WorldId, Ptr>> hits;
        for (CellStore* cell : world->getWorldScene().getActiveCells())
        {
            cell->forEachType<NeutralBase>([&](Ptr ptr) {
                const auto placement = neutralPlacementOf(ptr.getCellRef().getRefNum());
                if (!placement || std::find(ids, ids + count, *placement) == ids + count)
                    return true;
                if (!ptr.getRefData().isEnabled() || (physics != nullptr && physics->isNeutralActorDead(ptr)))
                    return true;
                const auto position = ptr.getRefData().getPosition().asVec3();
                const float dx = position.x - playerPosition.x;
                const float dy = position.y - playerPosition.y;
                if (dx * dx + dy * dy > range * range)
                    return true;
                hits.emplace_back(*placement, ptr);
                return true;
            });
        }
        std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        hits.erase(std::unique(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first == b.first; }),
            hits.end());
        for (auto& [id, ptr] : hits)
            found.push_back(ptr);
        return found;
    }

    std::size_t carryNeutralFollowers(
        const std::string& gameId, const std::vector<Ptr>& followers, CellStore& cell, const ESM::Position& arrival)
    {
        if (followers.empty())
            return 0;
        const std::vector<ESM::Position> spots = planFollowerArrivals(arrival, followers.size());
        if (spots.size() != followers.size())
            return 0;
        openeden::sdk::game::FollowerRules* rules = followerRulesOf(gameId);
        MWBase::World* world = MWBase::Environment::get().getWorld();
        std::size_t moved = 0;
        for (std::size_t i = 0; i < followers.size(); ++i)
        {
            const Ptr& follower = followers[i];
            const auto placement = neutralPlacementOf(follower.getCellRef().getRefNum());
            ESM::Position position = follower.getRefData().getPosition();
            position.rot[2] = spots[i].rot[2];
            follower.getRefData().setPosition(position);
            world->moveObject(follower, &cell, spots[i].asVec3(), true, true);
            ++moved;
            Log(Debug::Info) << "Neutral follower " << (placement ? *placement : 0) << " carried to "
                             << cell.getCell()->getNameId();
            if (rules != nullptr && placement)
                rules->onFollowerCarried(*placement);
        }
        return moved;
    }

    std::size_t carryNeutralFollowersWithPlayer(CellStore& destination, const ESM::Position& arrival)
    {
        if (!MWBase::Environment::exists())
            return 0;
        MWBase::World* world = MWBase::Environment::get().getWorld();
        const Ptr player = world != nullptr ? world->getPlayerPtr() : Ptr();
        CellStore* from = player.isEmpty() || !player.isInCell() ? nullptr : player.getCell();
        if (from == nullptr || !from->isNeutral())
            return 0;
        const std::string gameId = from->neutralGameId();
        const std::vector<Ptr> followers = collectNeutralFollowers(gameId, player);
        if (!followers.empty())
            Log(Debug::Info) << "Neutral player teleport: " << followers.size() << " followers go with the player";
        return carryNeutralFollowers(gameId, followers, destination, arrival);
    }

    std::size_t restoreNeutralFollowers()
    {
        if (!MWBase::Environment::exists())
            return 0;
        MWBase::World* world = MWBase::Environment::get().getWorld();
        const Ptr player = world != nullptr ? world->getPlayerPtr() : Ptr();
        CellStore* playerCell = player.isEmpty() || !player.isInCell() ? nullptr : player.getCell();
        if (playerCell == nullptr || !playerCell->isNeutral())
            return 0;
        const std::string gameId = playerCell->neutralGameId();
        openeden::sdk::game::FollowerRules* rules = followerRulesOf(gameId);
        if (rules == nullptr)
            return 0;
        openeden::sdk::game::WorldId ids[kMaxCarriedFollowers];
        const std::size_t count = std::min(rules->followers(ids, kMaxCarriedFollowers), kMaxCarriedFollowers);
        if (count == 0)
            return 0;
        // Followers that are already in an active cell stay where they are (their cell was the player's).
        const std::vector<Ptr> present = collectNeutralFollowers(gameId, player, 1e9f);
        std::vector<openeden::sdk::game::WorldId> have;
        for (const Ptr& p : present)
            if (const auto id = neutralPlacementOf(p.getCellRef().getRefNum()))
                have.push_back(*id);
        std::sort(ids, ids + count);
        WorldModel* model = MWBase::Environment::get().getWorldModel();
        std::vector<Ptr> brought;
        for (std::size_t i = 0; i < count; ++i)
        {
            if (std::find(have.begin(), have.end(), ids[i]) != have.end())
                continue;
            const openeden::sdk::game::WorldId home = rules->homeCell(ids[i]);
            if (home == openeden::sdk::game::kNoWorldId)
            {
                Log(Debug::Warning) << "Neutral follower " << ids[i] << ": the pack knows no home cell; not restored";
                continue;
            }
            const NeutralCellLookup found = model->findNeutralCell(gameId, home);
            if (found.mStatus != NeutralCellStatus::Found || found.mStore == nullptr)
            {
                Log(Debug::Warning) << "Neutral follower " << ids[i] << ": the home cell " << home
                                    << " cannot be built (" << found.mError << "); not restored";
                continue;
            }
            found.mStore->load();
            Ptr follower;
            found.mStore->forEachType<NeutralBase>([&](Ptr ptr) {
                const auto placement = neutralPlacementOf(ptr.getCellRef().getRefNum());
                if (placement && *placement == ids[i])
                {
                    follower = ptr;
                    return false;
                }
                return true;
            });
            if (follower.isEmpty())
            {
                Log(Debug::Warning) << "Neutral follower " << ids[i] << ": not in its home cell " << home;
                continue;
            }
            brought.push_back(follower);
        }
        if (brought.empty())
            return 0;
        // Around the player, as a door arrival: his own position and facing.
        return carryNeutralFollowers(gameId, brought, *playerCell, player.getRefData().getPosition());
    }
}
