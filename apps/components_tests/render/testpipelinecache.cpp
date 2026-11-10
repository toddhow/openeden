#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <thread>

#include <components/render/pipelinecache.hpp>

namespace
{
    using namespace Render::PipelineCache;

    DeviceKey testKey()
    {
        DeviceKey key;
        key.mVendorId = 0x10de;
        key.mDeviceId = 0x2786;
        for (std::size_t i = 0; i < key.mCacheUuid.size(); ++i)
            key.mCacheUuid[i] = static_cast<std::uint8_t>(i + 1);
        return key;
    }

    void put32(std::vector<std::uint8_t>& v, std::size_t at, std::uint32_t x)
    {
        for (int i = 0; i < 4; ++i)
            v[at + i] = static_cast<std::uint8_t>(x >> (8 * i));
    }

    // A driver blob: the 32 byte header of VkPipelineCacheHeaderVersionOne, then some body.
    std::vector<std::uint8_t> makeBlob(const DeviceKey& key, std::size_t bodyBytes = 100)
    {
        std::vector<std::uint8_t> blob(vulkanHeaderBytes + bodyBytes, 0xAB);
        put32(blob, 0, static_cast<std::uint32_t>(vulkanHeaderBytes));
        put32(blob, 4, 1);
        put32(blob, 8, key.mVendorId);
        put32(blob, 12, key.mDeviceId);
        std::memcpy(blob.data() + 16, key.mCacheUuid.data(), 16);
        return blob;
    }

    std::filesystem::path scratch(const char* name)
    {
        auto dir = std::filesystem::temp_directory_path() / "openeden_pipelinecache_test" / name;
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir);
        return dir;
    }

    TEST(RenderPipelineCache, wrap_then_unwrap_round_trips)
    {
        const auto key = testKey();
        const auto blob = makeBlob(key);
        std::vector<std::uint8_t> out;
        ASSERT_EQ(unwrap(wrap(blob), key, out), LoadStatus::Ok);
        EXPECT_EQ(out, blob);
    }

    TEST(RenderPipelineCache, the_wrapper_layout_is_pinned)
    {
        const auto key = testKey();
        const auto file = wrap(makeBlob(key, 4));
        ASSERT_EQ(file.size(), wrapperBytes + vulkanHeaderBytes + 4);
        EXPECT_EQ(std::memcmp(file.data(), "EDPC", 4), 0);
        EXPECT_EQ(file[4], 1);
        EXPECT_EQ(file[8], vulkanHeaderBytes + 4);
    }

    TEST(RenderPipelineCache, every_truncation_is_refused)
    {
        const auto key = testKey();
        const auto file = wrap(makeBlob(key, 40));
        std::vector<std::uint8_t> out;
        for (std::size_t n = 0; n < file.size(); ++n)
        {
            const std::vector<std::uint8_t> cut(file.begin(), file.begin() + n);
            EXPECT_NE(unwrap(cut, key, out), LoadStatus::Ok) << n;
            EXPECT_TRUE(out.empty());
        }
    }

    TEST(RenderPipelineCache, a_flipped_payload_byte_fails_the_checksum)
    {
        const auto key = testKey();
        auto file = wrap(makeBlob(key));
        file[wrapperBytes + 50] ^= 0x40;
        std::vector<std::uint8_t> out;
        EXPECT_EQ(unwrap(file, key, out), LoadStatus::BadChecksum);
    }

    TEST(RenderPipelineCache, wrong_magic_or_format_is_a_bad_wrapper)
    {
        const auto key = testKey();
        std::vector<std::uint8_t> out;
        auto file = wrap(makeBlob(key));
        file[0] = 'X';
        EXPECT_EQ(unwrap(file, key, out), LoadStatus::BadWrapper);
        file = wrap(makeBlob(key));
        file[4] = 2;
        EXPECT_EQ(unwrap(file, key, out), LoadStatus::BadWrapper);
    }

    TEST(RenderPipelineCache, another_device_or_driver_is_discarded)
    {
        const auto key = testKey();
        const auto file = wrap(makeBlob(key));
        std::vector<std::uint8_t> out;
        auto other = key;
        other.mVendorId = 0x1002;
        EXPECT_EQ(unwrap(file, other, out), LoadStatus::DeviceMismatch);
        other = key;
        other.mDeviceId += 1;
        EXPECT_EQ(unwrap(file, other, out), LoadStatus::DeviceMismatch);
        other = key;
        other.mCacheUuid[15] ^= 1;
        EXPECT_EQ(unwrap(file, other, out), LoadStatus::DeviceMismatch);
        EXPECT_TRUE(out.empty());
    }

    TEST(RenderPipelineCache, a_bad_driver_header_is_refused_even_with_a_right_checksum)
    {
        const auto key = testKey();
        std::vector<std::uint8_t> out;
        auto blob = makeBlob(key);
        put32(blob, 4, 2); // header version
        EXPECT_EQ(unwrap(wrap(blob), key, out), LoadStatus::BadHeader);
        blob = makeBlob(key);
        put32(blob, 0, 8); // header size below 32
        EXPECT_EQ(unwrap(wrap(blob), key, out), LoadStatus::BadHeader);
        blob = makeBlob(key);
        put32(blob, 0, 100000); // header size beyond the data
        EXPECT_EQ(unwrap(wrap(blob), key, out), LoadStatus::BadHeader);
        EXPECT_EQ(unwrap(wrap(std::vector<std::uint8_t>(10)), key, out), LoadStatus::BadHeader);
    }

    TEST(RenderPipelineCache, a_declared_size_that_does_not_match_is_refused)
    {
        const auto key = testKey();
        auto file = wrap(makeBlob(key));
        file[8] += 1;
        std::vector<std::uint8_t> out;
        EXPECT_EQ(unwrap(file, key, out), LoadStatus::BadWrapper);
    }

    TEST(RenderPipelineCache, an_oversized_input_is_refused_before_any_work)
    {
        std::vector<std::uint8_t> big(maxFileBytes + 1);
        std::vector<std::uint8_t> out;
        EXPECT_EQ(unwrap(big, testKey(), out), LoadStatus::TooLarge);
    }

    TEST(RenderPipelineCache, an_oversized_file_is_not_read)
    {
        const auto dir = scratch("toolarge");
        const auto path = dir / "big.bin";
        {
            std::ofstream out(path, std::ios::binary);
            out.seekp(static_cast<std::streamoff>(maxFileBytes));
            out.put('x');
        }
        std::vector<std::uint8_t> blob;
        EXPECT_EQ(readFile(path, testKey(), blob), LoadStatus::TooLarge);
        EXPECT_TRUE(blob.empty());
    }

    TEST(RenderPipelineCache, a_missing_file_is_missing_not_an_error)
    {
        std::vector<std::uint8_t> blob;
        EXPECT_EQ(readFile(scratch("missing") / "none.bin", testKey(), blob), LoadStatus::Missing);
    }

    TEST(RenderPipelineCache, write_then_read_round_trips_and_leaves_no_temp_file)
    {
        const auto dir = scratch("roundtrip");
        const auto key = testKey();
        const auto blob = makeBlob(key, 500);
        const auto path = dir / "sub" / fileName(key);
        std::string error;
        ASSERT_TRUE(writeFileAtomic(path, blob, error)) << error;
        std::vector<std::uint8_t> back;
        EXPECT_EQ(readFile(path, key, back), LoadStatus::Ok);
        EXPECT_EQ(back, blob);
        EXPECT_FALSE(std::filesystem::exists(path.string() + ".tmp"));
    }

    TEST(RenderPipelineCache, a_second_write_replaces_the_first)
    {
        const auto dir = scratch("replace");
        const auto key = testKey();
        const auto path = dir / fileName(key);
        std::string error;
        ASSERT_TRUE(writeFileAtomic(path, makeBlob(key, 10), error));
        const auto second = makeBlob(key, 900);
        ASSERT_TRUE(writeFileAtomic(path, second, error));
        std::vector<std::uint8_t> back;
        EXPECT_EQ(readFile(path, key, back), LoadStatus::Ok);
        EXPECT_EQ(back, second);
    }

    TEST(RenderPipelineCache, a_corrupted_file_on_disk_starts_empty)
    {
        const auto dir = scratch("corrupt");
        const auto key = testKey();
        const auto path = dir / fileName(key);
        {
            std::ofstream out(path, std::ios::binary);
            out << "this is not a pipeline cache";
        }
        std::vector<std::uint8_t> blob;
        EXPECT_NE(readFile(path, key, blob), LoadStatus::Ok);
        EXPECT_TRUE(blob.empty());
    }

    TEST(RenderPipelineCache, the_file_name_depends_on_the_device_and_driver)
    {
        auto a = testKey();
        auto b = a;
        b.mCacheUuid[0] ^= 1;
        auto c = a;
        c.mDeviceId += 1;
        EXPECT_NE(fileName(a), fileName(b));
        EXPECT_NE(fileName(a), fileName(c));
        EXPECT_EQ(fileName(a).rfind(devicePrefix(a), 0), 0u);
        EXPECT_EQ(fileName(a), "pipelinecache-000010de-00002786-0102030405060708090a0b0c0d0e0f10.bin");
    }

    TEST(RenderPipelineCache, stale_siblings_of_other_drivers_are_removed_and_other_devices_kept)
    {
        const auto dir = scratch("stale");
        const auto key = testKey();
        auto otherDriver = key;
        otherDriver.mCacheUuid[3] ^= 0xff;
        auto otherDevice = key;
        otherDevice.mDeviceId += 1;
        std::string error;
        ASSERT_TRUE(writeFileAtomic(dir / fileName(key), makeBlob(key), error));
        ASSERT_TRUE(writeFileAtomic(dir / fileName(otherDriver), makeBlob(otherDriver), error));
        ASSERT_TRUE(writeFileAtomic(dir / fileName(otherDevice), makeBlob(otherDevice), error));
        {
            std::ofstream out(dir / (fileName(key) + ".tmp"), std::ios::binary);
            out << "x";
        }
        {
            std::ofstream out(dir / "unrelated.bin", std::ios::binary);
            out << "x";
        }
        EXPECT_EQ(removeStaleSiblings(dir, key), 2u);
        EXPECT_TRUE(std::filesystem::exists(dir / fileName(key)));
        EXPECT_FALSE(std::filesystem::exists(dir / fileName(otherDriver)));
        EXPECT_TRUE(std::filesystem::exists(dir / fileName(otherDevice)));
        EXPECT_TRUE(std::filesystem::exists(dir / "unrelated.bin"));
    }

    TEST(RenderPipelineCache, the_saver_writes_when_the_cache_grows_and_not_when_it_does_not)
    {
        const auto dir = scratch("saver");
        const auto key = testKey();
        std::atomic<std::size_t> size{ 0 };
        Saver saver(dir / fileName(key), key, [&] { return size.load(); },
            [&](std::vector<std::uint8_t>& out) {
                out = makeBlob(key, size.load() - vulkanHeaderBytes);
                return true;
            },
            nullptr, std::chrono::hours(1));
        EXPECT_FALSE(saver.saveIfChanged()); // nothing compiled yet
        size = vulkanHeaderBytes + 50;
        EXPECT_TRUE(saver.saveIfChanged());
        EXPECT_FALSE(saver.saveIfChanged()); // unchanged
        size = vulkanHeaderBytes + 80;
        EXPECT_TRUE(saver.saveIfChanged());
        EXPECT_EQ(saver.writes(), 2u);
        std::vector<std::uint8_t> back;
        EXPECT_EQ(readFile(dir / fileName(key), key, back), LoadStatus::Ok);
        EXPECT_EQ(back.size(), vulkanHeaderBytes + 80);
    }

    TEST(RenderPipelineCache, a_cache_loaded_from_disk_is_not_rewritten_until_it_changes)
    {
        const auto dir = scratch("known");
        const auto key = testKey();
        Saver saver(dir / fileName(key), key, [] { return vulkanHeaderBytes + 50; },
            [&](std::vector<std::uint8_t>& out) {
                out = makeBlob(key, 50);
                return true;
            },
            nullptr, std::chrono::hours(1));
        saver.setKnownSize(vulkanHeaderBytes + 50);
        EXPECT_FALSE(saver.saveIfChanged());
        EXPECT_EQ(saver.writes(), 0u);
    }

    TEST(RenderPipelineCache, the_saver_thread_saves_periodically_and_stop_does_a_last_save)
    {
        const auto dir = scratch("thread");
        const auto key = testKey();
        std::atomic<std::size_t> size{ vulkanHeaderBytes + 20 };
        Saver saver(dir / fileName(key), key, [&] { return size.load(); },
            [&](std::vector<std::uint8_t>& out) {
                out = makeBlob(key, size.load() - vulkanHeaderBytes);
                return true;
            },
            nullptr, std::chrono::milliseconds(10));
        for (int i = 0; i < 500 && saver.writes() == 0; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        EXPECT_GE(saver.writes(), 1u);
        size = vulkanHeaderBytes + 60;
        saver.stop();
        saver.stop(); // idempotent
        std::vector<std::uint8_t> back;
        ASSERT_EQ(readFile(dir / fileName(key), key, back), LoadStatus::Ok);
        EXPECT_EQ(back.size(), vulkanHeaderBytes + 60);
    }

    TEST(RenderPipelineCache, a_failing_fetch_or_write_is_logged_once_and_never_throws)
    {
        const auto dir = scratch("failing");
        const auto key = testKey();
        int logs = 0;
        Saver saver(dir / fileName(key), key, [] { return vulkanHeaderBytes + 50; },
            [](std::vector<std::uint8_t>&) -> bool { throw 1; }, [&](const std::string&) { ++logs; },
            std::chrono::hours(1));
        EXPECT_FALSE(saver.saveIfChanged());
        EXPECT_FALSE(saver.saveIfChanged());
        EXPECT_EQ(logs, 1);
        EXPECT_FALSE(std::filesystem::exists(dir / fileName(key)));
    }

    TEST(RenderPipelineCache, a_blob_that_is_only_a_header_is_not_saved)
    {
        const auto dir = scratch("headeronly");
        const auto key = testKey();
        Saver saver(dir / fileName(key), key, [] { return vulkanHeaderBytes + 10; },
            [&](std::vector<std::uint8_t>& out) {
                out = makeBlob(key, 0);
                return true;
            },
            nullptr, std::chrono::hours(1));
        EXPECT_FALSE(saver.saveIfChanged());
    }
}
