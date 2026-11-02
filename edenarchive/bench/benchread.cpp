// Eden Archive reference library. Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
// Measurements for task 0052 stage h. Not a test.
//   edenarchive-bench read <pack> <count>      random 64 KiB range reads: latency percentiles
//   edenarchive-bench many <output> <entries>  writes a pack of many small entries, then opens it and reports the time
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <edenarchive/archive.hpp>
#include <edenarchive/writer.hpp>

namespace
{
    using Clock = std::chrono::steady_clock;

    double secondsSince(Clock::time_point start)
    {
        return std::chrono::duration<double>(Clock::now() - start).count();
    }

    // A small fixed pseudo-random generator, so a run is repeatable.
    struct Random
    {
        std::uint64_t mState = 0x9E3779B97F4A7C15ull;
        std::uint64_t next()
        {
            mState ^= mState << 13;
            mState ^= mState >> 7;
            mState ^= mState << 17;
            return mState;
        }
    };

    int readBench(const char* packPath, std::size_t count)
    {
        EdenArchive::Error error;
        const Clock::time_point openStart = Clock::now();
        const auto pack = EdenArchive::Archive::open(packPath, EdenArchive::OpenOptions{}, error);
        if (!pack)
        {
            std::fprintf(stderr, "%s\n", error.describe(packPath).c_str());
            return 2;
        }
        std::printf("open: %.3f s, %zu entries, metadata %llu bytes\n", secondsSince(openStart), pack->entryCount(),
            static_cast<unsigned long long>(pack->info().mMetadataSize));
        std::vector<std::size_t> candidates;
        for (std::size_t i = 0; i < pack->entryCount(); ++i)
        {
            if (pack->entry(i).mSize > 0)
                candidates.push_back(i);
        }
        if (candidates.empty())
            return 2;
        Random random;
        std::vector<double> micros;
        std::vector<std::uint8_t> buffer(65536);
        std::uint64_t bytes = 0;
        for (std::size_t n = 0; n < count; ++n)
        {
            const std::size_t index = candidates[random.next() % candidates.size()];
            const std::uint64_t size = pack->entry(index).mSize;
            const std::size_t length = static_cast<std::size_t>(std::min<std::uint64_t>(size, buffer.size()));
            const std::uint64_t offset = size == length ? 0 : random.next() % (size - length + 1);
            const Clock::time_point start = Clock::now();
            if (const EdenArchive::Error failure = pack->readRange(index, offset, length, buffer.data()))
            {
                std::fprintf(stderr, "%s\n", failure.describe().c_str());
                return 1;
            }
            micros.push_back(secondsSince(start) * 1e6);
            bytes += length;
        }
        std::sort(micros.begin(), micros.end());
        double sum = 0;
        for (const double m : micros)
            sum += m;
        std::printf("reads: %zu of up to 64 KiB (%llu bytes): mean %.1f us, p50 %.1f us, p99 %.1f us, max %.1f us\n",
            micros.size(), static_cast<unsigned long long>(bytes), sum / micros.size(), micros[micros.size() / 2],
            micros[micros.size() * 99 / 100], micros.back());
        return 0;
    }

    int manyBench(const char* output, std::size_t count)
    {
        std::vector<EdenArchive::InputEntry> entries(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            char name[80];
            std::snprintf(name, sizeof(name), "meshes/set%03zu/folder%04zu/object_%07zu.nif", i % 997, i % 4093, i);
            entries[i].mPath = name;
            entries[i].mData.assign(64 + i % 200, static_cast<std::uint8_t>(i));
        }
        EdenArchive::WriteOptions options;
        options.mStoreOnly = true;
        const Clock::time_point writeStart = Clock::now();
        if (const EdenArchive::Error error = EdenArchive::writePack(entries, output, options))
        {
            std::fprintf(stderr, "%s\n", error.describe().c_str());
            return 1;
        }
        std::printf("wrote %zu entries in %.2f s\n", count, secondsSince(writeStart));
        return 0;
    }
}

int main(int argc, char** argv)
{
    if (argc == 4 && std::strcmp(argv[1], "read") == 0)
        return readBench(argv[2], std::strtoull(argv[3], nullptr, 10));
    if (argc == 4 && std::strcmp(argv[1], "many") == 0)
        return manyBench(argv[2], std::strtoull(argv[3], nullptr, 10));
    std::fprintf(stderr, "usage: edenarchive-bench read <pack> <count> | many <output> <entries>\n");
    return 2;
}
