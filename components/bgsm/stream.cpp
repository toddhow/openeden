#include "stream.hpp"

#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

#include <format>
#include <stdexcept>
#include <string>

namespace Bgsm
{
    template <>
    void BGSMStream::read<vsg::vec2>(vsg::vec2& vec)
    {
        readBufferOfType(mStream, vec.value);
    }

    template <>
    void BGSMStream::read<vsg::vec3>(vsg::vec3& vec)
    {
        readBufferOfType(mStream, vec.value);
    }

    template <>
    void BGSMStream::read<vsg::vec4>(vsg::vec4& vec)
    {
        readBufferOfType(mStream, vec.value);
    }

    template <>
    void BGSMStream::read<std::string>(std::string& str)
    {
        std::uint32_t length;
        read(length);
        // Prevent potential memory allocation freezes; strings this long are not expected in BGSM
        if (length > 1024)
            throw std::runtime_error("Requested string length is too large: " + std::to_string(length));
        str = std::string(length, '\0');
        mStream->read(str.data(), length);
        if (mStream->fail())
            throw std::runtime_error(std::format(
                "Failed to read sized string of {} chars: {}", length, std::generic_category().message(errno)));
        std::size_t end = str.find('\0');
        if (end != std::string::npos)
            str.erase(end);
    }
}
