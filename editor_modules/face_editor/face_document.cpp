#include "face_document.hpp"

#include <components/face/sha256.hpp>

#include <openeden/editor_module/registrar.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <system_error>

namespace face_editor
{
    using namespace openeden::editor;
    namespace fs = std::filesystem;

    namespace
    {
        constexpr const char* kAudioExtensions[] = { ".wav", ".mp3", ".fuz", ".ogg", ".xwm", ".flac" };
        constexpr std::uint32_t kMinFrameRate = 1;

        std::string lowerCase(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        /// "line" for "line.facecurves.json" (any case); the whole name when it does not end with the extension.
        std::string stemOf(const fs::path& curvesPath)
        {
            const std::string name = curvesPath.filename().generic_string();
            const std::string ext = kFileExtension;
            if (name.size() > ext.size() && lowerCase(name.substr(name.size() - ext.size())) == ext)
                return name.substr(0, name.size() - ext.size());
            return curvesPath.stem().generic_string();
        }

        bool safeRelative(const fs::path& relative)
        {
            if (relative.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory())
                return false;
            for (const fs::path& part : relative)
                if (part == "..")
                    return false;
            return true;
        }

        bool isFile(const fs::path& p)
        {
            std::error_code ec;
            return fs::is_regular_file(p, ec);
        }

        bool readWholeFile(const fs::path& path, std::string& bytes, std::string& error)
        {
            std::error_code ec;
            const auto size = fs::file_size(path, ec);
            if (ec)
            {
                error = path.generic_string() + ": cannot read the file (" + ec.message() + ")";
                return false;
            }
            if (size > AudioLimits::sMaxFileBytes)
            {
                error = path.generic_string() + ": the file is too large";
                return false;
            }
            std::ifstream in(path, std::ios::binary);
            bytes.resize(static_cast<std::size_t>(size));
            if (!in.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
            {
                error = path.generic_string() + ": cannot read the file";
                return false;
            }
            return true;
        }

        bool writeFileAtomically(const fs::path& path, const std::string& text, std::string& error)
        {
            const fs::path temp = fs::path(path.generic_string() + ".tmp");
            {
                std::ofstream out(temp, std::ios::binary);
                out.write(text.data(), static_cast<std::streamsize>(text.size()));
                out.flush();
                if (!out)
                {
                    error = "cannot write " + temp.generic_string();
                    std::error_code ec;
                    fs::remove(temp, ec);
                    return false;
                }
            }
            std::error_code ec;
            fs::rename(temp, path, ec);
            if (ec)
            {
                error = "cannot replace " + path.generic_string() + ": " + ec.message();
                std::error_code ignored;
                fs::remove(temp, ignored);
                return false;
            }
            return true;
        }

        double rangeMin(Face::ChannelKind kind) { return kind == Face::ChannelKind::Signed ? -1.0 : 0.0; }
    }

    const std::vector<std::string>& defaultChannelNames()
    {
        static const std::vector<std::string> sNames = { "jawopen", "mouthclose", "mouthfunnel", "mouthpucker",
            "mouthsmileleft", "mouthsmileright", "mouthlowerdownleft", "mouthlowerdownright", "mouthupperupleft",
            "mouthupperupright", "eyeblinkleft", "eyeblinkright", "browinnerup", "browdownleft", "browdownright",
            "gazex", "gazey" };
        return sNames;
    }

    fs::path findAudio(const fs::path& curvesPath, const Face::Curves& curves, const fs::path& audioRoot)
    {
        const fs::path dir = curvesPath.parent_path();
        const std::string stem = stemOf(curvesPath);
        for (const char* ext : kAudioExtensions)
            if (isFile(dir / (stem + ext)))
                return dir / (stem + ext);
        const fs::path recorded(curves.mAudioName);
        if (!curves.mAudioName.empty() && safeRelative(recorded))
        {
            if (isFile(dir / recorded.filename()))
                return dir / recorded.filename();
            if (!audioRoot.empty() && isFile(audioRoot / recorded))
                return audioRoot / recorded;
        }
        return {};
    }

    namespace
    {
        struct LoadedAudio
        {
            Pcm mPcm;
            std::string mSha256;
        };

        bool loadAudioFile(const fs::path& path, LoadedAudio& out, std::string& error)
        {
            std::string bytes;
            if (!readWholeFile(path, bytes, error))
                return false;
            if (!decodeVoiceBytes(bytes, path.generic_string(), out.mPcm, error))
                return false;
            out.mSha256 = Face::sha256Hex(bytes);
            return true;
        }
    }

    bool createCurvesFile(const fs::path& audioPath, const fs::path& outPath, std::uint32_t frameRate, std::string& error)
    {
        if (frameRate < kMinFrameRate || frameRate > Face::Limits::sMaxFrameRate)
        {
            error = "the frame rate must be between 1 and " + std::to_string(Face::Limits::sMaxFrameRate);
            return false;
        }
        LoadedAudio audio;
        if (!loadAudioFile(audioPath, audio, error))
            return false;
        const double duration = audio.mPcm.durationSeconds();
        Face::Curves c;
        c.mAudioName = audioPath.filename().generic_string();
        c.mAudioDurationSeconds = duration;
        c.mAudioSha256 = audio.mSha256;
        c.mGeneratorName = "manual";
        c.mGeneratorVersion = "1";
        c.mFrameRate = frameRate;
        // Within one frame of the audio, as Face::validate requires (frameCount / frameRate against the duration): the
        // smallest count that covers it. (A real Morrowind line with 45.6 frames failed with "round + 1".)
        const auto frames = std::max<std::uint64_t>(1, static_cast<std::uint64_t>(std::ceil(duration * frameRate - 1e-9)));
        if (frames > Face::Limits::sMaxFrameCount)
        {
            error = "the audio is too long for one curve file";
            return false;
        }
        c.mFrameCount = static_cast<std::uint32_t>(frames);
        for (const std::string& name : defaultChannelNames())
        {
            Face::Channel channel;
            channel.mName = name;
            channel.mKind = name.rfind("gaze", 0) == 0 ? Face::ChannelKind::Signed : Face::ChannelKind::Weight;
            channel.mValues.assign(c.mFrameCount, 0);
            c.mChannels.push_back(std::move(channel));
        }
        const auto text = Face::writeJson(c);
        if (!text)
        {
            const auto issues = Face::validate(c);
            error = "the new curve file is not valid" + (issues.empty() ? std::string() : ": " + issues.front().mMessage);
            return false;
        }
        return writeFileAtomically(outPath, *text, error);
    }

    // ---------------------------------------------------------------------------------------------------------------

    class FaceDocument::Impl
    {
    public:
        struct ChannelState
        {
            CurveChannelInfo mInfo;
            Face::ChannelKind mKind = Face::ChannelKind::Weight;
            std::vector<CurveKey> mKeys;
            bool mEdited = false;
        };

        class KeysCommand final : public Command
        {
        public:
            KeysCommand(Impl& owner, std::size_t channel, std::vector<CurveKey> after, std::string label)
                : mOwner(owner)
                , mChannel(channel)
                , mBefore(owner.mChannels[channel].mKeys)
                , mAfter(std::move(after))
                , mLabel(std::move(label))
            {
            }
            std::string_view label() const override { return mLabel; }
            void redo() override
            {
                mOwner.mChannels[mChannel].mKeys = mAfter;
                mOwner.mChannels[mChannel].mEdited = true;
                ++mOwner.mRevision;
            }
            void undo() override
            {
                mOwner.mChannels[mChannel].mKeys = mBefore;
                --mOwner.mRevision;
            }

        private:
            Impl& mOwner;
            std::size_t mChannel;
            std::vector<CurveKey> mBefore, mAfter;
            std::string mLabel;
        };

        class Curve final : public CurveTrack
        {
        public:
            explicit Curve(Impl& owner)
                : mOwner(owner)
            {
            }
            std::string_view id() const override { return "face.curves"; }
            std::string_view title() const override { return "Face curves"; }
            std::vector<TimelineKey> keys() const override { return {}; }
            std::vector<CurveChannelInfo> channels() const override
            {
                std::vector<CurveChannelInfo> result;
                for (const auto& c : mOwner.mChannels)
                    result.push_back(c.mInfo);
                return result;
            }
            std::vector<CurveKey> channelKeys(std::string_view channel) const override
            {
                const auto* c = mOwner.find(channel);
                return c != nullptr ? c->mKeys : std::vector<CurveKey>{};
            }
            double durationSeconds() const override { return mOwner.duration(); }

            std::unique_ptr<Command> makeAddKeyCommand(std::string_view channel, const CurveKey& key) override
            {
                const std::size_t index = mOwner.indexOf(channel);
                if (index == kNone || !mOwner.keyOk(mOwner.mChannels[index], key))
                    return nullptr;
                auto keys = mOwner.mChannels[index].mKeys;
                insertKey(keys, key);
                return std::make_unique<KeysCommand>(mOwner, index, std::move(keys), "Add key");
            }

            std::unique_ptr<Command> makeMoveKeyCommand(
                std::string_view channel, std::size_t keyIndex, const CurveKey& newKey) override
            {
                const std::size_t index = mOwner.indexOf(channel);
                if (index == kNone || !std::isfinite(newKey.timeSeconds) || !std::isfinite(newKey.value))
                    return nullptr;
                auto keys = mOwner.mChannels[index].mKeys;
                if (keyIndex >= keys.size())
                    return nullptr;
                const double lower = keyIndex > 0 ? keys[keyIndex - 1].timeSeconds : 0.0;
                const double upper = keyIndex + 1 < keys.size() ? keys[keyIndex + 1].timeSeconds : mOwner.duration();
                const auto& info = mOwner.mChannels[index].mInfo;
                keys[keyIndex].timeSeconds = std::clamp(newKey.timeSeconds, lower, std::max(lower, upper));
                keys[keyIndex].value = std::clamp(newKey.value, info.minValue, info.maxValue);
                return std::make_unique<KeysCommand>(mOwner, index, std::move(keys), "Move key");
            }

            std::unique_ptr<Command> makeDeleteKeyCommand(std::string_view channel, std::size_t keyIndex) override
            {
                const std::size_t index = mOwner.indexOf(channel);
                if (index == kNone || keyIndex >= mOwner.mChannels[index].mKeys.size())
                    return nullptr;
                auto keys = mOwner.mChannels[index].mKeys;
                keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(keyIndex));
                return std::make_unique<KeysCommand>(mOwner, index, std::move(keys), "Delete key");
            }

        private:
            Impl& mOwner;
        };

        class Wave final : public WaveformTrack
        {
        public:
            explicit Wave(Impl& owner)
                : mOwner(owner)
            {
            }
            std::string_view id() const override { return "face.audio"; }
            std::string_view title() const override { return "Voice audio"; }
            std::vector<TimelineKey> keys() const override { return {}; }
            WaveformData waveform() const override
            {
                return { mOwner.mPcm.mSamples.data(), mOwner.mPcm.mSamples.size(), mOwner.mPcm.mRate };
            }

        private:
            Impl& mOwner;
        };

        class Props final : public PropertySource
        {
        public:
            explicit Props(Impl& owner)
                : mOwner(owner)
            {
            }
            std::vector<PropertyDesc> describe() const override
            {
                auto row = [](const char* id, const char* label, PropertyType type) {
                    PropertyDesc p;
                    p.id = id;
                    p.label = label;
                    p.type = type;
                    p.readOnly = true;
                    return p;
                };
                return { row("face.audio.name", "Audio (recorded name)", PropertyType::String),
                    row("face.audio.file", "Audio file found", PropertyType::String),
                    row("face.audio.match", "Audio matches the file", PropertyType::String),
                    row("face.audio.duration", "Audio duration (s)", PropertyType::Float),
                    row("face.framerate", "Frames per second", PropertyType::Int),
                    row("face.frames", "Frames", PropertyType::Int), row("face.channels", "Channels", PropertyType::Int),
                    row("face.generator", "Generator", PropertyType::String) };
            }
            PropertyValue get(std::string_view id) const override
            {
                const Face::Curves& c = mOwner.mCurves;
                if (id == "face.audio.name")
                    return c.mAudioName;
                if (id == "face.audio.file")
                    return mOwner.mAudioPath.generic_string();
                if (id == "face.audio.match")
                    return mOwner.matchText();
                if (id == "face.audio.duration")
                    return c.mAudioDurationSeconds;
                if (id == "face.framerate")
                    return static_cast<std::int64_t>(c.mFrameRate);
                if (id == "face.frames")
                    return static_cast<std::int64_t>(c.mFrameCount);
                if (id == "face.channels")
                    return static_cast<std::int64_t>(c.mChannels.size());
                if (id == "face.generator")
                    return c.mGeneratorName + " " + c.mGeneratorVersion;
                return std::string();
            }
            std::unique_ptr<Command> makeSetCommand(std::string_view, const PropertyValue&) override { return nullptr; }

        private:
            Impl& mOwner;
        };

        static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

        Impl()
            : mCurve(*this)
            , mWave(*this)
            , mProps(*this)
        {
        }

        std::size_t indexOf(std::string_view channel) const
        {
            for (std::size_t i = 0; i < mChannels.size(); ++i)
                if (mChannels[i].mInfo.id == channel)
                    return i;
            return kNone;
        }
        const ChannelState* find(std::string_view channel) const
        {
            const std::size_t i = indexOf(channel);
            return i == kNone ? nullptr : &mChannels[i];
        }
        double duration() const
        {
            return mCurves.mFrameCount > 1 ? static_cast<double>(mCurves.mFrameCount - 1) / mCurves.mFrameRate : 0.0;
        }
        bool keyOk(const ChannelState& c, const CurveKey& key) const
        {
            return std::isfinite(key.timeSeconds) && std::isfinite(key.value) && key.timeSeconds >= 0.0
                && key.timeSeconds <= duration() && key.value >= c.mInfo.minValue && key.value <= c.mInfo.maxValue;
        }
        std::string matchText() const
        {
            if (!mHasAudio)
                return "no audio";
            return Face::audioMatches(mCurves, mAudioSha256, mPcm.durationSeconds()) ? "matches" : "differs";
        }

        void tryFindAudio(const fs::path& audioRoot)
        {
            const fs::path found = findAudio(mPath, mCurves, audioRoot);
            if (found.empty())
                return;
            LoadedAudio audio;
            if (loadAudioFile(found, audio, mAudioError))
            {
                mPcm = std::move(audio.mPcm);
                mAudioSha256 = std::move(audio.mSha256);
                mAudioPath = found;
                mHasAudio = true;
            }
        }

        fs::path mPath;
        std::string mId;
        Face::Curves mCurves;
        std::vector<ChannelState> mChannels;
        Pcm mPcm;
        bool mHasAudio = false;
        fs::path mAudioPath;
        std::string mAudioError;
        std::string mAudioSha256;
        bool mAudioChanged = false;
        long long mRevision = 0;
        long long mSavedRevision = 0;
        Curve mCurve;
        Wave mWave;
        Props mProps;
    };

    FaceDocument::FaceDocument(std::unique_ptr<Impl> impl)
        : mImpl(std::move(impl))
    {
    }

    FaceDocument::~FaceDocument() = default;

    std::unique_ptr<FaceDocument> FaceDocument::open(const fs::path& path, const fs::path& audioRoot, std::string& error)
    {
        std::string text;
        std::error_code ec;
        const auto size = fs::file_size(path, ec);
        if (ec)
        {
            error = path.generic_string() + ": cannot read the file (" + ec.message() + ")";
            return nullptr;
        }
        if (size > Face::Limits::sMaxFileBytes)
        {
            error = path.generic_string() + ": the file is too large";
            return nullptr;
        }
        {
            std::ifstream in(path, std::ios::binary);
            text.resize(static_cast<std::size_t>(size));
            if (!in.read(text.data(), static_cast<std::streamsize>(text.size())))
            {
                error = path.generic_string() + ": cannot read the file";
                return nullptr;
            }
        }
        Face::LoadError loadError;
        auto curves = Face::readJson(text, path.generic_string(), loadError);
        if (!curves)
        {
            error = loadError.format();
            return nullptr;
        }
        auto impl = std::make_unique<Impl>();
        impl->mPath = path;
        impl->mId = path.generic_string();
        impl->mCurves = std::move(*curves);
        for (const Face::Channel& channel : impl->mCurves.mChannels)
        {
            Impl::ChannelState state;
            state.mKind = channel.mKind;
            state.mInfo.id = channel.mName;
            state.mInfo.label = channel.mName;
            state.mInfo.minValue = rangeMin(channel.mKind);
            state.mInfo.maxValue = 1.0;
            // A stable colour per channel: the hue follows the channel's place.
            const std::size_t n = impl->mChannels.size();
            const float hue = static_cast<float>(n % 12) / 12.f;
            state.mInfo.colour = Colour{ 0.5f + 0.5f * std::sin(hue * 6.2832f), 0.5f + 0.5f * std::sin(hue * 6.2832f + 2.094f),
                0.5f + 0.5f * std::sin(hue * 6.2832f + 4.188f), 1.f };
            state.mKeys = sparsify(channel.mValues, impl->mCurves.mFrameRate);
            impl->mChannels.push_back(std::move(state));
        }
        impl->tryFindAudio(audioRoot);
        error.clear();
        return std::unique_ptr<FaceDocument>(new FaceDocument(std::move(impl)));
    }

    std::string_view FaceDocument::assetId() const
    {
        return mImpl->mId;
    }

    PropertySource& FaceDocument::properties()
    {
        return mImpl->mProps;
    }

    std::vector<TimelineTrack*> FaceDocument::tracks()
    {
        std::vector<TimelineTrack*> result;
        if (mImpl->mHasAudio)
            result.push_back(&mImpl->mWave);
        result.push_back(&mImpl->mCurve);
        return result;
    }

    bool FaceDocument::isDirty() const
    {
        return mImpl->mRevision != mImpl->mSavedRevision || mImpl->mAudioChanged;
    }

    bool FaceDocument::save(std::string& error)
    {
        Face::Curves out = mImpl->mCurves;
        for (std::size_t i = 0; i < mImpl->mChannels.size(); ++i)
        {
            const auto& state = mImpl->mChannels[i];
            if (!state.mEdited)
                continue;
            const int lo = state.mKind == Face::ChannelKind::Signed ? -Face::Limits::sMaxWeight : 0;
            out.mChannels[i].mValues = resample(state.mKeys, out.mFrameRate, out.mFrameCount, lo, Face::Limits::sMaxWeight);
        }
        const auto text = Face::writeJson(out);
        if (!text)
        {
            const auto issues = Face::validate(out);
            error = "the curves are not valid" + (issues.empty() ? std::string() : ": " + issues.front().mMessage);
            return false;
        }
        if (!writeFileAtomically(mImpl->mPath, *text, error))
            return false;
        mImpl->mCurves = std::move(out);
        mImpl->mSavedRevision = mImpl->mRevision;
        mImpl->mAudioChanged = false;
        return true;
    }

    const Face::Curves& FaceDocument::curves() const
    {
        return mImpl->mCurves;
    }

    const fs::path& FaceDocument::path() const
    {
        return mImpl->mPath;
    }

    const fs::path& FaceDocument::audioPath() const
    {
        return mImpl->mAudioPath;
    }

    const std::string& FaceDocument::audioError() const
    {
        return mImpl->mAudioError;
    }

    std::string FaceDocument::audioMatchText() const
    {
        return mImpl->matchText();
    }

    bool FaceDocument::setAudio(const fs::path& audioPath, std::string& error)
    {
        LoadedAudio audio;
        if (!loadAudioFile(audioPath, audio, error))
            return false;
        mImpl->mPcm = std::move(audio.mPcm);
        mImpl->mAudioSha256 = audio.mSha256;
        mImpl->mAudioPath = audioPath;
        mImpl->mAudioError.clear();
        mImpl->mHasAudio = true;
        mImpl->mCurves.mAudioSha256 = audio.mSha256;
        mImpl->mCurves.mAudioDurationSeconds = mImpl->mPcm.durationSeconds();
        mImpl->mAudioChanged = true;
        return true;
    }

    CurveTrack& FaceDocument::curveTrack()
    {
        return mImpl->mCurve;
    }

    WaveformTrack* FaceDocument::waveformTrack()
    {
        return mImpl->mHasAudio ? &mImpl->mWave : nullptr;
    }

    std::vector<CurveKey> FaceDocument::keysOf(std::string_view channel) const
    {
        const auto* c = mImpl->find(channel);
        return c != nullptr ? c->mKeys : std::vector<CurveKey>{};
    }
}
