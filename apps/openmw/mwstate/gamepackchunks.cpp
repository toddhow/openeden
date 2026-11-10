#include "gamepackchunks.hpp"

#include <stdexcept>
#include <string>

#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>

namespace MWState
{
    std::size_t countGamePackChunkRecords(std::span<const std::byte> bytes) noexcept
    {
        return bytes.empty() ? 0 : 1;
    }

    void writeGamePackChunkRecord(ESM::ESMWriter& writer, std::span<const std::byte> bytes)
    {
        if (bytes.empty())
            return;
        writer.startRecord(ESM::REC_GPCH);
        writer.startSubRecord("GPDA");
        writer.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        writer.endRecord("GPDA");
        writer.endRecord(ESM::REC_GPCH);
    }

    std::vector<std::byte> readGamePackChunkRecord(ESM::ESMReader& reader, std::size_t maxBytes)
    {
        reader.getSubNameIs("GPDA");
        reader.getSubHeader();
        const std::size_t size = reader.getSubSize();
        if (size > maxBytes)
            throw std::runtime_error("The game pack's save chunks are " + std::to_string(size)
                + " bytes, more than the " + std::to_string(maxBytes) + " that are accepted");
        std::vector<std::byte> bytes(size);
        if (size > 0)
            reader.getExact(bytes.data(), size);
        return bytes;
    }
}
