/// Functions used to read raw binary data from .nif files

#ifndef OPENMW_COMPONENTS_NIF_NIFSTREAM_HPP
#define OPENMW_COMPONENTS_NIF_NIFSTREAM_HPP

#include <array>
#include <cassert>
#include <cerrno>
#include <format>
#include <istream>
#include <stdexcept>
#include <stdint.h>
#include <string>
#include <system_error>
#include <type_traits>
#include <vector>

#include <components/files/istreamptr.hpp>
#include <components/files/utils.hpp>
#include <components/misc/endianness.hpp>
#include <components/misc/float16.hpp>

#include <vsg/maths/plane.h>
#include <vsg/maths/quat.h>
#include <vsg/maths/sphere.h>
#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

#include "niftypes.hpp"

namespace ToUTF8
{
    class StatelessUtf8Encoder;
}

namespace Nif
{

    class Reader;

    template <std::size_t numInstances, typename T>
    inline void readBufferOfType(Files::IStreamPtr& pIStream, T* dest)
    {
        static_assert(
            std::is_arithmetic_v<T> || std::is_same_v<T, Misc::float16_t>, "Buffer element type is not arithmetic");
        static_assert(!std::is_same_v<T, bool>, "Buffer element type is boolean");
        pIStream->read((char*)dest, numInstances * sizeof(T));
        if (pIStream->fail())
            throw std::runtime_error(std::format("Failed to read typed ({}) dynamic buffer of {} instances: {}",
                typeid(T).name(), numInstances, std::generic_category().message(errno)));
        if constexpr (Misc::IS_BIG_ENDIAN)
            for (std::size_t i = 0; i < numInstances; i++)
                Misc::swapEndiannessInplace(dest[i]);
    }

    template <std::size_t numInstances, typename T>
    inline void readBufferOfType(Files::IStreamPtr& pIStream, T (&dest)[numInstances])
    {
        readBufferOfType<numInstances>(pIStream, static_cast<T*>(dest));
    }

    template <typename T>
    inline void readDynamicBufferOfType(Files::IStreamPtr& pIStream, T* dest, std::size_t numInstances)
    {
        static_assert(
            std::is_arithmetic_v<T> || std::is_same_v<T, Misc::float16_t>, "Buffer element type is not arithmetic");
        static_assert(!std::is_same_v<T, bool>, "Buffer element type is boolean");
        pIStream->read((char*)dest, numInstances * sizeof(T));
        if (pIStream->fail())
            throw std::runtime_error(std::format("Failed to read typed ({}) dynamic buffer of {} instances: {}",
                typeid(T).name(), numInstances, std::generic_category().message(errno)));
        if constexpr (Misc::IS_BIG_ENDIAN)
            for (std::size_t i = 0; i < numInstances; i++)
                Misc::swapEndiannessInplace(dest[i]);
    }

    class NIFStream;

    template <class T>
    void readRecord(NIFStream& stream, T& value);

    class NIFStream
    {
        const Reader& mReader;
        Files::IStreamPtr mStream;
        const ToUTF8::StatelessUtf8Encoder* mEncoder;
        std::string mBuffer;
        std::size_t mStreamSize;

    public:
        explicit NIFStream(
            const Reader& reader, Files::IStreamPtr&& stream, const ToUTF8::StatelessUtf8Encoder* encoder)
            : mReader(reader)
            , mStream(std::move(stream))
            , mEncoder(encoder)
            , mStreamSize(static_cast<std::size_t>(Files::getStreamSizeLeft(*mStream)))
        {
        }

        const Reader& getFile() const { return mReader; }

        /// Throws if a read of this many bytes could not fit in the stream. Use it before reserving memory from a
        /// count read from the file.
        void ensureFits(std::size_t bytes) { checkStreamSize(bytes); }

        unsigned int getVersion() const;
        unsigned int getUserVersion() const;
        unsigned int getBethVersion() const;

        /// Convert human-readable version numbers into a number that can be compared.
        static constexpr uint32_t generateVersion(uint8_t major, uint8_t minor, uint8_t patch, uint8_t rev)
        {
            return (major << 24) + (minor << 16) + (patch << 8) + rev;
        }

        void skip(size_t size) { mStream->ignore(size); }

        /// The read position in the stream (task 0046: record byte ranges for the NIF editor).
        std::streamoff tell() { return mStream->tellg(); }

        /// Read into a single instance of type
        template <class T>
        void read(T& data)
        {
            readBufferOfType<1>(mStream, &data);
        }

        /// Read multiple instances of type into an array
        template <class T, size_t size>
        void readArray(std::array<T, size>& arr)
        {
            readBufferOfType<size>(mStream, arr.data());
        }

        /// Read instances of type into a dynamic buffer
        template <class T>
        void read(T* dest, size_t size)
        {
            readDynamicBufferOfType<T>(mStream, dest, size);
        }

        /// Read multiple instances of type into a vector
        template <class T>
        void readVector(std::vector<T>& vec, size_t size)
        {
            if (size == 0)
                return;

            checkStreamSize(size * sizeof(T));

            vec.resize(size);
            read(vec.data(), size);
        }

        /// Extract an instance of type
        template <class T>
        T get()
        {
            T data;
            read(data);
            return data;
        }

        /// Read a string of the given length
        std::string getSizedString(size_t length);

        /// Read a string of the length specified in the file
        std::string getSizedString() { return getSizedString(get<uint32_t>()); }

        /// Read a list of strings without using the string table, e.g. the string table itself
        void getSizedStrings(std::vector<std::string>& vec, size_t size);

        /// Read a Bethesda header string that uses a byte for length
        std::string getExportString() { return getSizedString(get<uint8_t>()); }

        /// Read the version string which doesn't start with a number and ends with "\n"
        std::string getVersionString();

        /// Read a sequence of null-terminated strings
        std::string getStringPalette();

        template <class Count, class T, class Read>
        void readVectorOfRecords(Count count, Read&& read, std::vector<T>& values)
        {
            values.clear();
            values.reserve(count);
            for (Count i = 0; i < count; ++i)
            {
                T value;
                read(*this, value);
                values.push_back(std::move(value));
            }
        }

        template <class Count, class T, class Read>
        void readVectorOfRecords(Read&& read, std::vector<T>& values)
        {
            readVectorOfRecords(get<Count>(), std::forward<Read>(read), values);
        }

        template <class Count, class T>
        void readVectorOfRecords(Count count, std::vector<T>& values)
        {
            readVectorOfRecords(count, readRecord<T>, values);
        }

        template <class Count, class T>
        void readVectorOfRecords(std::vector<T>& values)
        {
            readVectorOfRecords<Count>(readRecord<T>, values);
        }

    private:
        void checkStreamSize(std::size_t size);
    };

    template <class T>
    void readRecord(NIFStream& stream, T& value)
    {
        value.read(&stream);
    }

    template <>
    void NIFStream::read<vsg::vec2>(vsg::vec2& vec);
    template <>
    void NIFStream::read<vsg::vec3>(vsg::vec3& vec);
    template <>
    void NIFStream::read<vsg::vec4>(vsg::vec4& vec);
    template <>
    void NIFStream::read<Matrix3>(Matrix3& mat);
    template <>
    void NIFStream::read<vsg::dquat>(vsg::dquat& quat);
    template <>
    void NIFStream::read<vsg::sphere>(vsg::sphere& sphere);
    template <>
    void NIFStream::read<NiTransform>(NiTransform& transform);
    template <>
    void NIFStream::read<NiQuatTransform>(NiQuatTransform& transform);
    template <>
    void NIFStream::read<bool>(bool& data);
    template <>
    void NIFStream::read<std::string>(std::string& str);

    template <>
    void NIFStream::read<vsg::vec2>(vsg::vec2* dest, size_t size);
    template <>
    void NIFStream::read<vsg::vec3>(vsg::vec3* dest, size_t size);
    template <>
    void NIFStream::read<vsg::vec4>(vsg::vec4* dest, size_t size);
    template <>
    void NIFStream::read<Matrix3>(Matrix3* dest, size_t size);
    template <>
    void NIFStream::read<vsg::dquat>(vsg::dquat* dest, size_t size);
    template <>
    void NIFStream::read<vsg::sphere>(vsg::sphere* dest, size_t size);
    template <>
    void NIFStream::read<NiTransform>(NiTransform* dest, size_t size);
    template <>
    void NIFStream::read<NiQuatTransform>(NiQuatTransform* dest, size_t size);
    template <>
    void NIFStream::read<bool>(bool* dest, size_t size);
    template <>
    void NIFStream::read<std::string>(std::string* dest, size_t size);

    // A plane stored as four floats; OSG's Plane(Vec4f) widened them.
    inline vsg::dplane readPlane(NIFStream* nif)
    {
        const vsg::vec4 values = nif->get<vsg::vec4>();
        return vsg::dplane(values[0], values[1], values[2], values[3]);
    }
}

#endif
