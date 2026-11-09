#include "bakestatus.hpp"

#include <components/misc/strictjson.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>

namespace WorldLod
{
    namespace
    {
        namespace Json = Misc::StrictJson;
        namespace fs = std::filesystem;
        using nlohmann::json;

        constexpr std::string_view sStatusFormat = "openeden.lod.status";
        constexpr std::string_view sCommandFormat = "openeden.lod.command";
        constexpr int sVersion = 1;
        constexpr std::int64_t sMaxInt = 9007199254740991; // 2^53 - 1: exact in any JSON reader

        const char* stateName(BakerState s)
        {
            switch (s)
            {
                case BakerState::Idle: return "idle";
                case BakerState::Building: return "building";
                case BakerState::Paused: return "paused";
                case BakerState::Complete: return "complete";
                case BakerState::Failed: return "failed";
            }
            return "idle";
        }

        bool isHex64(const std::string& text)
        {
            return text.size() == 64
                && std::all_of(text.begin(), text.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
        }

        template <class Body>
        auto readFile(std::string_view text, std::string_view fileName, std::string_view format, StatusError& error,
            Body body) -> decltype(body(std::declval<const json&>()))
        {
            error = StatusError();
            error.mFile = std::string(fileName);
            Json::Limits limits;
            limits.mMaxBytes = sMaxStatusBytes;
            Json::ParseError parseError;
            const auto parsed = Json::parse(text, limits, parseError);
            if (!parsed)
            {
                error.mLine = parseError.mLine;
                error.mMessage = parseError.mMessage;
                return std::nullopt;
            }
            try
            {
                if (!parsed->is_object())
                    throw Json::FormatError{ "", "expected an object" };
                const auto formatIt = parsed->find("format");
                if (formatIt == parsed->end() || !formatIt->is_string() || formatIt->get<std::string>() != format)
                    throw Json::FormatError{ "/format", "expected \"" + std::string(format) + "\"" };
                const auto versionIt = parsed->find("version");
                if (versionIt == parsed->end() || !versionIt->is_number_integer())
                    throw Json::FormatError{ "/version", "expected an integer" };
                if (versionIt->get<std::int64_t>() > sVersion)
                {
                    error.mNewerVersion = true;
                    throw Json::FormatError{ "/version", "written in a newer format than this reader knows" };
                }
                if (versionIt->get<std::int64_t>() < 1)
                    throw Json::FormatError{ "/version", "expected 1" };
                return body(*parsed);
            }
            catch (const Json::FormatError& e)
            {
                error.mPointer = e.mPointer;
                error.mMessage = e.mMessage;
            }
            return std::nullopt;
        }
    }

    std::string StatusError::format() const
    {
        std::string result = mFile;
        if (mLine != 0)
            result += ":" + std::to_string(mLine);
        if (!mPointer.empty())
            result += ": " + mPointer;
        return result + ": " + mMessage;
    }

    std::optional<BakeStatus> readStatus(std::string_view text, std::string_view fileName, StatusError& error)
    {
        return readFile(text, fileName, sStatusFormat, error, [](const json& value) -> std::optional<BakeStatus> {
            const Json::ObjectView v(value, "",
                { "format", "version", "toolVersion", "pid", "processStartTime", "state", "pauseReason", "heartbeat",
                    "profile", "fingerprint", "generation", "unitsTotal", "unitsDone", "unitsFailed", "level",
                    "levelsTotal", "activity", "lastCell", "etaSeconds", "cacheBytes", "errors", "errorsTotal",
                    "lastCommandId", "logPath" });
            BakeStatus s;
            s.mToolVersion = v.str("toolVersion", sMaxTextBytes, false);
            s.mPid = v.integerOr("pid", 0, sMaxInt, 0);
            s.mProcessStartTime = v.str("processStartTime", sMaxTextBytes, false);
            const std::string state = v.str("state", 16);
            if (state == "idle")
                s.mState = BakerState::Idle;
            else if (state == "building")
                s.mState = BakerState::Building;
            else if (state == "paused")
                s.mState = BakerState::Paused;
            else if (state == "complete")
                s.mState = BakerState::Complete;
            else if (state == "failed")
                s.mState = BakerState::Failed;
            else
                throw Json::FormatError{ v.pointer("state"), "unknown state" };
            s.mPauseReason = v.str("pauseReason", sMaxTextBytes, false);
            s.mHeartbeat = v.integer("heartbeat", 0, sMaxInt);
            s.mProfile = v.str("profile", sMaxTextBytes, false);
            s.mFingerprint = v.str("fingerprint", 64, false);
            if (!s.mFingerprint.empty() && !isHex64(s.mFingerprint))
                throw Json::FormatError{ v.pointer("fingerprint"), "expected 64 lower-case hex characters or empty" };
            s.mGeneration = v.str("generation", 64, false);
            if (!s.mGeneration.empty() && !isHex64(s.mGeneration))
                throw Json::FormatError{ v.pointer("generation"), "expected 64 lower-case hex characters or empty" };
            s.mUnitsTotal = v.integer("unitsTotal", 0, sMaxInt);
            s.mUnitsDone = v.integer("unitsDone", 0, sMaxInt);
            s.mUnitsFailed = v.integerOr("unitsFailed", 0, sMaxInt, 0);
            s.mLevel = v.integerOr("level", 0, 1000, 0);
            s.mLevelsTotal = v.integerOr("levelsTotal", 0, 1000, 0);
            s.mActivity = v.str("activity", sMaxTextBytes, false);
            if (v.has("lastCell"))
            {
                const Json::ObjectView cell(v.object("lastCell"), v.pointer("lastCell"), { "worldspace", "x", "y" });
                s.mLastCell.mWorldspace = cell.str("worldspace", sMaxTextBytes, false);
                s.mLastCell.mX = cell.integerOr("x", -1000000000, 1000000000, 0);
                s.mLastCell.mY = cell.integerOr("y", -1000000000, 1000000000, 0);
            }
            s.mEtaSeconds = v.integerOr("etaSeconds", -1, sMaxInt, -1);
            s.mCacheBytes = v.integerOr("cacheBytes", 0, sMaxInt, 0);
            const json& errors = v.array("errors", sMaxErrors, false);
            for (std::size_t i = 0; i < errors.size(); ++i)
            {
                const Json::ObjectView e(errors[i], v.pointer("errors") + "/" + std::to_string(i), { "unit", "message" });
                s.mErrors.push_back({ e.str("unit", sMaxTextBytes), e.str("message", sMaxTextBytes) });
            }
            s.mErrorsTotal = v.integerOr("errorsTotal", 0, sMaxInt, static_cast<std::int64_t>(s.mErrors.size()));
            s.mLastCommandId = v.integerOr("lastCommandId", 0, sMaxInt, 0);
            s.mLogPath = v.str("logPath", sMaxTextBytes, false);
            return s;
        });
    }

    std::string writeStatus(const BakeStatus& s)
    {
        json errors = json::array();
        for (const BakeError& e : s.mErrors)
            errors.push_back({ { "unit", e.mUnit }, { "message", e.mMessage } });
        json root = { { "format", std::string(sStatusFormat) }, { "version", sVersion },
            { "toolVersion", s.mToolVersion }, { "pid", s.mPid }, { "processStartTime", s.mProcessStartTime },
            { "state", stateName(s.mState) }, { "pauseReason", s.mPauseReason }, { "heartbeat", s.mHeartbeat },
            { "profile", s.mProfile }, { "fingerprint", s.mFingerprint }, { "generation", s.mGeneration },
            { "unitsTotal", s.mUnitsTotal }, { "unitsDone", s.mUnitsDone }, { "unitsFailed", s.mUnitsFailed },
            { "level", s.mLevel }, { "levelsTotal", s.mLevelsTotal }, { "activity", s.mActivity },
            { "lastCell", { { "worldspace", s.mLastCell.mWorldspace }, { "x", s.mLastCell.mX }, { "y", s.mLastCell.mY } } },
            { "etaSeconds", s.mEtaSeconds }, { "cacheBytes", s.mCacheBytes }, { "errors", std::move(errors) },
            { "errorsTotal", s.mErrorsTotal }, { "lastCommandId", s.mLastCommandId }, { "logPath", s.mLogPath } };
        return Json::canonicalText(root);
    }

    std::optional<Command> readCommand(std::string_view text, std::string_view fileName, StatusError& error)
    {
        return readFile(text, fileName, sCommandFormat, error, [](const json& value) -> std::optional<Command> {
            const Json::ObjectView v(value, "", { "format", "version", "id", "command" });
            Command c;
            c.mId = v.integer("id", 0, sMaxInt);
            const std::string kind = v.str("command", 16);
            if (kind == "pause")
                c.mKind = CommandKind::Pause;
            else if (kind == "resume")
                c.mKind = CommandKind::Resume;
            else if (kind == "stop")
                c.mKind = CommandKind::Stop;
            else
                throw Json::FormatError{ v.pointer("command"), "expected pause, resume or stop" };
            return c;
        });
    }

    std::string writeCommand(const Command& command)
    {
        const char* kind = command.mKind == CommandKind::Pause ? "pause"
            : command.mKind == CommandKind::Resume             ? "resume"
                                                               : "stop";
        json root = { { "format", std::string(sCommandFormat) }, { "version", sVersion }, { "id", command.mId },
            { "command", kind } };
        return Json::canonicalText(root);
    }

    Judgement judge(const JudgeInput& in)
    {
        Judgement result;
        if (!in.mFeatureEnabled)
        {
            result.mState = PageState::Off;
            return result;
        }
        if (!in.mStatusFileExists)
        {
            result.mState = PageState::NotBuilt;
            return result;
        }
        if (!in.mStatus)
        {
            result.mState = PageState::Unknown;
            result.mReason = in.mUnreadableReason.empty() ? "The status file cannot be read." : in.mUnreadableReason;
            return result;
        }
        const BakeStatus& s = *in.mStatus;
        switch (s.mState)
        {
            case BakerState::Idle:
                result.mState = PageState::NotBuilt;
                break;
            case BakerState::Building:
            case BakerState::Paused:
            {
                const std::int64_t delta = in.mNow - s.mHeartbeat;
                if (delta > in.mHeartbeatTimeout || delta < -in.mHeartbeatTimeout)
                {
                    result.mState = PageState::Failed;
                    result.mReason = "The baker stopped without finishing: its last sign of life was "
                        + std::to_string(delta) + " seconds ago.";
                }
                else
                    result.mState = s.mState == BakerState::Building ? PageState::Building : PageState::Paused;
                break;
            }
            case BakerState::Complete:
                result.mState = !s.mFingerprint.empty() && s.mFingerprint == in.mActiveFingerprint
                    ? PageState::UpToDate
                    : PageState::OutOfDate;
                break;
            case BakerState::Failed:
                result.mState = PageState::Failed;
                result.mReason = s.mErrors.empty() ? "The baker reported a failure." : s.mErrors.front().mMessage;
                break;
        }
        return result;
    }

    std::optional<std::int64_t> shownEtaSeconds(const BakeStatus& status, std::int64_t minUnits)
    {
        if (status.mUnitsDone < minUnits || status.mEtaSeconds <= 0)
            return std::nullopt;
        return status.mEtaSeconds;
    }

    std::optional<std::string> readSmallFile(const fs::path& path, std::size_t maxBytes, std::string& error)
    {
        std::error_code ec;
        const std::uintmax_t size = fs::file_size(path, ec);
        if (ec)
        {
            error = "The file cannot be read: " + ec.message();
            return std::nullopt;
        }
        if (size > maxBytes)
        {
            error = "The file is larger than " + std::to_string(maxBytes) + " bytes.";
            return std::nullopt;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            error = "The file cannot be opened.";
            return std::nullopt;
        }
        std::string text{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
        if (text.size() > maxBytes)
        {
            error = "The file is larger than " + std::to_string(maxBytes) + " bytes.";
            return std::nullopt;
        }
        return text;
    }

    bool writeFileAtomically(const fs::path& path, std::string_view text, std::string& error)
    {
        std::error_code ec;
        if (path.has_parent_path())
            fs::create_directories(path.parent_path(), ec);
        const fs::path temp = fs::path(path).concat(".tmp");
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            out.flush();
            if (!out)
            {
                out.close();
                fs::remove(temp, ec);
                error = "Cannot write " + temp.string();
                return false;
            }
        }
        fs::rename(temp, path, ec);
        if (ec)
        {
            fs::remove(temp, ec);
            error = "Cannot replace " + path.string() + ": " + ec.message();
            return false;
        }
        return true;
    }

    bool isLodCacheFolder(const fs::path& folder)
    {
        fs::path p = folder.lexically_normal();
        if (p.filename().empty())
            p = p.parent_path();
        const auto lower = [](const fs::path& part) {
            auto s = part.u8string();
            std::string text(s.begin(), s.end());
            for (char& c : text)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            return text;
        };
        if (lower(p.filename()) != "lod")
            return false;
        return lower(p.parent_path().filename()) == "openeden";
    }

    CacheListing listCacheFiles(const fs::path& folder)
    {
        CacheListing listing;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
        {
            std::error_code e2;
            const std::string name = it->path().filename().string();
            if (it->is_symlink(e2))
            {
                it.disable_recursion_pending();
                listing.mSkipped.push_back("link: " + name);
                continue;
            }
            if (!it->is_regular_file(e2))
                continue;
            const std::size_t dot = name.rfind('.');
            std::string ext = dot == std::string::npos ? std::string() : name.substr(dot + 1);
            for (char& c : ext)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            if (ext == "json" || ext == "tmp" || ext == "lock" || ext == "edenpak")
            {
                listing.mFiles.push_back(it->path());
                listing.mBytes += it->file_size(e2);
            }
            else
                listing.mSkipped.push_back("not a cache file: " + name);
        }
        std::sort(listing.mFiles.begin(), listing.mFiles.end());
        return listing;
    }

    std::string deleteCache(const fs::path& folder, const CacheListing& listing, bool bakerAlive, bool gameRunning)
    {
        if (!isLodCacheFolder(folder))
            return "Refused: the folder is not named .../openeden/lod.";
        if (bakerAlive)
            return "Refused: the baker is running. Stop it first.";
        if (gameRunning)
            return "Refused: the game is running.";
        std::error_code ec;
        const fs::path root = fs::weakly_canonical(folder, ec);
        for (const fs::path& file : listing.mFiles)
        {
            const fs::path resolved = fs::weakly_canonical(file, ec);
            const fs::path relative = resolved.lexically_relative(root);
            if (relative.empty() || *relative.begin() == "..")
                return "Refused: " + file.string() + " is not inside the cache folder.";
            if (fs::is_symlink(file, ec) || !fs::is_regular_file(file, ec))
                return "Refused: " + file.string() + " is not a plain file.";
        }
        for (const fs::path& file : listing.mFiles)
        {
            fs::remove(file, ec);
            if (ec)
                return "Could not delete " + file.string() + ": " + ec.message();
        }
        return {};
    }
}
