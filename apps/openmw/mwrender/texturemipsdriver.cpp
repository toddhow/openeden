#include "texturemipsdriver.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <sstream>

#include <components/debug/debuglog.hpp>

#include <components/vsgutil/compilecontext.hpp>

namespace MWRender
{
    namespace TM = Render::VideoMemory::TextureMips;

    namespace
    {
        constexpr double mebibyte = 1024.0 * 1024.0;
        // How often the scene is scanned for the ledger while nothing is being swapped (the scan walks the whole scene
        // graph: a measurement of its cost is in the task 0080 stage log).
        constexpr double idleScanSeconds = 5.0;
        // The shared caches can free the old image only after the deletion queue has let it go (four frames, a few
        // tens of milliseconds); a second is far past that.
        constexpr double pruneDelaySeconds = 1.0;
        constexpr double releaseDelaySeconds = 0.5;
        // A swapped texture is left alone for this many frames in the test script (the game's default is 300).
        constexpr std::uint64_t scriptHysteresisFrames = 30;
    }

    TextureMipsDriver::TextureMipsDriver(Services services)
        : mServices(std::move(services))
        , mSwapper(
              [this] {
                  vsgUtil::TextureMips::Hooks hooks;
                  hooks.compile = [this](const vsg::ref_ptr<vsg::Object>& object) {
                      return mServices.compile && mServices.compile->compile(object);
                  };
                  hooks.retire = [this](vsg::ref_ptr<vsg::Object> object) {
                      if (mServices.compile)
                          mServices.compile->detach(std::move(object));
                  };
                  return hooks;
              }(),
              TM::Params{})
    {
    }

    bool TextureMipsDriver::setScript(const std::string& text)
    {
        std::vector<Step> steps;
        std::stringstream in(text);
        std::string item;
        while (std::getline(in, item, ','))
        {
            const auto colon = item.find(':');
            if (colon == std::string::npos)
                return false;
            char* end = nullptr;
            const double at = std::strtod(item.substr(0, colon).c_str(), &end);
            const long level = std::strtol(item.substr(colon + 1).c_str(), nullptr, 10);
            if (end == item.c_str() || at < 0.0 || level < 0 || level > maxLevel)
                return false;
            steps.push_back({ at, static_cast<int>(level), false });
        }
        if (steps.empty())
            return false;
        mScript = std::move(steps);
        mParams.minFramesBetweenChanges = scriptHysteresisFrames;
        return true;
    }

    void TextureMipsDriver::account(Render::VideoMemory::Ledger& ledger)
    {
        if (mTime - mLastScan >= idleScanSeconds && !mWorking)
            rescan();
        ledger.set("textures.mips", mSwapper.residentBytes());
    }

    std::int64_t TextureMipsDriver::stepBytes(int level, bool down) const
    {
        return TM::stepBytes(mTextures, level, down, mParams);
    }

    void TextureMipsDriver::setLevel(int level)
    {
        mControllerLevel = std::clamp(level, 0, maxLevel);
        if (!mManual)
        {
            mLevel = mControllerLevel;
            mWorking = true;
        }
    }

    std::string TextureMipsDriver::status()
    {
        rescan();
        std::int64_t full = 0;
        std::int64_t held = 0;
        int reduced = 0;
        int nearCount = 0;
        for (const TM::Texture& t : mTextures)
        {
            full += TM::chainBytes(t, 0);
            held += TM::residentBytes(t);
            reduced += t.drop > 0 ? 1 : 0;
            nearCount += t.visible ? 1 : 0;
        }
        std::ostringstream out;
        out << "Texture mips: level " << mLevel << (mManual ? " (manual)" : " (controller)") << ", " << reduced
            << " of " << mTextures.size() << " textures of 2048 texels or more reduced (" << nearCount
            << " near the camera), the ledger holds " << static_cast<double>(held) / mebibyte << " of "
            << static_cast<double>(full) / mebibyte << " MiB (" << static_cast<double>(full - held) / mebibyte
            << " MiB freed)";
        if (mServices.memory)
        {
            const Render::VideoMemory::Sample sample = mServices.memory();
            out << ", driver heapUsage " << static_cast<double>(sample.usageBytes) / mebibyte << " of budget "
                << static_cast<double>(sample.budgetBytes) / mebibyte << " MiB";
        }
        return out.str();
    }

    std::string TextureMipsDriver::command(const std::string& argument)
    {
        const std::string usage = "Usage: texturemips <0|1|2|off|status>";
        if (argument.empty() || argument == "status")
            return status();
        if (argument == "off")
        {
            mManual.reset();
            mLevel = mControllerLevel;
            mWorking = true;
            return "Texture mips: manual level dropped; " + status();
        }
        if (argument.size() != 1 || argument[0] < '0' || argument[0] > '0' + maxLevel)
            return usage;
        mReportIdle = true;
        mManual = argument[0] - '0';
        mLevel = *mManual;
        mWorking = true;
        return "Texture mips: manual level " + argument + " set, swaps follow over the next frames";
    }

    void TextureMipsDriver::rescan()
    {
        mLastScan = mTime;
        vsg::Node* root = mServices.root ? mServices.root() : nullptr;
        if (!root)
        {
            mTextures.clear();
            return;
        }
        const auto begin = std::chrono::steady_clock::now();
        if (mServices.camera)
            mTextures = mSwapper.scan(*root, mServices.camera(), mFrame);
        else
            mTextures = mSwapper.scan(*root);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        mLastScanMs = ms;
        Log(Debug::Verbose) << "Texture mips: scan found " << mTextures.size() << " textures of 2048 texels or more in "
                            << ms << " ms";
    }

    void TextureMipsDriver::logMemory(const char* when)
    {
        if (!mServices.memory)
            return;
        const Render::VideoMemory::Sample sample = mServices.memory();
        Log(Debug::Info) << "Texture mips: driver memory " << when << ": heapUsage "
                         << static_cast<double>(sample.usageBytes) / mebibyte << " MiB, heapBudget "
                         << static_cast<double>(sample.budgetBytes) / mebibyte << " MiB, textures held "
                         << static_cast<double>(mSwapper.residentBytes()) / mebibyte << " MiB";
    }

    void TextureMipsDriver::applyPending()
    {
        rescan();
        // A manual level is a command: it is not held back by the hysteresis.
        TM::Params params = mParams;
        if (mManual)
            params.minFramesBetweenChanges = 0;
        const std::vector<TM::Change> changes = TM::plan(mTextures, mLevel, mFrame, params);
        if (changes.empty())
        {
            mWorking = false;
            if (mReportIdle)
            {
                mReportIdle = false;
                const std::string line = status(); // not inside the Log statement: a Log holds the log's lock and status() logs too
                Log(Debug::Info) << line;
            }
            return;
        }
        for (const TM::Change& change : changes)
        {
            const auto found = std::find_if(
                mTextures.begin(), mTextures.end(), [&](const TM::Texture& t) { return t.id == change.id; });
            const auto begin = std::chrono::steady_clock::now();
            const vsgUtil::TextureMips::SwapResult result = mSwapper.swap(change.id, change.toDrop, mFrame);
            const double total
                = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
            std::ostringstream out;
            out << "Texture mips: texture " << change.id;
            if (found != mTextures.end())
                out << " (" << found->width << " x " << found->height << ")";
            out << " drop " << change.fromDrop << " -> " << change.toDrop << ": " << (result.ok ? "ok" : "FAILED")
                << ", " << result.groups << " groups, " << result.binds << " binds, freed "
                << static_cast<double>(result.bytesBefore - result.bytesAfter) / mebibyte << " MiB in the ledger, "
                << "main thread " << total << " ms (build " << result.buildMs << ", compile " << result.compileMs
                << ", replace " << result.replaceMs << ")";
            Log(result.ok ? Debug::Info : Debug::Warning) << out.str();
            mPruneAt = mTime + pruneDelaySeconds;
        }
    }

    void TextureMipsDriver::update(float dt)
    {
        mTime += dt;
        ++mFrame;

        for (Step& step : mScript)
        {
            if (step.done || mTime < step.at)
                continue;
            step.done = true;
            Log(Debug::Info) << "Texture mips: script sets level " << step.level << " at " << mTime << " s";
            logMemory("before the swaps");
            setLevel(step.level);
        }

        if (mWorking)
            applyPending();
        else if (mTime - mLastScan >= idleScanSeconds && (mLevel > 0 || mAnyReduced))
        {
            // Textures loaded since the last swap come back at full size, and a restore may have been held back by
            // the hysteresis.
            mWorking = true;
        }
        mAnyReduced = std::any_of(mTextures.begin(), mTextures.end(), [](const TM::Texture& t) { return t.drop > 0; });

        if (!mScript.empty() && mTime >= mNextReport)
        {
            mNextReport = mTime + 10.0;
            if (mServices.pruneCaches)
                mServices.pruneCaches();
            const auto retired = mSwapper.retiredReport();
            Log(Debug::Info) << "Texture mips: at " << mTime << " s " << retired.alive << " of " << retired.retired
                             << " retired binds are alive";
            logMemory("periodic");
        }
        if (mPruneAt > 0.0 && mTime >= mPruneAt)
        {
            mPruneAt = -1.0;
            if (mServices.pruneCaches)
                mServices.pruneCaches();
            if (!mScript.empty())
                mReleaseAt = mTime + releaseDelaySeconds;
        }
        if (mReleaseAt > 0.0 && mTime >= mReleaseAt)
        {
            mReleaseAt = -1.0;
            logMemory("after the caches were pruned, before the pools are released");
            const auto retired = mSwapper.retiredReport();
            Log(Debug::Info) << "Texture mips: " << retired.alive << " of " << retired.retired
                             << " retired binds are still alive (reference count total " << retired.referenceCounts << ")";
            Log(Debug::Info) << "Texture mips: " << retired.imagesAlive << " of " << retired.imagesRetired
                             << " swapped-away images still exist (reference count total "
                             << retired.imageReferenceCounts << ")";
            for (const std::uint64_t texture : retired.aliveTextures)
                Log(Debug::Info) << "Texture mips:   a retired bind of texture " << texture << " is still alive";
            if (mServices.releasePools)
            {
                const auto begin = std::chrono::steady_clock::now();
                const std::int64_t bytes = mServices.releasePools();
                const double ms
                    = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
                Log(Debug::Info) << "Texture mips: pools released " << static_cast<double>(bytes) / mebibyte
                                 << " MiB in " << ms << " ms";
            }
            logMemory("after the pools were released");
        }
    }
}
