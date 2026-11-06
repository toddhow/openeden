#include "texturemipsdriver.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include <components/debug/debuglog.hpp>

#include <components/vsgutil/compilecontext.hpp>

namespace TerrainBindings
{
#include <files/shaders/terrain/bindings.glsl>
}

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
        // A swap is made in steps: at most this many new binds in a step, a step stops after this much main thread
        // time (but always does one bind), and a frame spends at most frameBudgetMs on swaps.
        constexpr int maxBindsPerStep = 8;
        constexpr double stepBudgetMs = 0.8;
        constexpr double frameBudgetMs = 1.5;
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
                  hooks.arrayKind = [](std::uint32_t binding) {
                      return binding == static_cast<std::uint32_t>(TerrainBindings::NORMAL_LAYER_BINDING)
                          ? vsgUtil::TextureResize::Kind::Normal
                          : vsgUtil::TextureResize::Kind::Colour;
                  };
                  hooks.beforeArraySwap = [this] {
                      if (mServices.beforeArraySwap)
                          mServices.beforeArraySwap();
                  };
                  hooks.afterArraySwap = [this] {
                      if (mServices.afterArraySwap)
                          mServices.afterArraySwap();
                  };
                  hooks.descriptorReplaced
                      = [this](const vsg::DescriptorImage* old, vsg::ref_ptr<vsg::DescriptorImage> replacement) {
                            if (mServices.arrayDescriptorReplaced)
                                mServices.arrayDescriptorReplaced(old, std::move(replacement));
                        };
                  hooks.bindsReplaced = [this](const auto& pairs) {
                      if (!mServices.bindsReplaced)
                          return;
                      std::map<const vsg::BindDescriptorSet*, vsg::ref_ptr<vsg::BindDescriptorSet>> map(
                          pairs.begin(), pairs.end());
                      mServices.bindsReplaced(map);
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
        if (!mSwapper.swapping())
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
        if (mSwapper.swapping() || !mQueue.empty())
            out << ", swaps in progress (" << mQueue.size() << " more waiting)";
        // The terrain's layer arrays have a line of their own: they are the big textures of a land texture mod.
        std::int64_t arrayFull = 0;
        std::int64_t arrayHeld = 0;
        int arrays = 0;
        int arraysReduced = 0;
        for (const TM::Texture& t : mTextures)
        {
            if (t.layers <= 1)
                continue;
            ++arrays;
            arraysReduced += t.drop > 0 ? 1 : 0;
            arrayFull += TM::chainBytes(t, 0);
            arrayHeld += TM::residentBytes(t);
        }
        if (arrays > 0)
            out << "\nTexture mips: terrain layer arrays: " << arraysReduced << " of " << arrays << " reduced, holding "
                << static_cast<double>(arrayHeld) / mebibyte << " of " << static_cast<double>(arrayFull) / mebibyte
                << " MiB (" << static_cast<double>(arrayFull - arrayHeld) / mebibyte << " MiB freed)";
        if (mServices.memory)
        {
            const Render::VideoMemory::Sample sample = mServices.memory();
            out << "\nTexture mips: driver heapUsage " << static_cast<double>(sample.usageBytes) / mebibyte
                << " of budget " << static_cast<double>(sample.budgetBytes) / mebibyte << " MiB";
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
        if (mServices.poolStats)
        {
            const auto [reserved, available] = mServices.poolStats();
            Log(Debug::Info) << "Texture mips: device memory pools: " << static_cast<double>(reserved) / mebibyte
                             << " MiB reserved, " << static_cast<double>(available) / mebibyte << " MiB free inside";
        }
    }

    void TextureMipsDriver::logSwap(const vsgUtil::TextureMips::SwapResult& result)
    {
        const auto found = std::find_if(
            mTextures.begin(), mTextures.end(), [&](const TM::Texture& t) { return t.id == mJobTexture; });
        std::ostringstream out;
        out << "Texture mips: texture " << mJobTexture;
        if (found != mTextures.end())
        {
            out << " (" << found->width << " x " << found->height;
            if (found->layers > 1)
                out << " x " << found->layers << " layers";
            out << ")";
        }
        out << " drop " << mJobFrom << " -> " << mJobTo << ": " << (result.ok ? "ok" : "FAILED") << ", "
            << result.groups << " groups, " << result.binds << " binds, freed "
            << static_cast<double>(result.bytesBefore - result.bytesAfter) / mebibyte << " MiB in the ledger, "
            << "main thread " << mJobMs << " ms in " << mJobSteps << " steps (build " << result.buildMs << ", compile "
            << result.compileMs << ", replace " << result.replaceMs << ")";
        Log(result.ok ? Debug::Info : Debug::Warning) << out.str();
    }

    void TextureMipsDriver::applyPending()
    {
        if (mHoldQueue && !mSwapper.swapping())
            return; // waiting for the pools to be released after an array swap
        const auto frameBegin = std::chrono::steady_clock::now();
        const auto frameMs = [&] {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameBegin).count();
        };
        do
        {
            if (!mSwapper.swapping())
            {
                if (mQueue.empty())
                {
                    rescan();
                    // A manual level is a command: it is not held back by the hysteresis.
                    TM::Params params = mParams;
                    if (mManual)
                        params.minFramesBetweenChanges = 0;
                    for (const TM::Change& change : TM::plan(mTextures, mLevel, mFrame, params))
                        mQueue.push_back(change);
                    if (mQueue.empty())
                    {
                        mWorking = false;
                        if (mReportIdle)
                        {
                            mReportIdle = false;
                            // Not inside the Log statement: a Log holds the log's lock and status() logs too.
                            const std::string line = status();
                            Log(Debug::Info) << line;
                        }
                        return;
                    }
                }
                const TM::Change change = mQueue.front();
                mQueue.pop_front();
                mJobTexture = change.id;
                mJobFrom = change.fromDrop;
                mJobTo = change.toDrop;
                mJobMs = 0.0;
                mJobSteps = 0;
                if (!mSwapper.beginSwap(change.id, change.toDrop, mFrame))
                {
                    Log(Debug::Warning) << "Texture mips: texture " << change.id << " could not be swapped";
                    continue;
                }
            }
            const auto stepBegin = std::chrono::steady_clock::now();
            const vsgUtil::TextureMips::Swapper::StepResult step = mSwapper.stepSwap(maxBindsPerStep, stepBudgetMs);
            if (!step.waiting)
            {
                mJobMs
                    += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - stepBegin).count();
                ++mJobSteps;
            }
            if (step.done)
            {
                logSwap(step.total);
                mPruneAt = mTime + pruneDelaySeconds;
                // An array's old images are the biggest blocks in VSG's pools. Whatever is allocated before they are
                // returned (the next swap's new image) lands in the free block and keeps it: so the queue waits until
                // the caches are pruned and the pools released (see update).
                const auto done = std::find_if(
                    mTextures.begin(), mTextures.end(), [&](const TM::Texture& t) { return t.id == mJobTexture; });
                if (done != mTextures.end() && done->layers > 1)
                    mHoldQueue = true;
            }
            if (step.waiting)
                return; // the reduced array is still being made on a worker: look again next frame
        } while (frameMs() < frameBudgetMs && (mSwapper.swapping() || !mQueue.empty()));
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
            if (mHoldQueue)
            {
                mHoldQueue = false;
                if (mServices.releasePools)
                {
                    const auto begin = std::chrono::steady_clock::now();
                    const std::int64_t bytes = mServices.releasePools();
                    const double ms
                        = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
                    Log(Debug::Info) << "Texture mips: pools released after an array swap: "
                                     << static_cast<double>(bytes) / mebibyte << " MiB in " << ms << " ms";
                }
            }
        }
        if (mReleaseAt > 0.0 && mTime >= mReleaseAt)
        {
            mReleaseAt = -1.0;
            logMemory("after the caches were pruned, before the pools are released");
            const auto retired = mSwapper.retiredReport();
            Log(Debug::Info) << "Texture mips: " << retired.alive << " of " << retired.retired
                             << " retired binds are still alive (reference count total " << retired.referenceCounts
                             << ")";
            Log(Debug::Info) << "Texture mips: " << retired.imagesAlive << " of " << retired.imagesRetired
                             << " swapped-away images still exist (reference count total "
                             << retired.imageReferenceCounts << ")";
            std::map<std::uint64_t, int> aliveImages;
            for (const std::uint64_t texture : retired.aliveImageTextures)
                ++aliveImages[texture];
            for (const auto& [texture, count] : aliveImages)
                if (count > 0 && (texture == 1 || texture == 2))
                    Log(Debug::Info) << "Texture mips:   " << count << " swapped-away images of texture " << texture
                                     << " still exist";
            std::map<std::uint64_t, int> aliveByTexture;
            for (const std::uint64_t texture : retired.aliveTextures)
                ++aliveByTexture[texture];
            for (const auto& [texture, count] : aliveByTexture)
                Log(Debug::Info) << "Texture mips:   " << count << " retired binds of texture " << texture
                                 << " are still alive";
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
