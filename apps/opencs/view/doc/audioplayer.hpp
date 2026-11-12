#ifndef CSV_DOC_AUDIOPLAYER_H
#define CSV_DOC_AUDIOPLAYER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace CSVDoc
{
    /// Plays a mono PCM buffer for the timeline's waveform track (Editor Module API 1.4, task 0147). Windows only for
    /// now (PlaySound from memory); elsewhere available() is false and play() does nothing. The buffer is copied, so the
    /// module's buffer may change while it plays. Main thread only.
    class AudioPlayer
    {
    public:
        ~AudioPlayer();
        static bool available();
        /// Starts playing from \a fromSeconds. False when nothing can be played (no samples, past the end, or no
        /// output on this platform).
        bool play(const float* samples, std::size_t count, std::uint32_t rate, double fromSeconds);
        void stop();
    private:
        std::vector<char> mBuffer; // must stay alive while it plays
    };
}

#endif
