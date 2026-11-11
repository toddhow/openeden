#include "facespeech.hpp"

#include <algorithm>

namespace Face
{
    CurvePaths curvePathsFor(std::string_view voicePath)
    {
        CurvePaths paths;
        if (voicePath.empty())
            return paths;
        const std::size_t slash = voicePath.find_last_of("/\\");
        const std::size_t nameStart = slash == std::string_view::npos ? 0 : slash + 1;
        const std::size_t dot = voicePath.find_last_of('.');
        const std::string_view stem
            = (dot != std::string_view::npos && dot > nameStart) ? voicePath.substr(0, dot) : voicePath;
        paths.mBinary.assign(stem);
        paths.mBinary.append(sBinaryExtension);
        paths.mJson.assign(stem);
        paths.mJson.append(sJsonExtension);
        return paths;
    }

    LoadResult loadSpeechCurves(std::string_view voicePath, const ReadFile& read)
    {
        LoadResult result;
        const CurvePaths paths = curvePathsFor(voicePath);
        if (paths.mBinary.empty() || !read)
            return result;

        LoadError error;
        if (const auto bytes = read(paths.mBinary))
        {
            if (auto curves = readBinary(*bytes, paths.mBinary, error))
                result.mCurves = std::make_shared<const Curves>(std::move(*curves));
            else
                result.mWarning = error.format();
            return result;
        }
        if (const auto bytes = read(paths.mJson))
        {
            const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            if (auto curves = readJson(text, paths.mJson, error))
                result.mCurves = std::make_shared<const Curves>(std::move(*curves));
            else
                result.mWarning = error.format();
        }
        return result;
    }

    SpeechCurveCache::SpeechCurveCache(std::size_t maxEntries)
        : mMax(std::max<std::size_t>(1, maxEntries))
    {
        mEntries.reserve(mMax);
    }

    std::shared_ptr<const Curves> SpeechCurveCache::find(
        std::string_view voicePath, const ReadFile& read, std::string* warning)
    {
        ++mClock;
        for (Entry& entry : mEntries)
        {
            if (entry.mPath == voicePath)
            {
                entry.mLastUse = mClock;
                return entry.mCurves;
            }
        }
        LoadResult loaded = loadSpeechCurves(voicePath, read);
        ++mLoads;
        if (warning != nullptr)
            *warning = std::move(loaded.mWarning);
        if (mEntries.size() >= mMax)
        {
            const auto oldest = std::min_element(mEntries.begin(), mEntries.end(),
                [](const Entry& a, const Entry& b) { return a.mLastUse < b.mLastUse; });
            mEntries.erase(oldest);
        }
        mEntries.push_back(Entry{ std::string(voicePath), loaded.mCurves, mClock });
        return loaded.mCurves;
    }

    SpeechFrame SpeechPlayback::update(SpeechCurveCache& cache, const ReadFile& read, std::string_view voicePath,
        double streamSeconds, std::string* warning)
    {
        if (voicePath.empty())
        {
            reset();
            return {};
        }
        if (voicePath != mPath)
        {
            mDriver.reset();
            mPath.assign(voicePath);
            mCurves = cache.find(voicePath, read, warning);
            if (mCurves)
                mDriver.emplace(*mCurves);
        }
        if (!mDriver)
            return {};
        SpeechFrame frame;
        frame.mActive = true;
        if (const auto reduction = mDriver->update(std::max(0.0, streamSeconds)))
        {
            frame.mTalk = reduction->mTalk;
            frame.mBlink = reduction->mBlink;
        }
        return frame;
    }

    void SpeechPlayback::reset()
    {
        mDriver.reset();
        mCurves.reset();
        mPath.clear();
    }
}
