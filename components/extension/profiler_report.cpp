#include "profiler_snapshot.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace openeden::extension
{
    namespace
    {
        // Appends to a fixed buffer; once full it stays full. Never allocates.
        class Writer
        {
        public:
            explicit Writer(std::span<char> out)
                : mOut(out)
            {
            }

            void text(const char* s, bool escapeMarkup)
            {
                for (; *s != '\0'; ++s)
                {
                    put(*s);
                    if (escapeMarkup && *s == '#')
                        put('#');
                }
            }

            void format(const char* fmt, double a, double b, unsigned long long calls)
            {
                char buffer[160];
                const int n = std::snprintf(buffer, sizeof(buffer), fmt, calls, a, b);
                if (n > 0)
                    text(buffer, false);
            }

            void count(const char* fmt, unsigned long long n)
            {
                char buffer[96];
                if (std::snprintf(buffer, sizeof(buffer), fmt, n) > 0)
                    text(buffer, false);
            }

            /// Terminates the buffer; a cut text ends with "..." inside the buffer.
            std::size_t finish()
            {
                if (mOut.empty())
                    return 0;
                if (mCut)
                {
                    // The buffer is full: overwrite its last characters with the dots (as many as fit)
                    const std::size_t dots = std::min<std::size_t>(3, mOut.size() - 1);
                    for (std::size_t i = 0; i < dots; ++i)
                        mOut[mOut.size() - 1 - dots + i] = '.';
                    mLength = mOut.size() - 1;
                }
                mOut[mLength] = '\0';
                return mLength;
            }

        private:
            void put(char c)
            {
                // Always leave the last byte for the terminator
                if (mCut || mLength + 1 >= mOut.size())
                {
                    mCut = true;
                    return;
                }
                mOut[mLength++] = c;
            }

            std::span<char> mOut;
            std::size_t mLength = 0;
            bool mCut = false;
        };
    }

    std::size_t formatProfilerReport(const ProfilerSnapshot& snapshot, std::span<char> out, bool escapeMarkup)
    {
        if (out.empty())
            return 0;
        Writer writer(out);

        if (snapshot.count == 0)
            writer.text("No extension scope was timed in the last frame.\n", false);

        // One block per extension, in the order the extensions first appear; the scopes of an extension are listed in
        // the order they were first used. Quadratic in the number of rows, which is at most MaxRows.
        for (std::size_t i = 0; i < snapshot.count; ++i)
        {
            const ProfilerSnapshot::Row& first = snapshot.rows[i];
            bool seenBefore = false;
            for (std::size_t j = 0; j < i && !seenBefore; ++j)
                seenBefore = std::strcmp(snapshot.rows[j].extensionId, first.extensionId) == 0;
            if (seenBefore)
                continue;

            writer.text("[", false);
            writer.text(first.extensionId, escapeMarkup);
            writer.text("]\n", false);
            for (std::size_t k = i; k < snapshot.count; ++k)
            {
                const ProfilerSnapshot::Row& row = snapshot.rows[k];
                if (std::strcmp(row.extensionId, first.extensionId) != 0)
                    continue;
                writer.text("  ", false);
                writer.text(row.scopeName, escapeMarkup);
                writer.format(": %llu calls, %.3f ms total, %.3f ms max\n", row.stats.totalTimeMs, row.stats.maxTimeMs,
                    static_cast<unsigned long long>(row.stats.callCount));
            }
        }
        if (snapshot.dropped > 0)
            writer.count("(%llu more scopes not shown)\n", snapshot.dropped);
        return writer.finish();
    }
}
