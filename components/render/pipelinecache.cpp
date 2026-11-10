#include "pipelinecache.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <system_error>
#include <utility>

namespace Render::PipelineCache
{
    namespace
    {
        constexpr char magic[4] = { 'E', 'D', 'P', 'C' };
        constexpr std::uint32_t formatVersion = 1;
        // FNV-1a, 64 bit. Not a security measure: it finds a torn or truncated file.
        constexpr std::uint64_t fnvOffset = 14695981039346656037ull;
        constexpr std::uint64_t fnvPrime = 1099511628211ull;

        void put32(std::vector<std::uint8_t>& out, std::uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
        }

        void put64(std::vector<std::uint8_t>& out, std::uint64_t v)
        {
            for (int i = 0; i < 8; ++i)
                out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
        }

        std::uint32_t get32(const std::uint8_t* p)
        {
            std::uint32_t v = 0;
            for (int i = 0; i < 4; ++i)
                v |= static_cast<std::uint32_t>(p[i]) << (8 * i);
            return v;
        }

        std::uint64_t get64(const std::uint8_t* p)
        {
            std::uint64_t v = 0;
            for (int i = 0; i < 8; ++i)
                v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
            return v;
        }

        std::string hex(std::span<const std::uint8_t> bytes)
        {
            static const char digits[] = "0123456789abcdef";
            std::string text;
            for (std::uint8_t b : bytes)
            {
                text.push_back(digits[b >> 4]);
                text.push_back(digits[b & 15]);
            }
            return text;
        }

        bool endsWith(const std::string& text, const char* suffix)
        {
            const std::size_t n = std::strlen(suffix);
            return text.size() >= n && text.compare(text.size() - n, n, suffix) == 0;
        }
    }

    const char* describe(LoadStatus status)
    {
        switch (status)
        {
            case LoadStatus::Ok:
                return "ok";
            case LoadStatus::Missing:
                return "no cache file yet";
            case LoadStatus::TooLarge:
                return "the file is larger than the allowed maximum";
            case LoadStatus::BadWrapper:
                return "not a pipeline cache file of this version, or truncated";
            case LoadStatus::BadChecksum:
                return "the checksum does not match (torn or damaged file)";
            case LoadStatus::BadHeader:
                return "the driver blob's header is invalid";
            case LoadStatus::DeviceMismatch:
                return "the cache was made by another device or driver";
            case LoadStatus::ReadError:
                return "the file could not be read";
        }
        return "unknown";
    }

    std::string devicePrefix(const DeviceKey& key)
    {
        char text[64];
        std::snprintf(text, sizeof(text), "pipelinecache-%08x-%08x-", static_cast<unsigned>(key.mVendorId),
            static_cast<unsigned>(key.mDeviceId));
        return text;
    }

    std::string fileName(const DeviceKey& key)
    {
        return devicePrefix(key) + hex(key.mCacheUuid) + ".bin";
    }

    std::uint64_t checksum(std::span<const std::uint8_t> data)
    {
        std::uint64_t h = fnvOffset;
        for (std::uint8_t b : data)
        {
            h ^= b;
            h *= fnvPrime;
        }
        return h;
    }

    std::vector<std::uint8_t> wrap(std::span<const std::uint8_t> blob)
    {
        std::vector<std::uint8_t> out;
        out.reserve(wrapperBytes + blob.size());
        out.insert(out.end(), magic, magic + 4);
        put32(out, formatVersion);
        put64(out, blob.size());
        put64(out, checksum(blob));
        out.insert(out.end(), blob.begin(), blob.end());
        return out;
    }

    LoadStatus unwrap(std::span<const std::uint8_t> file, const DeviceKey& key, std::vector<std::uint8_t>& blob)
    {
        blob.clear();
        if (file.size() > maxFileBytes)
            return LoadStatus::TooLarge;
        if (file.size() < wrapperBytes || std::memcmp(file.data(), magic, 4) != 0
            || get32(file.data() + 4) != formatVersion)
            return LoadStatus::BadWrapper;
        if (get64(file.data() + 8) != file.size() - wrapperBytes)
            return LoadStatus::BadWrapper;
        const std::span<const std::uint8_t> payload = file.subspan(wrapperBytes);
        if (checksum(payload) != get64(file.data() + 16))
            return LoadStatus::BadChecksum;
        if (payload.size() < vulkanHeaderBytes)
            return LoadStatus::BadHeader;
        const std::uint32_t headerSize = get32(payload.data());
        if (headerSize < vulkanHeaderBytes || headerSize > payload.size() || get32(payload.data() + 4) != 1)
            return LoadStatus::BadHeader;
        if (get32(payload.data() + 8) != key.mVendorId || get32(payload.data() + 12) != key.mDeviceId
            || std::memcmp(payload.data() + 16, key.mCacheUuid.data(), 16) != 0)
            return LoadStatus::DeviceMismatch;
        blob.assign(payload.begin(), payload.end());
        return LoadStatus::Ok;
    }

    LoadStatus readFile(const std::filesystem::path& path, const DeviceKey& key, std::vector<std::uint8_t>& blob)
    {
        blob.clear();
        std::error_code error;
        if (!std::filesystem::exists(path, error))
            return LoadStatus::Missing;
        const std::uintmax_t size = std::filesystem::file_size(path, error);
        if (error)
            return LoadStatus::ReadError;
        if (size > maxFileBytes)
            return LoadStatus::TooLarge;
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return LoadStatus::ReadError;
        std::vector<std::uint8_t> file(static_cast<std::size_t>(size));
        if (!file.empty() && !in.read(reinterpret_cast<char*>(file.data()), static_cast<std::streamsize>(file.size())))
            return LoadStatus::ReadError;
        return unwrap(file, key, blob);
    }

    bool writeFileAtomic(const std::filesystem::path& path, std::span<const std::uint8_t> blob, std::string& error)
    {
        std::error_code ec;
        if (path.has_parent_path())
            std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            error = "cannot create " + path.parent_path().string() + ": " + ec.message();
            return false;
        }
        std::filesystem::path temp = path;
        temp += ".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                error = "cannot open " + temp.string();
                return false;
            }
            const std::vector<std::uint8_t> file = wrap(blob);
            out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
            out.flush();
            if (!out)
            {
                error = "cannot write " + temp.string();
                out.close();
                std::filesystem::remove(temp, ec);
                return false;
            }
        }
        std::filesystem::rename(temp, path, ec);
        if (ec)
        {
            error = "cannot rename " + temp.string() + ": " + ec.message();
            std::filesystem::remove(temp, ec);
            return false;
        }
        return true;
    }

    std::size_t removeStaleSiblings(const std::filesystem::path& directory, const DeviceKey& key)
    {
        std::error_code ec;
        std::size_t removed = 0;
        const std::string prefix = devicePrefix(key);
        const std::string current = fileName(key);
        for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec))
        {
            const std::string name = it->path().filename().string();
            if (name.rfind(prefix, 0) != 0)
                continue;
            if ((endsWith(name, ".bin") && name != current) || endsWith(name, ".tmp"))
            {
                std::error_code removeError;
                if (std::filesystem::remove(it->path(), removeError))
                    ++removed;
            }
        }
        return removed;
    }

    Saver::Saver(std::filesystem::path path, DeviceKey key, SizeFn size, FetchFn fetch, LogFn log,
        std::chrono::milliseconds interval)
        : mPath(std::move(path))
        , mKey(key)
        , mSize(std::move(size))
        , mFetch(std::move(fetch))
        , mLog(std::move(log))
        , mInterval(interval)
    {
        mThread = std::thread([this] { run(); });
    }

    Saver::~Saver()
    {
        stop();
    }

    void Saver::stop()
    {
        {
            std::lock_guard lock(mMutex);
            mStop = true;
        }
        mWake.notify_all();
        if (mThread.joinable())
            mThread.join();
        saveIfChanged();
    }

    bool Saver::saveIfChanged()
    {
        std::lock_guard lock(mMutex);
        std::size_t size = 0;
        try
        {
            size = mSize();
        }
        catch (...)
        {
            return false;
        }
        // The driver's blob is never smaller than its header; 0 means "cannot say" or "nothing compiled yet".
        if (size <= vulkanHeaderBytes || size == mSavedSize.load())
            return false;
        std::vector<std::uint8_t> bytes;
        bool fetched = false;
        try
        {
            fetched = mFetch(bytes);
        }
        catch (...)
        {
            fetched = false;
        }
        std::string error;
        if (!fetched || bytes.size() <= vulkanHeaderBytes || bytes.size() > maxFileBytes
            || !writeFileAtomic(mPath, bytes, error))
        {
            if (!mWarnedOnce && mLog)
            {
                mWarnedOnce = true;
                mLog(error.empty() ? "could not read the pipeline cache from the driver" : error);
            }
            return false;
        }
        mSavedSize.store(size);
        ++mWrites;
        return true;
    }

    void Saver::run()
    {
        for (;;)
        {
            {
                std::unique_lock lock(mMutex);
                if (mWake.wait_for(lock, mInterval, [this] { return mStop; }))
                    return;
            }
            saveIfChanged();
        }
    }
}
