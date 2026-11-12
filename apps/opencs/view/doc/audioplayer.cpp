#include "audioplayer.hpp"

#include <apps/opencs/model/module/timelinemodel.hpp>

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// PlaySoundW lives in winmm.
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif

namespace CSVDoc
{
    AudioPlayer::~AudioPlayer()
    {
        stop();
    }

    bool AudioPlayer::available()
    {
#ifdef _WIN32
        return true;
#else
        return false;
#endif
    }

    bool AudioPlayer::play(const float* samples, std::size_t count, std::uint32_t rate, double fromSeconds)
    {
        stop();
#ifdef _WIN32
        mBuffer = CSMModule::pcmToWav16(samples, count, rate, fromSeconds);
        if (mBuffer.empty())
            return false;
        return PlaySoundW(reinterpret_cast<LPCWSTR>(mBuffer.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT)
            != FALSE;
#else
        (void)samples;
        (void)count;
        (void)rate;
        (void)fromSeconds;
        return false;
#endif
    }

    void AudioPlayer::stop()
    {
#ifdef _WIN32
        if (!mBuffer.empty())
            PlaySoundW(nullptr, nullptr, 0);
#endif
        mBuffer.clear();
    }
}
