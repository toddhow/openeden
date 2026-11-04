#include "bodycache.hpp"

#include "bodypackage.hpp"

#include <edenarchive/hash.hpp>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <exception>
#include <fstream>
#include <functional>
#include <iterator>
#include <set>
#include <system_error>
#include <thread>
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

        // JSON text of `text`: quotes, backslashes and control bytes escaped; a byte that is not part of well-formed
        // UTF-8 (a race id comes from a mod) becomes U+FFFD, so the file is always valid UTF-8. utf8Length is declared
        // below.
        std::size_t utf8Length(std::string_view text);

        std::string jsonString(std::string_view text)
        {
            std::string out = "\"";
            for (std::size_t i = 0; i < text.size();)
            {
                const char c = text[i];
                const unsigned char u = static_cast<unsigned char>(c);
                if (c == '"' || c == '\\')
                {
                    out += '\\';
                    out += c;
                    ++i;
                }
                else if (u < 0x20 || u == 0x7F)
                {
                    char escape[8];
                    std::snprintf(escape, sizeof(escape), "\\u%04x", u);
                    out += escape;
                    ++i;
                }
                else if (u < 0x80)
                {
                    out += c;
                    ++i;
                }
                else if (const std::size_t length = utf8Length(text.substr(i)); length > 0)
                {
                    out.append(text.substr(i, length));
                    i += length;
                }
                else
                {
                    out += "\\ufffd";
                    ++i;
                }
            }
            return out + "\"";
        }

        // A race, sex or view as read back from the manifest (already unescaped): any bytes, one to 128 of them.
        bool validWord(std::string_view text)
        {
            return !text.empty() && text.size() <= 128;
        }

        bool needsEscape(unsigned char u)
        {
            return u < 0x20 || u == 0x7F || u == '%';
        }

        // A manifest field as one tab-free, newline-free word: control bytes and '%' as %XX (two lower-case hex
        // digits).
        std::string escapeField(std::string_view text)
        {
            std::string out;
            for (const char c : text)
            {
                const unsigned char u = static_cast<unsigned char>(c);
                if (needsEscape(u))
                {
                    char escape[4];
                    std::snprintf(escape, sizeof(escape), "%%%02x", u);
                    out += escape;
                }
                else
                {
                    out += c;
                }
            }
            return out;
        }

        int hexValue(char c)
        {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            return -1;
        }

        // The inverse of escapeField, strictly canonical: only bytes that need an escape may be escaped, with
        // lower-case digits, so every manifest has exactly one text.
        bool unescapeField(std::string_view text, std::string& out)
        {
            out.clear();
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                if (text[i] != '%')
                {
                    if (needsEscape(static_cast<unsigned char>(text[i])))
                        return false;
                    out += text[i];
                    continue;
                }
                if (i + 2 >= text.size())
                    return false;
                const int high = hexValue(text[i + 1]);
                const int low = hexValue(text[i + 2]);
                if (high < 0 || low < 0)
                    return false;
                const unsigned char value = static_cast<unsigned char>(high * 16 + low);
                if (!needsEscape(value))
                    return false;
                out += static_cast<char>(value);
                i += 2;
            }
            return true;
        }

        // The length of the well-formed UTF-8 sequence at the start of `text` (1 to 4), or 0 when it is not well
        // formed: no overlong forms, no surrogates, nothing above U+10FFFF.
        std::size_t utf8Length(std::string_view text)
        {
            const auto at = [&text](std::size_t i) { return static_cast<unsigned char>(text[i]); };
            const auto cont = [](unsigned char b) { return (b & 0xC0) == 0x80; };
            const unsigned char b0 = at(0);
            if (b0 < 0x80)
                return 1;
            if (b0 >= 0xC2 && b0 <= 0xDF)
                return text.size() >= 2 && cont(at(1)) ? 2 : 0;
            if (b0 >= 0xE0 && b0 <= 0xEF)
            {
                if (text.size() < 3 || !cont(at(1)) || !cont(at(2)))
                    return 0;
                if (b0 == 0xE0 && at(1) < 0xA0)
                    return 0;
                if (b0 == 0xED && at(1) > 0x9F)
                    return 0;
                return 3;
            }
            if (b0 >= 0xF0 && b0 <= 0xF4)
            {
                if (text.size() < 4 || !cont(at(1)) || !cont(at(2)) || !cont(at(3)))
                    return 0;
                if (b0 == 0xF0 && at(1) < 0x90)
                    return 0;
                if (b0 == 0xF4 && at(1) > 0x8F)
                    return 0;
                return 4;
            }
            return 0;
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

    // The key is computed through STRUCTURED BINDINGS of every struct it covers. A structured binding must name every
    // member, so a field added to one of these structs (BodyKeyInput, BodyPartKey, MergeOptions, MergeLimits,
    // SeamDeclaration) stops this file from compiling until the key learns about it: a stored entry can never go stale
    // silently because someone added a merger parameter.
    std::string computeBodyKey(const BodyKeyInput& input)
    {
        const auto& [race, sex, view, partList, skeletonPath, skeletonDigest, seamList, merge, limits, readerVersion,
            mergerVersion, layoutVersion]
            = input;
        std::vector<BodyPartKey> parts = partList;
        std::sort(parts.begin(), parts.end(), [](const BodyPartKey& a, const BodyPartKey& b) {
            return std::tie(a.mSlot, a.mRecordId, a.mModelPath, a.mModelDigest, a.mPartType, a.mFlags, a.mRace)
                < std::tie(b.mSlot, b.mRecordId, b.mModelPath, b.mModelDigest, b.mPartType, b.mFlags, b.mRace);
        });
        std::string text = "openeden.bodykey 2\n";
        const auto line = [&text](std::string_view key, std::string_view value) {
            text += key;
            text += '=';
            // A length prefix makes the text unambiguous whatever the value holds.
            text += std::to_string(value.size());
            text += ':';
            text += value;
            text += '\n';
        };
        const auto number = [](auto value) { return std::to_string(value); };
        line("merger", number(mergerVersion));
        line("layout", number(layoutVersion));
        line("reader", number(readerVersion));
        line("race", race);
        line("sex", sex);
        line("view", view);
        line("skeleton", skeletonPath);
        line("skeletonDigest", skeletonDigest);

        // What the merger is given.
        const auto& [boneOrder, uvTolerance, normalAngle] = merge;
        line("uv", doubleText(uvTolerance));
        line("normal", doubleText(normalAngle));
        line("boneOrder", number(boneOrder.size()));
        for (const std::string& bone : boneOrder)
            line("bone", bone);
        line("seams", number(seamList.size()));
        for (const SeamDeclaration& seam : seamList)
        {
            const auto& [slotA, slotB, tolerance] = seam;
            line("seamA", number(slotA));
            line("seamB", number(slotB));
            line("seamTolerance", doubleText(tolerance));
        }
        const auto& [maxParts, maxVerticesPerPart, maxTrianglesPerPart, maxTotalVertices, maxTotalTriangles,
            maxBonesPerPart, maxSeams, maxPairsPerSeam, maxCandidateChecks, maxNameBytes, maxErrors]
            = limits;
        line("limits",
            number(maxParts) + "," + number(maxVerticesPerPart) + "," + number(maxTrianglesPerPart) + ","
                + number(maxTotalVertices) + "," + number(maxTotalTriangles) + "," + number(maxBonesPerPart) + ","
                + number(maxSeams) + "," + number(maxPairsPerSeam) + "," + number(maxCandidateChecks) + ","
                + number(maxNameBytes) + "," + number(maxErrors));
        // The merger's compile-time constants, from the names it uses.
        line("constants",
            doubleText(maxCoordinate) + "," + doubleText(minGridCell) + "," + doubleText(maxSeamTolerance) + ","
                + doubleText(maxPlacementEntry) + "," + number(maxInputInfluences) + "," + number(maxMergedInfluences)
                + "," + doubleText(uvMergeTolerance) + "," + doubleText(normalMergeAngle));

        line("parts", number(parts.size()));
        for (const BodyPartKey& part : parts)
        {
            const auto& [slot, recordId, partType, flags, partRace, modelPath, modelDigest] = part;
            line("slot", number(slot));
            line("record", recordId);
            line("type", number(partType));
            line("flags", number(flags));
            line("partRace", partRace);
            line("model", modelPath);
            line("modelDigest", modelDigest);
        }
        return hashText(text);
    }

    std::string bodyEntryName(std::string_view race, std::string_view sex, std::string_view view)
    {
        // The digest covers the exact tuple (with separators that cannot be part of a word), 128 bits of it.
        std::string tuple(race);
        tuple += '\0';
        tuple += sex;
        tuple += '\0';
        tuple += view;
        return "bodies/" + sanitizeWord(race, 40) + "-" + sanitizeWord(sex, 16) + "-" + sanitizeWord(view, 16) + "-"
            + hashText(tuple).substr(0, 32) + ".bodymesh";
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
            text += "entry\t" + entry.mName + "\t" + escapeField(entry.mRace) + "\t" + escapeField(entry.mSex) + "\t"
                + escapeField(entry.mView) + "\t" + entry.mKey + "\n";
        }
        for (const CacheFailure& failure : failures)
        {
            text += "failed\t" + escapeField(failure.mRace) + "\t" + escapeField(failure.mSex) + "\t"
                + escapeField(failure.mView) + "\t" + oneLine(failure.mReason) + "\n";
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
                std::string race, sex, view;
                if (fields.size() != 6 || !validEntryName(fields[1]) || !unescapeField(fields[2], race)
                    || !unescapeField(fields[3], sex) || !unescapeField(fields[4], view) || !validWord(race)
                    || !validWord(sex) || !validWord(view) || !isHex64(fields[5]))
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
                manifest.mEntries.push_back({ fields[1], race, sex, view, fields[5] });
            }
            else if (fields[0] == "failed")
            {
                std::string race, sex, view;
                if (fields.size() != 5 || !unescapeField(fields[1], race) || !unescapeField(fields[2], sex)
                    || !unescapeField(fields[3], view) || !validWord(race) || !validWord(sex) || !validWord(view)
                    || fields[4].empty() || fields[4].size() > 512)
                {
                    return fail(lineNumber, "a malformed failed line");
                }
                if (manifest.mFailures.size() >= CacheLimits::sMaxEntries)
                {
                    return fail(lineNumber, "more than " + std::to_string(CacheLimits::sMaxEntries) + " failures");
                }
                const CacheFailure next{ race, sex, view, fields[4] };
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

    namespace
    {
        constexpr std::string_view quickHeader = "openeden.bodyquick\t1\n";

        bool isLowerHexKey(std::string_view key)
        {
            const auto hex = [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); };
            return key.size() == 64 && std::all_of(key.begin(), key.end(), hex);
        }
    }

    std::string writeQuickKeys(const std::map<std::string, std::string>& keys)
    {
        // std::map iterates in name order, which is the canonical order.
        std::string text(quickHeader);
        for (const auto& [name, key] : keys)
        {
            text += name;
            text += '\t';
            text += key;
            text += '\n';
        }
        return text;
    }

    std::optional<std::map<std::string, std::string>> readQuickKeys(std::string_view text, std::string& error)
    {
        const auto fail = [&error](std::size_t line, const std::string& message) {
            error = "quick keys, line " + std::to_string(line) + ": " + message;
            return std::optional<std::map<std::string, std::string>>{};
        };
        if (text.size() > CacheLimits::sMaxManifestBytes)
            return fail(1, "over " + std::to_string(CacheLimits::sMaxManifestBytes) + " bytes");
        if (text.substr(0, quickHeader.size()) != quickHeader)
            return fail(1, "the first line is not \"openeden.bodyquick<tab>1\"");
        std::map<std::string, std::string> keys;
        std::string_view rest = text.substr(quickHeader.size());
        std::size_t lineNumber = 1;
        std::string previous;
        while (!rest.empty())
        {
            ++lineNumber;
            if (keys.size() >= CacheLimits::sMaxEntries)
                return fail(lineNumber, "more than " + std::to_string(CacheLimits::sMaxEntries) + " entries");
            const std::size_t end = rest.find('\n');
            if (end == std::string_view::npos)
                return fail(lineNumber, "the text does not end with a newline");
            const std::string_view line = rest.substr(0, end);
            rest.remove_prefix(end + 1);
            if (line.size() > CacheLimits::sMaxLineBytes)
                return fail(lineNumber, "the line is over " + std::to_string(CacheLimits::sMaxLineBytes) + " bytes");
            const std::size_t tab = line.find('\t');
            if (tab == std::string_view::npos)
                return fail(lineNumber, "no tab between the name and the key");
            const std::string_view name = line.substr(0, tab);
            const std::string_view key = line.substr(tab + 1);
            if (!validEntryName(name))
                return fail(lineNumber, "not a body entry name");
            if (!isLowerHexKey(key))
                return fail(lineNumber, "the key is not 64 lower-case hex digits");
            if (!previous.empty() && std::string_view(previous) >= name)
                return fail(lineNumber, "the names are not strictly increasing");
            previous = std::string(name);
            keys.emplace(std::string(name), std::string(key));
        }
        return keys;
    }

    bool isEntryCurrent(const CacheManifest& manifest, std::string_view name, std::string_view key)
    {
        return std::any_of(manifest.mEntries.begin(), manifest.mEntries.end(),
            [&](const CacheEntry& entry) { return entry.mName == name && entry.mKey == key; });
    }

    namespace
    {
        // A temporary name no other writer in this process, nor (for practical purposes) another process, picks: the
        // target's name, a process-wide counter and a hash of the thread. It affects no output byte.
        std::filesystem::path temporaryPathFor(const std::filesystem::path& target)
        {
            static std::atomic<std::uint64_t> counter{ 0 };
            const std::uint64_t number = counter.fetch_add(1) + 1;
            const std::size_t thread = std::hash<std::thread::id>{}(std::this_thread::get_id());
            char suffix[48];
            std::snprintf(suffix, sizeof(suffix), ".%llx-%zx.tmp", static_cast<unsigned long long>(number), thread);
            std::filesystem::path result = target;
            result += suffix;
            return result;
        }
    }

    bool writeFileAtomic(const std::filesystem::path& path, std::string_view text, std::string& error)
    {
        const std::filesystem::path pending = temporaryPathFor(path);
        std::error_code code;
        try
        {
            {
                std::ofstream out(pending, std::ios::binary | std::ios::trunc);
                out.write(text.data(), static_cast<std::streamsize>(text.size()));
                out.flush();
                const bool wrote = static_cast<bool>(out);
                out.close();
                if (!wrote || out.fail())
                {
                    std::filesystem::remove(pending, code);
                    error = "cannot write '" + pending.generic_string() + "'";
                    return false;
                }
            }
            std::filesystem::rename(pending, path, code);
            if (code)
            {
                const std::string message = code.message();
                std::filesystem::remove(pending, code);
                error = "cannot replace '" + path.generic_string() + "': " + message;
                return false;
            }
            return true;
        }
        catch (const std::exception& e)
        {
            std::filesystem::remove(pending, code);
            error = oneLine(std::string("cannot write '") + path.generic_string() + "': " + e.what());
            return false;
        }
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
            const std::filesystem::path pack = folder / packName;
            // A pack of this name is the same content (the name is the digest): leave it alone when it reads back
            // whole, so a running game that has it open is never written under. A damaged one is replaced below.
            bool present = false;
            if (std::filesystem::exists(pack, code))
            {
                std::string ignored;
                const std::optional<PackageFiles> existing = readPackageFile(pack, ignored);
                present = existing.has_value() && packageDigest(*existing, ignored) == digest;
            }
            if (!present)
            {
                // Written whole under a temporary name and renamed into place: the pack never exists half written.
                const std::filesystem::path pending = temporaryPathFor(pack);
                if (!writePackageFile(files, pending, error))
                {
                    std::filesystem::remove(pending, code);
                    return false;
                }
                std::filesystem::rename(pending, pack, code);
                if (code)
                {
                    const std::string message = code.message();
                    std::filesystem::remove(pending, code);
                    error = "cannot move the pack into place '" + pack.generic_string() + "': " + message;
                    return false;
                }
            }
            // The pack is complete: now name it. The pointer is written whole under another name and renamed over
            // `current`, so a reader sees the old pointer or the new one and never a half-written one.
            return writeFileAtomic(folder / "current", packName + "\n", error);
        }
        catch (const std::exception& e)
        {
            error = oneLine(std::string("cannot publish the cache: ") + e.what());
            return false;
        }
    }

    std::size_t pruneOldPacks(const std::filesystem::path& folder, const std::vector<std::string>& keep)
    {
        std::size_t deleted = 0;
        try
        {
            std::error_code code;
            std::vector<std::filesystem::path> doomed;
            for (std::filesystem::directory_iterator it(folder, code), end; !code && it != end; it.increment(code))
            {
                const std::string name = it->path().filename().generic_string();
                if (validPackName(name) && std::find(keep.begin(), keep.end(), name) == keep.end())
                    doomed.push_back(it->path());
            }
            for (const std::filesystem::path& path : doomed)
            {
                if (std::filesystem::remove(path, code) && !code)
                    ++deleted;
            }
        }
        catch (const std::exception&)
        {
            // Pruning is housekeeping: whatever could not be deleted stays.
        }
        return deleted;
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
