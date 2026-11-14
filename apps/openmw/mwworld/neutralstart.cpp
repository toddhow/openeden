#include "neutralstart.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <vector>

namespace MWWorld
{
    namespace
    {
        constexpr double kMaxCoordinate = 10000000.0;
        constexpr int kMaxParentDepth = 8;

        std::string_view trim(std::string_view s)
        {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
                s.remove_prefix(1);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
                s.remove_suffix(1);
            return s;
        }

        bool parseNumber(std::string_view field, double& out)
        {
            field = trim(field);
            if (field.empty())
                return false;
            const auto [end, ec] = std::from_chars(field.data(), field.data() + field.size(), out);
            return ec == std::errc() && end == field.data() + field.size() && std::isfinite(out)
                && std::abs(out) <= kMaxCoordinate;
        }
    }

    NeutralStartTarget parseNeutralStart(std::string_view text)
    {
        NeutralStartTarget target;
        if (text.empty() || text.front() != '@')
        {
            target.mKind = NeutralStartTarget::Kind::Interior;
            target.mCell = std::string(text);
            return target;
        }
        const std::string_view usage = "use --start @<world editor id>,<x>,<y>[,<z>] (units)";
        std::vector<std::string_view> fields;
        std::string_view rest = text.substr(1);
        for (;;)
        {
            const std::size_t comma = rest.find(',');
            fields.push_back(rest.substr(0, comma));
            if (comma == std::string_view::npos)
                break;
            rest.remove_prefix(comma + 1);
        }
        if (fields.size() < 3 || fields.size() > 4)
        {
            target.mError = "the start place '" + std::string(text)
                + "' needs a world, x, y and optionally z: " + std::string(usage);
            return target;
        }
        const std::string_view world = trim(fields[0]);
        if (world.empty())
        {
            target.mError = "the start place '" + std::string(text) + "' names no world: " + std::string(usage);
            return target;
        }
        double x = 0.0, y = 0.0, z = 0.0;
        if (!parseNumber(fields[1], x) || !parseNumber(fields[2], y)
            || (fields.size() == 4 && !parseNumber(fields[3], z)))
        {
            target.mError = "the start place '" + std::string(text)
                + "' has a coordinate that is not a number within +-10,000,000 units: " + std::string(usage);
            return target;
        }
        target.mKind = NeutralStartTarget::Kind::Exterior;
        target.mWorld = std::string(world);
        target.mX = x;
        target.mY = y;
        if (fields.size() == 4)
            target.mZ = z;
        return target;
    }

    std::optional<double> neutralCellHeightAt(
        const openeden::sdk::game::TerrainCell& cell, double cellSizeUnits, double x, double y)
    {
        const std::size_t v = cell.vertsPerSide;
        if (!cell.hasHeights || v < 2 || cell.heights.size() != v * v || !(cellSizeUnits > 0.0) || !std::isfinite(x)
            || !std::isfinite(y))
            return std::nullopt;
        const double last = static_cast<double>(v - 1);
        const double u = std::clamp((x - cell.gridX * cellSizeUnits) / cellSizeUnits * last, 0.0, last);
        const double w = std::clamp((y - cell.gridY * cellSizeUnits) / cellSizeUnits * last, 0.0, last);
        const std::size_t col = std::min(static_cast<std::size_t>(u), v - 2);
        const std::size_t row = std::min(static_cast<std::size_t>(w), v - 2);
        const double fu = u - static_cast<double>(col);
        const double fw = w - static_cast<double>(row);
        const auto at = [&](std::size_t r, std::size_t c) { return static_cast<double>(cell.heights[r * v + c]); };
        const double h00 = at(row, col), h10 = at(row, col + 1), h01 = at(row + 1, col), h11 = at(row + 1, col + 1);
        const double h = (h00 * (1 - fu) + h10 * fu) * (1 - fw) + (h01 * (1 - fu) + h11 * fu) * fw;
        if (!std::isfinite(h))
            return std::nullopt;
        return h;
    }

    std::optional<double> neutralLandHeight(openeden::sdk::game::WorldCellSource& source,
        openeden::sdk::game::WorldId world, double x, double y, std::string& error)
    {
        using namespace openeden::sdk::game;
        if (!std::isfinite(x) || !std::isfinite(y))
        {
            error = "the position is not a number";
            return std::nullopt;
        }
        WorldId current = world;
        for (int depth = 0; depth < kMaxParentDepth; ++depth)
        {
            WorldSpaceInfo info;
            if (!source.worldInfo(current, info))
            {
                error = "world " + std::to_string(current) + " is unknown to its game pack";
                return std::nullopt;
            }
            const double size = info.cellSizeUnits;
            if (!(size > 0.0))
            {
                error = "the world has no cell size";
                return std::nullopt;
            }
            const double gx = std::floor(x / size), gy = std::floor(y / size);
            if (!(std::abs(gx) < 1.0e6) || !(std::abs(gy) < 1.0e6))
            {
                error = "the position is outside the grid";
                return std::nullopt;
            }
            TerrainCell cell;
            std::string buildError;
            switch (source.buildTerrain(
                current, static_cast<std::int32_t>(gx), static_cast<std::int32_t>(gy), cell, buildError))
            {
                case TerrainResult::Built:
                {
                    if (!cell.hasHeights)
                        return static_cast<double>(info.defaultLandHeight);
                    const auto h = neutralCellHeightAt(cell, size, x, y);
                    if (!h)
                        error = "the land of the square holds no usable heights";
                    return h;
                }
                case TerrainResult::NoLand:
                    if (info.parent != kNoWorldId && (info.parentUsage & TerrainParentUsage::Land) != 0)
                    {
                        current = info.parent;
                        continue;
                    }
                    error = "the world has no land in that square";
                    return std::nullopt;
                case TerrainResult::Failed:
                    error = "the land could not be built: " + buildError;
                    return std::nullopt;
                case TerrainResult::NotSupported:
                    error = "the game pack has no terrain";
                    return std::nullopt;
            }
        }
        error = "the chain of parent worlds is too long or loops";
        return std::nullopt;
    }
}
