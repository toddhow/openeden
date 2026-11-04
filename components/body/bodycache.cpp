#include "bodycache.hpp"

#include "bodypackage.hpp"

#include <edenarchive/hash.hpp>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>
#include <tuple>

namespace Body
{
    namespace
    {
        constexpr std::string_view manifestMagic = "openeden.bodycache";
        constexpr std::size_t maxPointerBytes = 256;
        constexpr std::size_t packNameHexDigits = 16;

        std::string hexOf(const EdenArchive::Hash& hash)
        {
            return EdenArchive::toHex(hash);
        }

        std::string hashText(std::string_view text)
        {
            EdenArchive::Hasher hasher;
            hasher.update(text.data(), text.size());
            return hexOf(hasher.finalize());
        }

        std::string doubleText(double value)
        {
            char buffer[40];
            std::snprintf(buffer, sizeof(buffer), "%.17g", value);
            return buffer;
        }

        bool hasControl(std::string_view text)
        {
            return std::any_of(text.begin(), text.end(), [](char c) {
                const unsigned char u = static_cast<unsigned char>(c);
                return u < 0x20 || u == 0x7F;
            });
        }

        bool isHex64(std::string_view text)
        {
            return text.size() == 64 && std::all_of(text.begin(), text.end(), [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            });
        }

        std::string oneLine(std::string text)
        {
            for (char& c : text)
            {
                const unsigned char u = static_cast<unsigned char>(c);
                if (u < 0x20 || u == 0x7F)
                {
                    c = ' ';
                }
            }
            return text;
        }

        std::string sanitizeWord(std::string_view text, std::size_t maxLength)
        {
            std::string out;
            for (char c : text)
            {
                if (out.size() >= maxLength)
                {
                    break;
                }
                if (c >= 'A' && c <= 'Z')
                {
                    c = static_cast<char>(c - 'A' + 'a');
                }
                out += ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) ? c : '-';
            }
            return out.empty() ? std::string("x") : out;
        }

        std::vector<std::string> splitTabs(std::string_view line)
        {
            std::vector<std::string> fields;
            std::size_t start = 0;
            while (true)
            {
                const std::size_t tab = line.find('\t', start);
                if (tab == std::string_view::npos)
                {
                    fields.emplace_back(line.substr(start));
                    return fields;
                }
                fields.emplace_back(line.substr(start, tab - start));
                start = tab + 1;
            }
        }

        std::string jsonString(std::string_view text)
        {
            std::string out = "\"";
            for (char c : text)
            {
                const unsigned char u = static_cast<unsigned char>(c);
                if (c == '"' || c == '\\')
                {
                    out += '\\';
                    out += c;
                }
                else if (u < 0x20 || u == 0x7F)
                {
                    char escape[8];
                    std::snprintf(escape, sizeof(escape), "\\u%04x", u);
                    out += escape;
                }
                else
                {
                    out += c;
                }
            }
            return out + "\"";
        }

        bool validWord(std::string_view text)
        {
            return !text.empty() && text.size() <= 128 && !hasControl(text);
        }

        bool validEntryName(std::string_view name)
        {
            constexpr std::string_view prefix = "bodies/";
            constexpr std::string_view suffix = ".bodymesh";
            return isSafePackagePath(name) && name.size() > prefix.size() + suffix.size()
                && name.substr(0, prefix.size()) == prefix && name.substr(name.size() - suffix.size()) == suffix;
        }

        bool validPackName(std::string_view name)
        {
            constexpr std::string_view prefix = "merged-";
            constexpr std::string_view suffix = ".edenpak";
            if (name.size() != prefix.size() + packNameHexDigits + suffix.size()
                || name.substr(0, prefix.size()) != prefix || name.substr(name.size() - suffix.size()) != suffix)
            {
                return false;
            }
            const std::string_view digits = name.substr(prefix.size(), packNameHexDigits);
            return std::all_of(
                digits.begin(), digits.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
        }
    }

    std::string computeBodyKey(const BodyKeyInput& input)
    {
        std::vector<BodyPartKey> parts = input.mParts;
        std::sort(parts.begin(), parts.end(), [](const BodyPartKey& a, const BodyPartKey& b) {
            return std::tie(a.mSlot, a.mRecordId, a.mModelPath, a.mModelDigest, a.mPartType, a.mFlags, a.mRace)
                < std::tie(b.mSlot, b.mRecordId, b.mModelPath, b.mModelDigest, b.mPartType, b.mFlags, b.mRace);
        });
        std::string text = "openeden.bodykey 1\n";
        const auto line = [&text](std::string_view key, std::string_view value) {
            text += key;
            text += '=';
            // A length prefix makes the text unambiguous whatever the value holds.
            text += std::to_string(value.size());
            text += ':';
            text += value;
            text += '\n';
        };
        line("merger", std::to_string(input.mMergerVersion));
        line("layout", std::to_string(input.mLayoutVersion));
        line("weld", doubleText(input.mWeldTolerance));
        line("uv", doubleText(input.mUvTolerance));
        line("normal", doubleText(input.mNormalAngle));
        line("race", input.mRace);
        line("sex", input.mSex);
        line("view", input.mView);
        line("skeleton", input.mSkeletonPath);
        line("skeletonDigest", input.mSkeletonDigest);
        line("parts", std::to_string(parts.size()));
        for (const BodyPartKey& part : parts)
        {
            line("slot", std::to_string(part.mSlot));
            line("record", part.mRecordId);
            line("type", std::to_string(part.mPartType));
            line("flags", std::to_string(part.mFlags));
            line("partRace", part.mRace);
            line("model", part.mModelPath);
            line("modelDigest", part.mModelDigest);
        }
        return hashText(text);
    }

    std::string bodyEntryName(std::string_view race, std::string_view sex, std::string_view view)
    {
        return "bodies/" + sanitizeWord(race, 40) + "-" + sanitizeWord(sex, 16) + "-" + sanitizeWord(view, 16) + "-"
            + hashText(race).substr(0, 8) + ".bodymesh";
    }

    std::string writeManifest(const CacheManifest& manifest)
    {
        std::vector<CacheEntry> entries = manifest.mEntries;
        std::sort(
            entries.begin(), entries.end(), [](const CacheEntry& a, const CacheEntry& b) { return a.mName < b.mName; });
        std::vector<CacheFailure> failures = manifest.mFailures;
        std::sort(failures.begin(), failures.end(), [](const CacheFailure& a, const CacheFailure& b) {
            return std::tie(a.mRace, a.mSex, a.mView) < std::tie(b.mRace, b.mSex, b.mView);
        });
        std::string text;
        text += std::string(manifestMagic) + "\t" + std::to_string(manifest.mManifestVersion) + "\n";
        text += "merger\t" + std::to_string(manifest.mMergerVersion) + "\n";
        text += "layout\t" + std::to_string(manifest.mLayoutVersion) + "\n";
        for (const CacheEntry& entry : entries)
        {
            text += "entry\t" + entry.mName + "\t" + entry.mRace + "\t" + entry.mSex + "\t" + entry.mView + "\t"
                + entry.mKey + "\n";
        }
        for (const CacheFailure& failure : failures)
        {
            text += "failed\t" + failure.mRace + "\t" + failure.mSex + "\t" + failure.mView + "\t"
                + oneLine(failure.mReason) + "\n";
        }
        return text;
    }

    std::optional<CacheManifest> readManifest(std::string_view text, std::string& error)
    {
        const auto fail = [&error](std::size_t lineNumber, const std::string& message) -> std::optional<CacheManifest> {
            error = "line " + std::to_string(lineNumber) + ": " + message;
            return std::nullopt;
        };
        if (text.empty() || text.size() > CacheLimits::sMaxManifestBytes)
        {
            return fail(
                1, "the manifest is empty or over " + std::to_string(CacheLimits::sMaxManifestBytes) + " bytes");
        }
        if (text.back() != '\n')
        {
            return fail(1, "the manifest does not end with a newline");
        }
        CacheManifest manifest;
        std::size_t lineNumber = 0;
        std::size_t position = 0;
        int stage = 0; // 0 magic, 1 merger, 2 layout, 3 entries, 4 failures
        const auto number = [](const std::string& field, std::uint32_t& out) {
            if (field.empty() || field.size() > 9
                || !std::all_of(field.begin(), field.end(), [](char c) { return c >= '0' && c <= '9'; }))
            {
                return false;
            }
            out = static_cast<std::uint32_t>(std::stoul(field));
            return true;
        };
        std::set<std::string> names;
        while (position < text.size())
        {
            const std::size_t end = text.find('\n', position);
            const std::string_view line = text.substr(position, end - position);
            position = end + 1;
            ++lineNumber;
            if (line.size() > CacheLimits::sMaxLineBytes)
            {
                return fail(lineNumber, "the line is over " + std::to_string(CacheLimits::sMaxLineBytes) + " bytes");
            }
            if (std::any_of(line.begin(), line.end(), [](char c) {
                    const unsigned char u = static_cast<unsigned char>(c);
                    return (u < 0x20 && c != '\t') || u == 0x7F;
                }))
            {
                return fail(lineNumber, "the line holds a control character");
            }
            const std::vector<std::string> fields = splitTabs(line);
            if (stage == 0)
            {
                if (fields.size() != 2 || fields[0] != manifestMagic || !number(fields[1], manifest.mManifestVersion))
                {
                    return fail(lineNumber, "not a body cache manifest");
                }
                if (manifest.mManifestVersion != cacheManifestVersion)
                {
                    return fail(lineNumber, "manifest version " + fields[1] + " is not supported");
                }
                stage = 1;
            }
            else if (stage == 1)
            {
                if (fields.size() != 2 || fields[0] != "merger" || !number(fields[1], manifest.mMergerVersion))
                {
                    return fail(lineNumber, "expected 'merger<TAB>number'");
                }
                stage = 2;
            }
            else if (stage == 2)
            {
                if (fields.size() != 2 || fields[0] != "layout" || !number(fields[1], manifest.mLayoutVersion))
                {
                    return fail(lineNumber, "expected 'layout<TAB>number'");
                }
                stage = 3;
            }
            else if (fields[0] == "entry" && stage == 3)
            {
                if (fields.size() != 6 || !validEntryName(fields[1]) || !validWord(fields[2]) || !validWord(fields[3])
                    || !validWord(fields[4]) || !isHex64(fields[5]))
                {
                    return fail(lineNumber, "a malformed entry line");
                }
                if (manifest.mEntries.size() >= CacheLimits::sMaxEntries)
                {
                    return fail(lineNumber, "more than " + std::to_string(CacheLimits::sMaxEntries) + " entries");
                }
                if (!manifest.mEntries.empty() && !(manifest.mEntries.back().mName < fields[1]))
                {
                    return fail(lineNumber, "entry names are not strictly increasing (a duplicate or out of order)");
                }
                manifest.mEntries.push_back({ fields[1], fields[2], fields[3], fields[4], fields[5] });
            }
            else if (fields[0] == "failed")
            {
                if (fields.size() != 5 || !validWord(fields[1]) || !validWord(fields[2]) || !validWord(fields[3])
                    || fields[4].empty() || fields[4].size() > 512)
                {
                    return fail(lineNumber, "a malformed failed line");
                }
                if (manifest.mFailures.size() >= CacheLimits::sMaxEntries)
                {
                    return fail(lineNumber, "more than " + std::to_string(CacheLimits::sMaxEntries) + " failures");
                }
                const CacheFailure next{ fields[1], fields[2], fields[3], fields[4] };
                if (!manifest.mFailures.empty())
                {
                    const CacheFailure& last = manifest.mFailures.back();
                    if (!(std::tie(last.mRace, last.mSex, last.mView) < std::tie(next.mRace, next.mSex, next.mView)))
                    {
                        return fail(lineNumber, "failed lines are not strictly increasing");
                    }
                }
                manifest.mFailures.push_back(next);
                stage = 4;
            }
            else
            {
                return fail(lineNumber, "an unknown or misplaced line");
            }
        }
        if (stage < 3)
        {
            return fail(lineNumber, "the manifest ends before its header is complete");
        }
        return manifest;
    }

    bool isEntryCurrent(const CacheManifest& manifest, std::string_view name, std::string_view key)
    {
        return std::any_of(manifest.mEntries.begin(), manifest.mEntries.end(),
            [&](const CacheEntry& entry) { return entry.mName == name && entry.mKey == key; });
    }

    bool publishCache(
        const std::filesystem::path& folder, const PackageFiles& files, std::string& packName, std::string& error)
    {
        try
        {
            std::error_code code;
            std::filesystem::create_directories(folder, code);
            if (code)
            {
                error = "cannot create '" + folder.generic_string() + "': " + code.message();
                return false;
            }
            const std::string digest = packageDigest(files, error);
            if (digest.empty())
            {
                return false;
            }
            packName = "merged-" + digest.substr(0, packNameHexDigits) + ".edenpak";
            if (!writePackageFile(files, folder / packName, error))
            {
                return false;
            }
            // The pack is complete: now name it. The pointer is written whole under another name and renamed over
            // `current`, so a reader sees the old pointer or the new one and never a half-written one.
            const std::filesystem::path pending = folder / "current.tmp";
            {
                std::ofstream out(pending, std::ios::binary | std::ios::trunc);
                out << packName << "\n";
                out.flush();
                if (!out)
                {
                    error = "cannot write '" + pending.generic_string() + "'";
                    return false;
                }
            }
            std::filesystem::rename(pending, folder / "current", code);
            if (code)
            {
                error = "cannot publish '" + (folder / "current").generic_string() + "': " + code.message();
                return false;
            }
            return true;
        }
        catch (const std::exception& e)
        {
            error = oneLine(std::string("cannot publish the cache: ") + e.what());
            return false;
        }
    }

    std::optional<PackageFiles> loadCurrentCache(const std::filesystem::path& folder, std::string& error)
    {
        try
        {
            std::ifstream in(folder / "current", std::ios::binary);
            if (!in)
            {
                error = "there is no cache: '" + (folder / "current").generic_string() + "' cannot be read";
                return std::nullopt;
            }
            std::string pointer((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (pointer.size() > maxPointerBytes)
            {
                error = "the pointer file 'current' is over " + std::to_string(maxPointerBytes) + " bytes";
                return std::nullopt;
            }
            if (!pointer.empty() && pointer.back() == '\n')
            {
                pointer.pop_back();
            }
            if (!validPackName(pointer))
            {
                error = "the pointer file 'current' does not name a merged-<digest>.edenpak file";
                return std::nullopt;
            }
            return readPackageFile(folder / pointer, error);
        }
        catch (const std::exception& e)
        {
            error = oneLine(std::string("cannot load the cache: ") + e.what());
            return std::nullopt;
        }
    }

    std::string writeStatusJson(const CacheManifest& manifest, std::string_view state)
    {
        std::vector<CacheFailure> failures = manifest.mFailures;
        std::sort(failures.begin(), failures.end(), [](const CacheFailure& a, const CacheFailure& b) {
            return std::tie(a.mRace, a.mSex, a.mView) < std::tie(b.mRace, b.mSex, b.mView);
        });
        std::string out = "{\n";
        out += "  \"state\": " + jsonString(state) + ",\n";
        out += "  \"layoutVersion\": " + std::to_string(manifest.mLayoutVersion) + ",\n";
        out += "  \"mergerVersion\": " + std::to_string(manifest.mMergerVersion) + ",\n";
        out += "  \"bodies\": " + std::to_string(manifest.mEntries.size()) + ",\n";
        if (failures.empty())
        {
            out += "  \"failed\": []\n";
        }
        else
        {
            out += "  \"failed\": [\n";
            for (std::size_t i = 0; i < failures.size(); ++i)
            {
                const CacheFailure& f = failures[i];
                out += "    {\"race\": " + jsonString(f.mRace) + ", \"sex\": " + jsonString(f.mSex)
                    + ", \"view\": " + jsonString(f.mView) + ", \"reason\": " + jsonString(oneLine(f.mReason)) + "}"
                    + (i + 1 < failures.size() ? "," : "") + "\n";
            }
            out += "  ]\n";
        }
        out += "}\n";
        return out;
    }
}
