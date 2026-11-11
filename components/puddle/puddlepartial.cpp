#include "puddlepartial.hpp"

#include <fstream>
#include <iterator>
#include <system_error>

namespace Puddle
{
    PartialStore::PartialStore(std::filesystem::path folder)
        : mFolder(std::move(folder))
    {
    }

    std::filesystem::path PartialStore::pathOf(std::int32_t cellX, std::int32_t cellY) const
    {
        return mFolder / (std::to_string(cellX) + "_" + std::to_string(cellY) + ".cell");
    }

    std::optional<CellMap> PartialStore::load(
        std::int32_t cellX, std::int32_t cellY, const Digest& parameters, const Digest& input) const
    {
        std::ifstream file(pathOf(cellX, cellY), std::ios::binary);
        if (!file)
            return std::nullopt;
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        CellMap cell;
        if (parseCell(bytes, cell))
            return std::nullopt;
        if (cell.mCellX != cellX || cell.mCellY != cellY || cell.mParameterDigest != parameters
            || cell.mInputDigest != input)
            return std::nullopt;
        return cell;
    }

    Error PartialStore::store(const CellMap& cell) const
    {
        std::vector<std::uint8_t> bytes;
        if (Error error = writeCell(cell, bytes))
            return error;
        std::error_code ec;
        std::filesystem::create_directories(mFolder, ec);
        const std::filesystem::path target = pathOf(cell.mCellX, cell.mCellY);
        std::filesystem::path temporary = target;
        temporary += ".tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file)
                return Error{ "partial", 0, "cannot write " + temporary.string() };
            file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!file)
                return Error{ "partial", 0, "cannot write " + temporary.string() };
        }
        std::filesystem::rename(temporary, target, ec);
        if (ec)
            return Error{ "partial", 0, "cannot rename " + temporary.string() + ": " + ec.message() };
        return {};
    }

    void PartialStore::clear() const
    {
        std::error_code ec;
        std::filesystem::remove_all(mFolder, ec);
    }
}
