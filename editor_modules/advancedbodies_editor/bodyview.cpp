#include "bodyview.hpp"

#include <components/body/bodyjson.hpp>
#include <components/body/bodymesh.hpp>

#include <edenarchive/archive.hpp>
#include <edenarchive/error.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <limits>
#include <numeric>
#include <utility>

namespace advancedbodies_editor
{
    namespace
    {
        using openeden::editor::Colour;

        // The design's palette (task 0069, "Stage f, design"): twelve colours that stay apart on a grey background.
        constexpr std::array<Colour, kRegionPaletteSize> kPalette{ {
            { 0.90f, 0.30f, 0.30f, 1.0f },
            { 0.30f, 0.75f, 0.35f, 1.0f },
            { 0.30f, 0.50f, 0.95f, 1.0f },
            { 0.95f, 0.80f, 0.25f, 1.0f },
            { 0.75f, 0.40f, 0.90f, 1.0f },
            { 0.25f, 0.85f, 0.85f, 1.0f },
            { 0.95f, 0.55f, 0.20f, 1.0f },
            { 0.60f, 0.85f, 0.30f, 1.0f },
            { 0.90f, 0.45f, 0.70f, 1.0f },
            { 0.55f, 0.60f, 0.95f, 1.0f },
            { 0.80f, 0.65f, 0.45f, 1.0f },
            { 0.45f, 0.70f, 0.60f, 1.0f },
        } };

        constexpr std::string_view kBodyFileName = "body.json";
        constexpr std::string_view kManifestFileName = "manifest.txt";
        constexpr std::string_view kPointerFileName = "current";
        constexpr std::string_view kPackEnding = ".edenpak";
        constexpr std::string_view kMergedMeshId = "body";
        constexpr std::string_view kMergeToolId = "openeden.bodymerge";

        std::string utf8(const std::filesystem::path& path)
        {
            const std::u8string text = path.generic_u8string();
            return std::string(text.begin(), text.end());
        }

        char lowerAscii(char c)
        {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }

        bool equalsIgnoringAsciiCase(std::string_view a, std::string_view b)
        {
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                return lowerAscii(x) == lowerAscii(y);
            });
        }

        bool endsWithIgnoringAsciiCase(std::string_view text, std::string_view suffix)
        {
            return text.size() >= suffix.size()
                && equalsIgnoringAsciiCase(text.substr(text.size() - suffix.size()), suffix);
        }

        /// The message with the file it is about, unless it names it already.
        std::string about(const std::filesystem::path& path, const std::string& message)
        {
            const std::string name = utf8(path);
            if (message.find(name) != std::string::npos)
            {
                return message;
            }
            return "'" + name + "': " + message;
        }

        std::optional<std::string> textOf(const Body::PackageFiles& files, std::string_view path)
        {
            if (!Body::isSafePackagePath(path))
            {
                return std::nullopt;
            }
            const auto found = files.mFiles.find(std::string(path));
            if (found == files.mFiles.end())
            {
                return std::nullopt;
            }
            return std::string(found->second.begin(), found->second.end());
        }

        std::optional<Body::MergedBody> readPayload(
            const Body::PackageFiles& files, std::string_view path, std::string& error)
        {
            const auto found
                = Body::isSafePackagePath(path) ? files.mFiles.find(std::string(path)) : files.mFiles.end();
            if (found == files.mFiles.end())
            {
                error = "the payload '" + std::string(path) + "' is not a file of the pack";
                return std::nullopt;
            }
            Body::BodyMeshReadResult read = Body::readBodyMesh(found->second);
            if (!read.mBody)
            {
                error = "the payload '" + std::string(path) + "' does not read: " + read.mError;
                return std::nullopt;
            }
            return std::move(read.mBody);
        }

        std::array<double, 3> inMetres(const Body::Vec3& p, double unitsPerMetre)
        {
            return { static_cast<double>(p[0]) / unitsPerMetre, static_cast<double>(p[1]) / unitsPerMetre,
                static_cast<double>(p[2]) / unitsPerMetre };
        }

        /// Per triangle, the first region (map order, then range order) whose range holds it, else -1. Every triangle
        /// is visited once: `next` is a "next free triangle" union-find, so a map of many long ranges costs
        /// O(triangles + ranges). Nothing is coloured when the map has more ranges than the reader allows.
        std::vector<int> assignRegions(const Body::RegionMap& map, std::size_t triangles)
        {
            std::vector<int> regions(triangles, -1);
            std::size_t ranges = 0;
            for (const Body::Region& region : map.mRegions)
            {
                ranges += region.mRanges.size();
            }
            if (ranges > Body::Limits::sMaxRegionRanges)
            {
                return regions;
            }
            std::vector<std::size_t> next(triangles + 1);
            std::iota(next.begin(), next.end(), std::size_t{ 0 });
            const auto findFree = [&next](std::size_t at) {
                while (next[at] != at)
                {
                    next[at] = next[next[at]];
                    at = next[at];
                }
                return at;
            };
            for (std::size_t r = 0; r < map.mRegions.size(); ++r)
            {
                for (const Body::TriangleRange& range : map.mRegions[r].mRanges)
                {
                    const std::size_t begin = std::min<std::size_t>(range.mBegin, triangles);
                    const std::size_t end = std::min<std::size_t>(range.mEnd, triangles);
                    for (std::size_t t = findFree(begin); t < end; t = findFree(t))
                    {
                        regions[t] = static_cast<int>(r);
                        next[t] = t + 1;
                    }
                }
            }
            return regions;
        }

        /// Every distinct edge once, in the order the triangles first use it, coloured by the region of that first
        /// triangle. Sorting (edge key, first use) and keeping the first of each key gives first use without a hash.
        void buildRegionLines(const DrawnMesh& mesh, BodyData& data)
        {
            struct Use
            {
                std::uint64_t mKey;
                std::uint64_t mOrder; // 3 * triangle + edge
            };
            const std::size_t triangles = mesh.mTriangles.size() / 3;
            std::vector<Use> uses;
            uses.reserve(3 * triangles);
            for (std::size_t t = 0; t < triangles; ++t)
            {
                for (std::size_t e = 0; e < 3; ++e)
                {
                    const std::uint32_t a = mesh.mTriangles[3 * t + e];
                    const std::uint32_t b = mesh.mTriangles[3 * t + (e + 1) % 3];
                    const std::uint64_t key = (static_cast<std::uint64_t>(std::min(a, b)) << 32)
                        | static_cast<std::uint64_t>(std::max(a, b));
                    uses.push_back({ key, 3 * static_cast<std::uint64_t>(t) + e });
                }
            }
            std::sort(uses.begin(), uses.end(), [](const Use& left, const Use& right) {
                return left.mKey != right.mKey ? left.mKey < right.mKey : left.mOrder < right.mOrder;
            });
            std::vector<Use> first;
            for (std::size_t i = 0; i < uses.size(); ++i)
            {
                if (i == 0 || uses[i].mKey != uses[i - 1].mKey)
                {
                    first.push_back(uses[i]);
                }
            }
            std::sort(first.begin(), first.end(),
                [](const Use& left, const Use& right) { return left.mOrder < right.mOrder; });
            data.mRegionLinesTotal = first.size();
            const std::size_t kept = std::min(first.size(), kMaxRegionLines);
            data.mRegionLines.reserve(kept);
            for (std::size_t i = 0; i < kept; ++i)
            {
                const auto low = static_cast<std::uint32_t>(first[i].mKey >> 32);
                const auto high = static_cast<std::uint32_t>(first[i].mKey & 0xFFFFFFFFu);
                const std::size_t triangle = static_cast<std::size_t>(first[i].mOrder / 3);
                const int region = triangle < mesh.mTriangleRegion.size() ? mesh.mTriangleRegion[triangle] : -1;
                OverlayLine line;
                line.mFrom = mesh.mPositions[low];
                line.mTo = mesh.mPositions[high];
                line.mColour = region >= 0 ? regionColour(static_cast<std::size_t>(region)) : kUnassignedColour;
                data.mRegionLines.push_back(line);
            }
        }

        /// The drawn mesh of a body, its regions, lines and weld points. `payload` is in `unitsPerMetre`; the package
        /// names its region map and weld groups.
        void buildDrawing(const Body::MergedBody& payload, double unitsPerMetre, const std::string& meshId,
            const Body::BodyPackage& package, BodyData& data)
        {
            DrawnMesh mesh;
            mesh.mMeshId = meshId;
            mesh.mPositions.reserve(payload.mPositions.size());
            for (const Body::Vec3& p : payload.mPositions)
            {
                mesh.mPositions.push_back(inMetres(p, unitsPerMetre));
            }
            mesh.mTriangles = payload.mTriangles;
            const std::size_t triangles = mesh.mTriangles.size() / 3;
            if (package.mRegionMap && package.mRegionMap->mMesh == meshId)
            {
                mesh.mTriangleRegion = assignRegions(*package.mRegionMap, triangles);
            }
            else
            {
                mesh.mTriangleRegion.assign(triangles, -1);
            }
            for (const int region : mesh.mTriangleRegion)
            {
                if (region >= 0 && static_cast<std::size_t>(region) < data.mRegions.size())
                {
                    ++data.mRegions[static_cast<std::size_t>(region)].mTriangles;
                }
            }
            if (package.mWeldGroups && package.mWeldGroups->mMesh == meshId)
            {
                mesh.mWeldGroups = package.mWeldGroups->mGroups;
            }
            data.mWeldGroups = mesh.mWeldGroups.size();
            for (const std::vector<std::uint32_t>& group : mesh.mWeldGroups)
            {
                data.mWeldMembers += group.size();
                if (data.mWeldPoints.size() < kMaxWeldPoints && !group.empty()
                    && group.front() < mesh.mPositions.size())
                {
                    data.mWeldPoints.push_back(mesh.mPositions[group.front()]);
                }
            }
            buildRegionLines(mesh, data);
            data.mMeshNote = "mesh '" + meshId + "': " + std::to_string(mesh.mPositions.size()) + " vertices, "
                + std::to_string(triangles) + " triangles, drawn in the bind pose (not skinned), "
                + std::to_string(data.mRegionLines.size()) + " of " + std::to_string(data.mRegionLinesTotal)
                + " edges drawn";
            data.mMesh = std::move(mesh);
        }

        void listRegions(const Body::BodyPackage& package, BodyData& data)
        {
            if (!package.mRegionMap)
            {
                return;
            }
            for (std::size_t r = 0; r < package.mRegionMap->mRegions.size(); ++r)
            {
                data.mRegions.push_back(RegionInfo{ package.mRegionMap->mRegions[r].mId, 0, regionColour(r) });
            }
        }

        // ---- what a pack is, from its index

        enum class PackKind
        {
            Unreadable, // the full reader says why
            Package,
            Cache,
            Neither,
        };

        PackKind peekPack(const std::filesystem::path& pack)
        {
            EdenArchive::Error error;
            const std::unique_ptr<EdenArchive::Archive> archive
                = EdenArchive::Archive::open(pack, EdenArchive::OpenOptions{}, error);
            if (archive == nullptr)
            {
                return PackKind::Unreadable;
            }
            bool package = false;
            bool cache = false;
            for (std::size_t i = 0; i < archive->entryCount(); ++i)
            {
                const EdenArchive::EntryInfo entry = archive->entry(i);
                package = package || (entry.mHasPath && entry.mPath == kBodyFileName);
                cache = cache || (entry.mHasPath && entry.mPath == kManifestFileName);
            }
            return package ? PackKind::Package : cache ? PackKind::Cache : PackKind::Neither;
        }

        // ---- the stage a report

        /// What a scan of the text outside strings finds before anything is parsed: the deepest nesting of arrays and
        /// objects, and the number of values (commas, arrays and objects), which bounds the memory nlohmann would use.
        struct JsonShape
        {
            std::size_t mDepth = 0;
            std::size_t mValues = 0;
        };

        /// Stops as soon as either count is past its limit.
        JsonShape scanJson(std::string_view text, std::size_t depthLimit, std::size_t valueLimit)
        {
            JsonShape shape;
            std::size_t depth = 0;
            bool inString = false;
            bool escaped = false;
            for (const char c : text)
            {
                if (inString)
                {
                    if (escaped)
                    {
                        escaped = false;
                    }
                    else if (c == '\\')
                    {
                        escaped = true;
                    }
                    else if (c == '"')
                    {
                        inString = false;
                    }
                    continue;
                }
                if (c == '"')
                {
                    inString = true;
                }
                else if (c == '[' || c == '{')
                {
                    shape.mDepth = std::max(shape.mDepth, ++depth);
                    ++shape.mValues;
                }
                else if ((c == ']' || c == '}') && depth > 0)
                {
                    --depth;
                }
                else if (c == ',')
                {
                    ++shape.mValues;
                }
                if (shape.mDepth > depthLimit || shape.mValues > valueLimit)
                {
                    return shape;
                }
            }
            return shape;
        }

        using Json = nlohmann::json;

        /// Reads typed fields of one object; the first problem is kept and names the field.
        class FieldReader
        {
        public:
            FieldReader(const Json& object, std::string where, std::string& error)
                : mObject(object)
                , mWhere(std::move(where))
                , mError(error)
            {
            }

            const Json* field(const char* name)
            {
                if (!mError.empty())
                {
                    return nullptr;
                }
                const auto found = mObject.find(name);
                if (found == mObject.end())
                {
                    fail(name, "is missing");
                    return nullptr;
                }
                return &*found;
            }

            std::string text(const char* name)
            {
                const Json* value = field(name);
                if (value == nullptr)
                {
                    return {};
                }
                if (!value->is_string())
                {
                    fail(name, "is not a string");
                    return {};
                }
                if (value->get_ref<const std::string&>().size() > kMaxAnalysisTextBytes)
                {
                    fail(name, "is longer than " + std::to_string(kMaxAnalysisTextBytes) + " bytes");
                    return {};
                }
                return value->get<std::string>();
            }

            bool flag(const char* name)
            {
                const Json* value = field(name);
                if (value == nullptr)
                {
                    return false;
                }
                if (!value->is_boolean())
                {
                    fail(name, "is not true or false");
                    return false;
                }
                return value->get<bool>();
            }

            std::uint64_t count(const char* name)
            {
                const Json* value = field(name);
                if (value == nullptr)
                {
                    return 0;
                }
                if (!value->is_number_unsigned() && !(value->is_number_integer() && value->get<std::int64_t>() >= 0))
                {
                    fail(name, "is not a count");
                    return 0;
                }
                return value->get<std::uint64_t>();
            }

            double number(const char* name)
            {
                const Json* value = field(name);
                if (value == nullptr)
                {
                    return 0;
                }
                if (!value->is_number())
                {
                    fail(name, "is not a number");
                    return 0;
                }
                const double result = value->get<double>();
                if (!std::isfinite(result))
                {
                    fail(name, "is not a finite number");
                    return 0;
                }
                return result;
            }

            const Json* object(const char* name)
            {
                const Json* value = field(name);
                if (value != nullptr && !value->is_object())
                {
                    fail(name, "is not an object");
                    return nullptr;
                }
                return value;
            }

            const Json* array(const char* name)
            {
                const Json* value = field(name);
                if (value != nullptr && !value->is_array())
                {
                    fail(name, "is not an array");
                    return nullptr;
                }
                return value;
            }

            void fail(const std::string& name, const std::string& problem)
            {
                if (mError.empty())
                {
                    mError = mWhere + "\"" + name + "\" " + problem;
                }
            }

            const std::string& where() const { return mWhere; }

        private:
            const Json& mObject;
            std::string mWhere;
            std::string& mError;
        };

        void readSeam(const Json& value, const std::string& where, AnalysisSeam& seam, std::string& error)
        {
            if (!value.is_object())
            {
                error = where + "is not an object";
                return;
            }
            FieldReader fields(value, where, error);
            seam.mLabel = fields.text("label");
            seam.mSlotA = fields.text("slotA");
            seam.mSlotB = fields.text("slotB");
            seam.mTouches = fields.flag("touches");
            if (const Json* distance = fields.object("distance"))
            {
                FieldReader inner(*distance, where + "\"distance\": ", error);
                seam.mPairs = inner.count("count");
                seam.mMinDistance = inner.number("min");
                seam.mMedianDistance = inner.number("median");
                seam.mMaxDistance = inner.number("max");
            }
            if (const Json* ladder = fields.array("withinLadder"))
            {
                if (ladder->size() != seam.mWithin.size())
                {
                    fields.fail("withinLadder", "does not hold six counts");
                }
                for (std::size_t i = 0; error.empty() && i < seam.mWithin.size(); ++i)
                {
                    const Json& entry = (*ladder)[i];
                    if (!entry.is_number_unsigned() && !(entry.is_number_integer() && entry.get<std::int64_t>() >= 0))
                    {
                        fields.fail("withinLadder", "holds something that is not a count");
                        break;
                    }
                    seam.mWithin[i] = entry.get<std::uint64_t>();
                }
            }
            seam.mOverlapPairs = fields.count("overlapPairs");
            seam.mGapPairs = fields.count("gapPairs");
            seam.mMeetingPairs = fields.count("meetingPairs");
            if (const Json* weights = fields.object("weightDisagreement"))
            {
                FieldReader inner(*weights, where + "\"weightDisagreement\": ", error);
                seam.mMaxWeightDisagreement = inner.number("max");
            }
        }

        void readUnit(const Json& value, const std::string& where, AnalysisUnit& unit, std::string& error)
        {
            FieldReader fields(value, where, error);
            unit.mSkeleton = fields.text("skeleton");
            unit.mBeast = fields.flag("beast");
            if (const Json* totals = fields.object("totals"))
            {
                FieldReader inner(*totals, where + "\"totals\": ", error);
                unit.mVertices = inner.count("vertices");
                unit.mTriangles = inner.count("triangles");
                unit.mBones = inner.count("bones");
                unit.mMaterials = inner.count("materials");
                unit.mFiles = inner.count("files");
                unit.mFileBytes = inner.count("fileBytes");
                unit.mSkinnedParts = inner.count("skinnedParts");
                unit.mRigidParts = inner.count("rigidParts");
            }
            if (const Json* seams = fields.array("seams"))
            {
                if (seams->size() > kMaxAnalysisSeams)
                {
                    fields.fail("seams", "holds more than " + std::to_string(kMaxAnalysisSeams) + " seams");
                    return;
                }
                for (std::size_t i = 0; error.empty() && i < seams->size(); ++i)
                {
                    AnalysisSeam seam;
                    readSeam((*seams)[i], where + "seam " + std::to_string(i) + ": ", seam, error);
                    unit.mSeams.push_back(std::move(seam));
                }
            }
        }

        // ---- the bodies of a view

        /// The validator's options for a package: its own files by safe path, and the summaries of its payloads.
        Body::Report validatePackage(const Body::BodyPackage& package, const Body::PackageFiles& files)
        {
            const PayloadMeshSource source(files);
            Body::ValidateOptions options;
            options.mMeshSource = &source;
            options.mReadFile = [&files](std::string_view path) { return textOf(files, path); };
            return Body::validateBodyPackage(package, options);
        }

        std::unique_ptr<BodyData> packageBody(const Body::BodyPackage& package, const Body::PackageFiles& files)
        {
            auto data = std::make_unique<BodyData>();
            data->mTitle = package.mIdentity.mId;
            data->mPackage = package;
            data->mReport = validatePackage(package, files);
            listRegions(package, *data);
            std::string why = "no LOD 0 skin mesh in the bodymesh format";
            for (const Body::MeshEntry& mesh : package.mMeshes)
            {
                if (mesh.mRole != Body::MeshRole::Skin || mesh.mLod != 0)
                {
                    continue;
                }
                if (mesh.mFormat != Body::MeshFormat::BodyMesh)
                {
                    why = "mesh '" + mesh.mId + "' is in the " + std::string(Body::toString(mesh.mFormat))
                        + " format: the panel draws only bodymesh payloads";
                    continue;
                }
                std::string error;
                const std::optional<Body::MergedBody> payload = readPayload(files, mesh.mPath, error);
                if (!payload)
                {
                    why = "mesh '" + mesh.mId + "': " + error;
                    continue;
                }
                buildDrawing(*payload, 1.0, mesh.mId, package, *data);
                break;
            }
            if (!data->mMesh)
            {
                data->mMeshNote = "nothing is drawn: " + why;
            }
            data->mAnalysisNote = "the stage a numbers (openmw --analyse-bodies) exist only for merged bodies";
            return data;
        }

        std::string titleOf(const Body::CacheEntry& entry)
        {
            return entry.mRace + " " + entry.mSex + " " + entry.mView;
        }

        std::unique_ptr<BodyData> mergedBody(const Body::CacheEntry& entry, const Body::PackageFiles& files,
            const Body::CacheManifest& manifest, const std::filesystem::path& cacheFolder)
        {
            auto data = std::make_unique<BodyData>();
            data->mTitle = titleOf(entry);
            std::string error;
            const std::optional<Body::MergedBody> payload = readPayload(files, entry.mName, error);
            if (!payload)
            {
                data->mPayloadError = error;
                data->mMeshNote = "nothing is drawn: " + error;
                data->mAnalysisNote = "no stage a numbers: the payload does not read";
                return data;
            }
            const Body::BodyPackage package = mergedEntryPackage(entry, *payload, manifest);
            data->mReport = Body::validateBodyPackage(package);
            listRegions(package, *data);
            buildDrawing(*payload, kGameUnitsPerMetre, std::string(kMergedMeshId), package, *data);
            data->mPackage = package;

            const std::filesystem::path reportPath = analysisReportFor(cacheFolder);
            std::error_code code;
            const std::uintmax_t size = std::filesystem::file_size(reportPath, code);
            if (code)
            {
                data->mAnalysisNote = "no stage a report at '" + utf8(reportPath.lexically_normal())
                    + "' (written by openmw --analyse-bodies)";
                return data;
            }
            if (size > kMaxAnalysisBytes)
            {
                data->mAnalysisNote = "the stage a report '" + utf8(reportPath.lexically_normal()) + "' is over "
                    + std::to_string(kMaxAnalysisBytes >> 20) + " MiB: not read";
                return data;
            }
            std::ifstream stream(reportPath, std::ios::binary);
            std::string text(static_cast<std::size_t>(size), '\0');
            stream.read(text.data(), static_cast<std::streamsize>(text.size()));
            if (!stream || stream.gcount() != static_cast<std::streamsize>(text.size()))
            {
                data->mAnalysisNote = "the stage a report '" + utf8(reportPath.lexically_normal()) + "' cannot be read";
                return data;
            }
            AnalysisResult result = readAnalysisUnit(text, entry.mRace, entry.mSex == "female");
            if (result.mUnit)
            {
                data->mAnalysis = std::move(result.mUnit);
                data->mAnalysisNote = "from '" + utf8(reportPath.lexically_normal()) + "'";
            }
            else
            {
                data->mAnalysisNote = "'" + utf8(reportPath.lexically_normal()) + "': " + result.mError;
            }
            return data;
        }
    }

    struct BodyViewAccess
    {
        static OpenResult fromPackageFiles(
            SourceKind kind, const std::filesystem::path& path, Body::PackageFiles files, const std::string& bodyName)
        {
            const std::optional<std::string> text = textOf(files, kBodyFileName);
            if (!text)
            {
                return { nullptr, about(path, "it is not a body package: it holds no body.json") };
            }
            const Body::BodyLoadResult loaded = Body::readBodyPackage(*text, bodyName);
            if (!loaded.mValue)
            {
                return { nullptr, about(path, loaded.mError.toString()) };
            }
            auto view = std::make_unique<BodyView>();
            view->mKind = kind;
            view->mPath = path;
            view->mFiles = std::move(files);
            view->mBodies.resize(1);
            view->mBodies[0] = packageBody(*loaded.mValue, view->mFiles);
            // A package's one body is worked out now; its files (textures too, in later packages) are not needed
            // again (fresh review of stage f, finding 1).
            view->mFiles.mFiles.clear();
            return { std::move(view), {} };
        }

        static OpenResult fromCacheFiles(
            const std::filesystem::path& path, const std::filesystem::path& cacheFolder, Body::PackageFiles files)
        {
            const std::optional<std::string> text = textOf(files, kManifestFileName);
            if (!text)
            {
                return { nullptr, about(path, "it is not a merged body cache: it holds no manifest.txt") };
            }
            std::string error;
            std::optional<Body::CacheManifest> manifest = Body::readManifest(*text, error);
            if (!manifest)
            {
                return { nullptr, about(path, "the merged body cache's manifest.txt does not read: " + error) };
            }
            if (manifest->mEntries.empty())
            {
                return { nullptr,
                    about(path,
                        "the merged body cache holds no body (" + std::to_string(manifest->mFailures.size())
                            + " failed in the bake)") };
            }
            auto view = std::make_unique<BodyView>();
            view->mKind = SourceKind::MergedCache;
            view->mPath = path;
            view->mCacheFolder = cacheFolder;
            view->mFiles = std::move(files);
            view->mManifest = std::move(*manifest);
            view->mBodies.resize(view->mManifest.mEntries.size());
            return { std::move(view), {} };
        }

        static const BodyData& body(const BodyView& view, std::size_t index)
        {
            if (index >= view.mBodies.size())
            {
                index = view.mSelected;
            }
            std::unique_ptr<BodyData>& slot = view.mBodies[index];
            if (!slot)
            {
                // Called from Studio's callbacks (the panel, the summary, the gizmos): nothing may leave them, and a
                // failure here (memory for a huge payload) is this body's message, not the module's end (fresh review
                // of stage f, finding 3).
                const Body::CacheEntry& entry = view.mManifest.mEntries[index];
                try
                {
                    slot = mergedBody(entry, view.mFiles, view.mManifest, view.mCacheFolder);
                }
                catch (const std::exception& e)
                {
                    slot = std::make_unique<BodyData>();
                    slot->mTitle = titleOf(entry);
                    slot->mPayloadError = std::string("the body could not be worked out: ") + e.what();
                    slot->mMeshNote = "nothing is drawn: " + slot->mPayloadError;
                    slot->mAnalysisNote = "no stage a numbers: " + slot->mPayloadError;
                }
            }
            return *slot;
        }
    };

    Colour regionColour(std::size_t regionIndex)
    {
        return kPalette[regionIndex % kRegionPaletteSize];
    }

    AnalysisResult readAnalysisUnit(std::string_view text, std::string_view race, bool female)
    {
        try
        {
            if (text.size() > kMaxAnalysisBytes)
            {
                return { std::nullopt,
                    "the stage a report is over " + std::to_string(kMaxAnalysisBytes >> 20) + " MiB" };
            }
            const JsonShape shape = scanJson(text, kMaxAnalysisNesting, kMaxAnalysisValues);
            if (shape.mDepth > kMaxAnalysisNesting)
            {
                return { std::nullopt,
                    "the stage a report nests deeper than " + std::to_string(kMaxAnalysisNesting) + " levels" };
            }
            if (shape.mValues > kMaxAnalysisValues)
            {
                return { std::nullopt,
                    "the stage a report holds more than " + std::to_string(kMaxAnalysisValues) + " values" };
            }
            const Json document = Json::parse(text.begin(), text.end(), nullptr, false);
            if (document.is_discarded())
            {
                return { std::nullopt, "the stage a report is not JSON" };
            }
            if (!document.is_object())
            {
                return { std::nullopt, "the stage a report is not a JSON object" };
            }
            std::string error;
            FieldReader top(document, "", error);
            const std::string format = top.text("format");
            if (error.empty() && format != "openeden.bodyanalysis")
            {
                top.fail("format", "is '" + format.substr(0, 64) + "', not 'openeden.bodyanalysis'");
            }
            const std::uint64_t version = error.empty() ? top.count("version") : 0;
            if (error.empty() && version != 1)
            {
                top.fail("version", "is " + std::to_string(version) + ", this reader knows version 1");
            }
            const Json* units = top.array("units");
            if (error.empty() && units->size() > kMaxAnalysisUnits)
            {
                top.fail("units", "holds more than " + std::to_string(kMaxAnalysisUnits) + " units");
            }
            if (!error.empty())
            {
                return { std::nullopt, error };
            }
            for (std::size_t i = 0; i < units->size(); ++i)
            {
                const Json& value = (*units)[i];
                const std::string where = "unit " + std::to_string(i) + ": ";
                if (!value.is_object())
                {
                    return { std::nullopt, where + "is not an object" };
                }
                FieldReader fields(value, where, error);
                AnalysisUnit unit;
                unit.mRace = fields.text("race");
                unit.mFemale = fields.flag("female");
                if (!error.empty())
                {
                    return { std::nullopt, error };
                }
                if (!equalsIgnoringAsciiCase(unit.mRace, race) || unit.mFemale != female)
                {
                    continue;
                }
                readUnit(value, where, unit, error);
                if (!error.empty())
                {
                    return { std::nullopt, error };
                }
                return { std::move(unit), {} };
            }
            return { std::nullopt,
                "the stage a report has no unit for " + std::string(race) + (female ? " female" : " male") };
        }
        catch (const std::exception& e)
        {
            return { std::nullopt, std::string("the stage a report cannot be read: ") + e.what() };
        }
    }

    std::filesystem::path analysisReportFor(const std::filesystem::path& cacheFolder)
    {
        return cacheFolder / ".." / ".." / "analysis" / "bodies.json";
    }

    PayloadMeshSource::PayloadMeshSource(const Body::PackageFiles& files)
        : mFiles(files)
    {
    }

    std::optional<Body::MeshSummary> PayloadMeshSource::summarize(std::string_view path, std::string_view format) const
    {
        if (format != Body::toString(Body::MeshFormat::BodyMesh))
        {
            return std::nullopt;
        }
        std::string error;
        const std::optional<Body::MergedBody> payload = readPayload(mFiles, path, error);
        if (!payload)
        {
            return std::nullopt;
        }
        return summarizeMergedBody(*payload, 1.0);
    }

    Body::MeshSummary summarizeMergedBody(const Body::MergedBody& body, double unitsPerMetre)
    {
        Body::MeshSummary summary;
        summary.mVertexCount = static_cast<std::uint32_t>(body.mPositions.size());
        summary.mTriangleCount = static_cast<std::uint32_t>(body.mTriangles.size() / 3);
        for (std::size_t v = 0; v < body.mPositions.size(); ++v)
        {
            const std::array<double, 3> p = inMetres(body.mPositions[v], unitsPerMetre);
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                summary.mBoundsMin[axis] = v == 0 ? p[axis] : std::min(summary.mBoundsMin[axis], p[axis]);
                summary.mBoundsMax[axis] = v == 0 ? p[axis] : std::max(summary.mBoundsMax[axis], p[axis]);
            }
        }
        summary.mBoneNames = body.mBones;
        summary.mInfluenceOffsets.reserve(body.mPositions.size() + 1);
        summary.mInfluenceOffsets.push_back(0);
        for (std::size_t v = 0; v < body.mPositions.size(); ++v)
        {
            for (std::size_t slot = 0; slot < Body::maxMergedInfluences; ++slot)
            {
                const float weight = v < body.mBoneWeights.size() ? body.mBoneWeights[v][slot] : 0.f;
                if (weight > 0.f)
                {
                    summary.mInfluences.push_back(
                        Body::MeshSummary::Influence{ body.mBoneIndices[v][slot], static_cast<double>(weight) });
                }
            }
            summary.mInfluenceOffsets.push_back(static_cast<std::uint32_t>(summary.mInfluences.size()));
        }
        summary.mTriangleIndices = body.mTriangles;
        summary.mUvSetCount = body.mUvs.empty() ? 0u : 1u;
        return summary;
    }

    Body::BodyPackage mergedEntryPackage(
        const Body::CacheEntry& entry, const Body::MergedBody& payload, const Body::CacheManifest& manifest)
    {
        Body::BodyPackage package;
        package.mIdentity.mId = "merged-body";
        package.mIdentity.mName = titleOf(entry);
        package.mIdentity.mVersion = "0.0.0";
        package.mIdentity.mAuthor = "OpenEden merged body bake (openmw --bake-bodies)";
        package.mIdentity.mLicense = "derived from the user's own game files: not for distribution";

        Body::Target target;
        target.mRace = entry.mRace;
        target.mSex = entry.mSex;
        package.mTargets.push_back(target);

        // No binding: the cache records no skeleton profile; the engine binds the body to its race's skeleton by bone
        // name. The validator says so (BODY-BIND-001, BODY-BIND-014).

        Body::MeshEntry mesh;
        mesh.mId = std::string(kMergedMeshId);
        mesh.mPath = entry.mName;
        mesh.mFormat = Body::MeshFormat::BodyMesh;
        mesh.mLod = 0;
        mesh.mRole = Body::MeshRole::Skin;
        mesh.mSummary = summarizeMergedBody(payload, kGameUnitsPerMetre);
        package.mMeshes.push_back(std::move(mesh));

        const std::string parameters = "layout " + std::to_string(manifest.mLayoutVersion);
        const auto provenance = [&](const char* part) {
            return Body::ProvenanceRecord{ part, entry.mKey, std::string(kMergeToolId),
                std::to_string(manifest.mMergerVersion), parameters };
        };

        Body::RegionMap regions;
        regions.mOrigin = Body::Origin::Derived;
        regions.mMesh = std::string(kMergedMeshId);
        for (const Body::RegionRange& range : payload.mRegions)
        {
            regions.mRegions.push_back(Body::Region{ "slot-" + std::to_string(range.mSlot),
                { Body::TriangleRange{ range.mFirstTriangle, range.mFirstTriangle + range.mTriangleCount } } });
        }
        package.mRegionMap = std::move(regions);
        package.mProvenance.push_back(provenance("regionMap"));

        if (!payload.mWeldGroups.empty())
        {
            Body::WeldGroups welds;
            welds.mOrigin = Body::Origin::Derived;
            welds.mMesh = std::string(kMergedMeshId);
            welds.mGroups = payload.mWeldGroups;
            double spread = 0;
            for (const std::vector<std::uint32_t>& group : payload.mWeldGroups)
            {
                if (group.empty() || group.front() >= payload.mPositions.size())
                {
                    continue;
                }
                const std::array<double, 3> first = inMetres(payload.mPositions[group.front()], kGameUnitsPerMetre);
                for (const std::uint32_t member : group)
                {
                    if (member >= payload.mPositions.size())
                    {
                        continue;
                    }
                    const std::array<double, 3> p = inMetres(payload.mPositions[member], kGameUnitsPerMetre);
                    spread = std::max(spread,
                        std::sqrt((p[0] - first[0]) * (p[0] - first[0]) + (p[1] - first[1]) * (p[1] - first[1])
                            + (p[2] - first[2]) * (p[2] - first[2])));
                }
            }
            welds.mToleranceM = spread;
            package.mWeldGroups = std::move(welds);
            package.mProvenance.push_back(provenance("weldGroups"));
        }
        return package;
    }

    std::size_t BodyView::bodyCount() const
    {
        return mBodies.size();
    }

    const BodyData& BodyView::body(std::size_t index) const
    {
        return BodyViewAccess::body(*this, index);
    }

    bool BodyView::select(std::size_t index)
    {
        if (index >= mBodies.size())
        {
            return false;
        }
        mSelected = index;
        return true;
    }

    OpenResult openBodySource(const std::filesystem::path& path)
    {
        const std::string name = utf8(path.filename());
        if (name == kPointerFileName)
        {
            return openMergedCache(path.parent_path());
        }
        if (endsWithIgnoringAsciiCase(name, kPackEnding))
        {
            return openPackageFile(path);
        }
        // Exactly body.json (somebody.json is not a package's): the folder that holds it is the package.
        if (equalsIgnoringAsciiCase(name, kBodyFileName))
        {
            std::error_code code;
            if (!std::filesystem::is_regular_file(path, code))
            {
                return { nullptr, about(path, "there is no such body.json") };
            }
            return openPackageFolder(path.parent_path());
        }
        return { nullptr,
            about(
                path, "it is not a body source: open a package's body.json, a .edenpak or a merged cache's current") };
    }

    OpenResult openPackageFolder(const std::filesystem::path& folder)
    {
        try
        {
            std::error_code code;
            if (!std::filesystem::is_regular_file(folder / std::string(kBodyFileName), code))
            {
                return { nullptr, about(folder, "it is not a body package folder: it has no body.json") };
            }
            std::string error;
            std::optional<Body::PackageFiles> files = Body::readPackageFolder(folder, error);
            if (!files)
            {
                return { nullptr, about(folder, error) };
            }
            return BodyViewAccess::fromPackageFiles(
                SourceKind::PackageFolder, folder, std::move(*files), utf8(folder / std::string(kBodyFileName)));
        }
        catch (const std::exception& e)
        {
            return { nullptr, about(folder, std::string("cannot be opened: ") + e.what()) };
        }
    }

    OpenResult openPackageFile(const std::filesystem::path& pack)
    {
        try
        {
            // From the index alone, before anything is decoded: a pack that is no body source (an LOD, texture or
            // data pack in an asset root) is refused without reading it (fresh review of stage f, finding 1).
            if (peekPack(pack) == PackKind::Neither)
            {
                return { nullptr,
                    about(pack,
                        "the pack holds neither body.json (a body package) nor manifest.txt (a merged body cache)") };
            }
            std::string error;
            std::optional<Body::PackageFiles> files = Body::readPackageFile(pack, error);
            if (!files)
            {
                return { nullptr, about(pack, error) };
            }
            if (files->mFiles.count(std::string(kBodyFileName)) == 1)
            {
                return BodyViewAccess::fromPackageFiles(
                    SourceKind::PackageFile, pack, std::move(*files), utf8(pack) + ":" + std::string(kBodyFileName));
            }
            if (files->mFiles.count(std::string(kManifestFileName)) == 1)
            {
                return BodyViewAccess::fromCacheFiles(pack, pack.parent_path(), std::move(*files));
            }
            return { nullptr,
                about(
                    pack, "the pack holds neither body.json (a body package) nor manifest.txt (a merged body cache)") };
        }
        catch (const std::exception& e)
        {
            return { nullptr, about(pack, std::string("cannot be opened: ") + e.what()) };
        }
    }

    OpenResult openMergedCache(const std::filesystem::path& cacheFolder)
    {
        try
        {
            std::string error;
            std::optional<Body::PackageFiles> files = Body::loadCurrentCache(cacheFolder, error);
            if (!files)
            {
                return { nullptr, "merged body cache '" + utf8(cacheFolder) + "': " + error };
            }
            return BodyViewAccess::fromCacheFiles(cacheFolder, cacheFolder, std::move(*files));
        }
        catch (const std::exception& e)
        {
            return { nullptr, "merged body cache '" + utf8(cacheFolder) + "' cannot be opened: " + e.what() };
        }
    }
}
