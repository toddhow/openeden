#include "file.hpp"

#include <istream>
#include <stdexcept>
#include <string>

#include "rangeread.hpp"

namespace VFS
{
    namespace
    {
        // Size of an already opened stream. The stream is left at its end.
        std::uint64_t streamSize(std::istream& stream)
        {
            stream.seekg(0, std::ios_base::end);
            const std::streamoff end = stream.tellg();
            if (!stream || end < 0)
                throw std::runtime_error("Failed to get the size of a stream: it is not seekable");
            return static_cast<std::uint64_t>(end);
        }
    }

    std::uint64_t File::size()
    {
        const Files::IStreamPtr stream = open();
        if (stream == nullptr)
            throw std::runtime_error("Failed to open a file to get its size");
        return streamSize(*stream);
    }

    void File::readRange(std::uint64_t offset, std::size_t size, void* destination)
    {
        const Files::IStreamPtr stream = open();
        if (stream == nullptr)
            throw std::runtime_error("Failed to open a file to read a range of it");

        const std::uint64_t contentSize = streamSize(*stream);
        if (!isRangeInside(offset, size, contentSize))
            throw std::runtime_error("Failed to read a range of a file: offset " + std::to_string(offset) + " and size "
                + std::to_string(size) + " are outside the content of " + std::to_string(contentSize) + " bytes");
        if (size == 0)
            return;

        stream->seekg(static_cast<std::streamoff>(offset), std::ios_base::beg);
        stream->read(static_cast<char*>(destination), static_cast<std::streamsize>(size));
        if (!*stream || static_cast<std::size_t>(stream->gcount()) != size)
            throw std::runtime_error("Failed to read a range of a file: expected " + std::to_string(size)
                + " bytes, got " + std::to_string(stream->gcount()));
    }
}
