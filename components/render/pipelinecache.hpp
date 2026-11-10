#ifndef OPENEDEN_RENDER_PIPELINECACHE_H
#define OPENEDEN_RENDER_PIPELINECACHE_H

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace Render::PipelineCache
{
    /*
     * The pure parts of the persistent Vulkan pipeline cache (OpenEden, 2026-11-09). No Vulkan header is included: the
     * driver's blob is bytes here, and the Engine does the Vulkan calls (engine.cpp).
     *
     * Why: VSG creates every graphics pipeline with no VkPipelineCache, so the NVIDIA driver recompiled the same
     * ubershader define sets on the main thread every run (0.7 to 2 s each; the stutter investigation of 2026-11-09).
     * The cache is kept in a file under <user data>/openeden/shadercache, one file per device and driver.
     *
     * A file is: "EDPC", u32 format 1, u64 payload size, u64 FNV-1a hash of the payload, then the driver's blob
     * (which starts with VkPipelineCacheHeaderVersionOne). Little-endian on every platform we build for. The checks
     * here are for a torn, truncated or foreign file; the driver does its own checks of the blob as well.
     */

    // Nothing larger is ever read or written (a driver's cache for this game is a few MiB to a few tens of MiB).
    inline constexpr std::size_t maxFileBytes = 256u * 1024u * 1024u;
    inline constexpr std::size_t wrapperBytes = 24;
    // VkPipelineCacheHeaderVersionOne: u32 size, u32 version (1), u32 vendor, u32 device, 16 bytes cache UUID.
    inline constexpr std::size_t vulkanHeaderBytes = 32;

    struct DeviceKey
    {
        std::uint32_t mVendorId = 0;
        std::uint32_t mDeviceId = 0;
        std::array<std::uint8_t, 16> mCacheUuid{};
    };

    enum class LoadStatus
    {
        Ok,
        Missing, // no file: the normal first run, not worth a log line
        TooLarge,
        BadWrapper, // too short, wrong magic or format, or the declared size does not match
        BadChecksum,
        BadHeader, // the driver blob's header is short, or not version 1, or declares a size that does not fit
        DeviceMismatch, // the blob was made by another device or driver
        ReadError,
    };

    const char* describe(LoadStatus status);

    // pipelinecache-<vendor>-<device>-<uuid>.bin: another GPU or driver version has a file of its own.
    std::string fileName(const DeviceKey& key);
    // The file name's prefix shared by every driver version of one device.
    std::string devicePrefix(const DeviceKey& key);

    std::uint64_t checksum(std::span<const std::uint8_t> data);

    std::vector<std::uint8_t> wrap(std::span<const std::uint8_t> blob);
    // Fills blob with the driver's bytes when the status is Ok; empty otherwise.
    LoadStatus unwrap(std::span<const std::uint8_t> file, const DeviceKey& key, std::vector<std::uint8_t>& blob);

    // Reads at most maxFileBytes (the size is checked before the read).
    LoadStatus readFile(const std::filesystem::path& path, const DeviceKey& key, std::vector<std::uint8_t>& blob);
    // Writes to "<path>.tmp" and renames over path, so a crash leaves the old file or the new one. False with a
    // reason in error.
    bool writeFileAtomic(const std::filesystem::path& path, std::span<const std::uint8_t> blob, std::string& error);

    // Deletes <directory>/pipelinecache-<vendor>-<device>-*.bin of other driver versions (and leftover .tmp files).
    // Returns how many files it removed.
    std::size_t removeStaleSiblings(const std::filesystem::path& directory, const DeviceKey& key);

    /*
     * Saves the cache in the background. Every `interval` it asks for the cache's current size and, when that has
     * changed since the last save, fetches the bytes and writes the file; stop() (and the destructor) do a last save.
     * Nothing here runs on the main thread. Owned by whoever created it (the Engine); the callbacks must stay valid
     * until stop() returns, and are called from the saver's thread only (and from stop()'s caller for the last save).
     */
    class Saver
    {
    public:
        // size: bytes the driver would write now (0 if it cannot say); fetch: fills the bytes, false on failure.
        using SizeFn = std::function<std::size_t()>;
        using FetchFn = std::function<bool(std::vector<std::uint8_t>&)>;
        using LogFn = std::function<void(const std::string&)>;

        Saver(std::filesystem::path path, DeviceKey key, SizeFn size, FetchFn fetch, LogFn log,
            std::chrono::milliseconds interval);
        ~Saver();
        Saver(const Saver&) = delete;
        Saver& operator=(const Saver&) = delete;

        // Ends the thread and saves once more if the cache changed. Idempotent.
        void stop();
        // Number of files written so far (tests).
        std::size_t writes() const { return mWrites.load(); }
        // Saves now on the calling thread if the cache changed since the last save.
        bool saveIfChanged();
        // Tells the saver which size the file loaded at start has, so an unchanged cache is not rewritten.
        void setKnownSize(std::size_t bytes) { mSavedSize.store(bytes); }

    private:
        void run();

        std::filesystem::path mPath;
        DeviceKey mKey;
        SizeFn mSize;
        FetchFn mFetch;
        LogFn mLog;
        std::chrono::milliseconds mInterval;
        std::mutex mMutex; // guards mStop and serialises saves
        std::condition_variable mWake;
        bool mStop = false;
        bool mWarnedOnce = false;
        std::atomic<std::size_t> mSavedSize{ 0 };
        std::atomic<std::size_t> mWrites{ 0 };
        std::thread mThread;
    };
}

#endif
