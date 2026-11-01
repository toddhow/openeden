#include "opas/anim/ClipSidecar.hpp"

#include "opas/format/FileReader.hpp"
#include "opas/format/FileWriter.hpp"

#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace opas::anim
{
    namespace
    {

        using format::detail::FileReader;
        using format::detail::Json;
        using format::detail::jsonString;

        constexpr std::string_view kFormatName = "opas.clipsidecar";

        // A rule broken by a complete sidecar: where (JSON pointer) and why. One
        // definition of the rules serves the loader and validateClipSidecar.
        struct Problem
        {
            std::string pointer;
            std::string message;
        };

        // Names must be non-empty and free of control characters (U+0000-U+001F,
        // U+007F), as in the other formats (docs/formats/clipsidecar.md).
        bool isValidName(std::string_view name)
        {
            if (name.empty())
            {
                return false;
            }
            for (const char c : name)
            {
                const auto u = static_cast<unsigned char>(c);
                if (u < 0x20 || u == 0x7f)
                {
                    return false;
                }
            }
            return true;
        }

        std::optional<Problem> findProblem(const ClipSidecar& sidecar)
        {
            std::set<std::string> names;
            for (std::size_t c = 0; c < sidecar.clips.size(); ++c)
            {
                const std::string at = "/clips/" + std::to_string(c);
                const ClipEvents& clip = sidecar.clips[c];
                if (!isValidName(clip.clip))
                {
                    return Problem{ at + "/clip", "must be non-empty, without control characters" };
                }
                if (!names.insert(clip.clip).second)
                {
                    return Problem{ at + "/clip", "duplicate clip \"" + clip.clip + "\"" };
                }
                for (std::size_t e = 0; e < clip.events.size(); ++e)
                {
                    const std::string eventAt = at + "/events/" + std::to_string(e);
                    const ClipEvent& event = clip.events[e];
                    if (!isValidName(event.name))
                    {
                        return Problem{ eventAt + "/name", "must be non-empty, without control characters" };
                    }
                    if (!std::isfinite(event.timeS) || event.timeS < 0.0)
                    {
                        return Problem{ eventAt + "/timeS", "must be finite and at least 0" };
                    }
                    if (e > 0 && event.timeS < clip.events[e - 1].timeS)
                    {
                        return Problem{ eventAt + "/timeS", "events must be sorted by time" };
                    }
                }
            }
            return std::nullopt;
        }

        class SidecarReader : public FileReader
        {
        public:
            using FileReader::FileReader;

            bool readEvent(const Json& value, const std::string& pointer, ClipEvent& out)
            {
                if (!value.is_object())
                {
                    return fail(value, pointer, "must be an event object");
                }
                if (!onlyKeys(value, pointer, { "name", "timeS", "payload" })
                    || !stringField(value, pointer, "name", out.name)
                    || !numberField(value, pointer, "timeS", out.timeS))
                {
                    return false;
                }
                // The payload is optional and may be empty: absent means "" (docs/formats/clipsidecar.md).
                const auto payload = value.find("payload");
                return payload == value.end() || stringOrEmptyField(value, pointer, "payload", out.payload);
            }

            bool readClip(const Json& value, const std::string& pointer, ClipEvents& out)
            {
                if (!value.is_object())
                {
                    return fail(value, pointer, "must be a clip object");
                }
                if (!onlyKeys(value, pointer, { "clip", "events" }) || !stringField(value, pointer, "clip", out.clip))
                {
                    return false;
                }
                const std::string eventsAt = pointer + "/events";
                const Json* events = member(value, pointer, "events");
                if (events == nullptr)
                {
                    return false;
                }
                if (!events->is_array())
                {
                    return fail(*events, eventsAt, "must be an array");
                }
                out.events.resize(events->size());
                for (std::size_t i = 0; i < events->size(); ++i)
                {
                    if (!readEvent((*events)[i], eventsAt + "/" + std::to_string(i), out.events[i]))
                    {
                        return false;
                    }
                }
                return true;
            }

            bool readDocument(const Json& root, ClipSidecar& out)
            {
                if (!readHeader(root, kFormatName, { "format", "version", "clips" }))
                {
                    return false;
                }
                const auto clip
                    = [this](const Json& v, const std::string& p, ClipEvents& o) { return readClip(v, p, o); };
                if (!readArray(root, "clips", out.clips, clip))
                {
                    return false;
                }
                if (const auto problem = findProblem(out))
                {
                    return failAt(root, problem->pointer, problem->message);
                }
                return true;
            }
        };

        std::string writeEvent(const ClipEvent& event)
        {
            std::string out = "        {\"name\": " + jsonString(event.name)
                + ", \"timeS\": " + format::detail::number(event.timeS);
            if (!event.payload.empty())
            {
                out += ", \"payload\": " + jsonString(event.payload);
            }
            return out + "}";
        }

        std::string writeClip(const ClipEvents& clip)
        {
            std::string out = "    {\n      \"clip\": " + jsonString(clip.clip) + ",\n      \"events\": ";
            if (clip.events.empty())
            {
                out += "[]";
            }
            else
            {
                out += "[\n";
                for (std::size_t i = 0; i < clip.events.size(); ++i)
                {
                    out += writeEvent(clip.events[i]);
                    out += i + 1 < clip.events.size() ? ",\n" : "\n";
                }
                out += "      ]";
            }
            return out + "\n    }";
        }

    } // namespace

    ClipSidecarLoadResult loadClipSidecar(std::string_view text, std::string_view fileName)
    {
        ClipSidecarLoadResult result;
        ClipSidecar sidecar;
        const std::optional<format::LoadError> error = format::detail::parseAndRead<SidecarReader>(
            text, fileName, [&sidecar](SidecarReader& r, const Json& root) { return r.readDocument(root, sidecar); });
        if (error)
        {
            result.error = *error;
        }
        else
        {
            result.value = std::move(sidecar);
        }
        return result;
    }

    ClipSidecarLoadResult loadClipSidecarFile(const std::filesystem::path& path)
    {
        ClipSidecarLoadResult result;
        const std::optional<std::string> text = format::detail::readTextFile(path, result.error);
        if (!text)
        {
            return result;
        }
        return loadClipSidecar(*text, format::detail::utf8FileName(path));
    }

    std::string writeClipSidecar(const ClipSidecar& sidecar)
    {
        std::string out = "{\n  \"format\": \"opas.clipsidecar\",\n  \"version\": 1,\n";
        out += "  \"clips\": " + format::detail::arrayBlock(sidecar.clips, writeClip) + "\n}\n";
        return out;
    }

    std::string validateClipSidecar(const ClipSidecar& sidecar)
    {
        const std::optional<Problem> problem = findProblem(sidecar);
        return problem ? problem->pointer + ": " + problem->message : std::string{};
    }

    std::string applyClipSidecar(const ClipSidecar& sidecar, std::span<Clip> clips)
    {
        if (const std::string problem = validateClipSidecar(sidecar); !problem.empty())
        {
            return problem;
        }
        std::map<std::string, std::size_t> clipOfName;
        for (std::size_t i = 0; i < clips.size(); ++i)
        {
            clipOfName.emplace(clips[i].name, i);
        }
        // Check every entry before changing any clip, so a failure changes nothing.
        std::vector<std::size_t> targets;
        for (std::size_t c = 0; c < sidecar.clips.size(); ++c)
        {
            const ClipEvents& entry = sidecar.clips[c];
            const auto found = clipOfName.find(entry.clip);
            if (found == clipOfName.end())
            {
                return "/clips/" + std::to_string(c) + "/clip: no clip named \"" + entry.clip + "\"";
            }
            const double durationS = clips[found->second].durationS;
            for (std::size_t e = 0; e < entry.events.size(); ++e)
            {
                if (entry.events[e].timeS > durationS)
                {
                    return "/clips/" + std::to_string(c) + "/events/" + std::to_string(e)
                        + "/timeS: after the end of clip \"" + entry.clip + "\" (" + format::detail::number(durationS)
                        + " s)";
                }
            }
            targets.push_back(found->second);
        }
        for (std::size_t c = 0; c < sidecar.clips.size(); ++c)
        {
            clips[targets[c]].events = sidecar.clips[c].events;
        }
        return {};
    }

    std::filesystem::path sidecarPathFor(const std::filesystem::path& animationFile)
    {
        std::filesystem::path path = animationFile;
        path.replace_extension(".events.json");
        return path;
    }

} // namespace opas::anim
