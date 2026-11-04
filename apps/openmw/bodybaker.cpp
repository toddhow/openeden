#include "bodybaker.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <components/body/bodycache.hpp>
#include <components/body/bodymerge.hpp>
#include <components/body/bodymesh.hpp>
#include <components/bodynif/skinnedshape.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm3/loadbody.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/files/conversion.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/mwanimation/partbones.hpp>
#include <components/nif/niffile.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/settings/settings.hpp>
#include <components/vfs/manager.hpp>

#include <edenarchive/hash.hpp>

#include "bodycommon.hpp"
#include "mwbase/environment.hpp"
#include "mwbase/world.hpp"
#include "mwrender/bodyparts.hpp"
#include "mwworld/esmstore.hpp"

namespace OMW
{
    using namespace BodyCommon;

    namespace
    {
        // Stage a measured these two seams as exact (all 40 rings within 0.0011 units, the next nearest pair anywhere
        // 0.054 away): a tolerance of 0.01 welds them and nothing else. The other joints are overlapping closed shells
        // and stay as they are (toddhow, 2026-11-03).
        constexpr double weldTolerance = 0.01;

        constexpr const char* viewName = "third";

        std::vector<Body::SeamDeclaration> exactSeams()
        {
            return {
                { ESM::PRT_RForearm, ESM::PRT_RWrist, weldTolerance },
                { ESM::PRT_LForearm, ESM::PRT_LWrist, weldTolerance },
                { ESM::PRT_RKnee, ESM::PRT_RAnkle, weldTolerance },
                { ESM::PRT_LKnee, ESM::PRT_LAnkle, weldTolerance },
            };
        }

        std::string sexName(bool female)
        {
            return female ? "female" : "male";
        }

        // The files of one run: parsed once each (a stock hand file serves both sides) with their digests.
        class FileCache
        {
        public:
            explicit FileCache(const VFS::Manager& vfs)
                : mVfs(vfs)
            {
            }

            const ParsedFile& get(const std::string& path)
            {
                auto it = mFiles.find(path);
                if (it == mFiles.end())
                    it = mFiles.emplace(path, parseFile(mVfs, path)).first;
                return it->second;
            }

            // Bounded: a unit needs a few dozen files and the next unit mostly the same ones.
            void trim()
            {
                if (mFiles.size() > 128)
                    mFiles.clear();
            }

        private:
            const VFS::Manager& mVfs;
            std::map<std::string, ParsedFile> mFiles;
        };

        // Everything one body needs, read from the game data: the merger's input, its options and the key's input.
        struct StockBody
        {
            Body::BodyKeyInput mKey;
            std::vector<Body::PartMesh> mParts;
            std::vector<Body::SeamDeclaration> mSeams;
            Body::MergeOptions mOptions;
            Body::MergeLimits mLimits;
            std::string mError; // non-empty: the body cannot be merged, one line
        };

        std::string materialKey(const BodyNif::Shape& shape)
        {
            return Misc::StringUtils::lowerCase(shape.mTexture)
                + (shape.mAlphaTest         ? "|a"
                        : shape.mAlphaBlend ? "|b"
                                            : "|-");
        }

        Body::PartMesh toPartMesh(const BodyNif::Shape& shape, const SlotDef& slot, const std::string& rigidBone)
        {
            Body::PartMesh part;
            part.mName = std::string(slot.mName) + "/" + shape.mName;
            part.mSlot = static_cast<int>(slot.mType);
            part.mMaterial = materialKey(shape);
            part.mPositions = shape.mPositions;
            part.mNormals = shape.mNormals;
            part.mUvs = shape.mUvs;
            part.mTriangles = shape.mTriangles;
            // The reader placed the vertices in the skeleton's space, mirroring a part on a "Left" bone as the engine
            // does; a mirror reverses the winding, which the reader does not do.
            if (shape.mMirrored)
            {
                for (std::size_t i = 0; i + 2 < part.mTriangles.size(); i += 3)
                    std::swap(part.mTriangles[i + 1], part.mTriangles[i + 2]);
            }
            if (shape.mSkinned)
            {
                for (const std::string& bone : shape.mBones)
                    part.mBones.push_back(Misc::StringUtils::lowerCase(bone));
                part.mSkin.resize(shape.mSkin.size());
                for (std::size_t v = 0; v < shape.mSkin.size(); ++v)
                {
                    const BodyNif::VertexSkin& in = shape.mSkin[v];
                    part.mSkin[v].mCount = in.mCount;
                    for (std::size_t k = 0; k < in.mCount && k < part.mSkin[v].mInfluences.size(); ++k)
                        part.mSkin[v].mInfluences[k] = { in.mInfluences[k].mBone, in.mInfluences[k].mWeight };
                }
            }
            else
            {
                part.mRigidBone = rigidBone;
            }
            return part;
        }

        StockBody buildStockBody(
            const Unit& unit, const MWWorld::ESMStore& store, const VFS::Manager& vfs, FileCache& files)
        {
            StockBody body;
            body.mKey.mRace = unit.mRace->mId.getRefIdString();
            body.mKey.mSex = sexName(unit.mFemale);
            body.mKey.mView = viewName;
            body.mKey.mLayoutVersion = Body::bodyMeshLayoutVersion;
            body.mSeams = exactSeams();
            for (const SlotDef& def : slotDefs)
                body.mOptions.mBoneOrder.push_back(Misc::StringUtils::lowerCase(MWAnim::sPartBones.at(def.mType)));
            // The key carries exactly what the merger is given: the same objects, not copies of their values.
            body.mKey.mSeams = body.mSeams;
            body.mKey.mMerge = body.mOptions;
            body.mKey.mLimits = body.mLimits;

            const bool beast = (unit.mRace->mData.mFlags & ESM::Race::Beast) != 0;
            const auto& bodyParts = store.get<ESM::BodyPart>();
            const std::vector<const ESM::BodyPart*>& chosen
                = MWRender::getBodyParts(unit.mRace->mId, unit.mFemale, false, false, bodyParts);

            // The skeleton whose bones the rigid parts hang on (Context::pickSkeleton: beast, female, else the base
            // one).
            const char* skeletonSetting = beast ? "baseanimkna" : unit.mFemale ? "baseanimfemale" : "baseanim";
            body.mKey.mSkeletonPath
                = VFS::Path::Normalized(Settings::Manager::getString(skeletonSetting, "Models")).value();
            const ParsedFile& skeleton = files.get(body.mKey.mSkeletonPath);
            if (!skeleton.mError.empty() || skeleton.mFile == nullptr)
            {
                body.mError = "the skeleton '" + body.mKey.mSkeletonPath + "' cannot be read: " + skeleton.mError;
                return body;
            }
            body.mKey.mSkeletonDigest = skeleton.mDigest;
            BodyNif::RestPose rest;
            for (const Nif::Record* root : skeleton.mFile->mRoots)
            {
                const auto* node = dynamic_cast<const Nif::NiAVObject*>(root);
                if (node != nullptr)
                    rest.merge(BodyNif::readRestPose(*node));
            }

            std::vector<std::string> errors;
            for (const SlotDef& def : slotDefs)
            {
                const ESM::BodyPart* record = def.mType < chosen.size() ? chosen[def.mType] : nullptr;
                if (record == nullptr)
                    continue;
                const std::string model
                    = Misc::ResourceHelpers::correctMeshPath(VFS::Path::Normalized(record->mModel)).value();
                const ParsedFile& parsed = files.get(model);
                if (!parsed.mError.empty() || parsed.mFile == nullptr)
                {
                    errors.push_back(std::string(def.mName) + ": " + parsed.mError);
                    continue;
                }
                const std::string& attachBone = MWAnim::sPartBones.at(def.mType);
                const BodyNif::Placement placement{ &rest, attachBone };
                const std::vector<BodyNif::Shape> shapes = shapesOf(parsed, attachBone, placement, errors);
                if (shapes.empty())
                {
                    errors.push_back(
                        std::string(def.mName) + ": no shape is selected by the skin filter '" + attachBone + "'");
                    continue;
                }
                for (const BodyNif::Shape& shape : shapes)
                {
                    if (!shape.mSkinned && !shape.mPlaced)
                    {
                        errors.push_back(
                            std::string(def.mName) + ": the attach bone '" + attachBone + "' is not in the skeleton");
                        continue;
                    }
                    body.mParts.push_back(toPartMesh(shape, def, Misc::StringUtils::lowerCase(attachBone)));
                }
                Body::BodyPartKey key;
                key.mSlot = static_cast<int>(def.mType);
                key.mRecordId = record->mId.getRefIdString();
                key.mPartType = static_cast<int>(record->mData.mPart);
                key.mFlags = static_cast<int>(record->mData.mFlags);
                key.mRace = record->mRace.getRefIdString();
                key.mModelPath = model;
                key.mModelDigest = parsed.mDigest;
                body.mKey.mParts.push_back(std::move(key));
            }
            if (body.mParts.empty())
                body.mError = errors.empty() ? std::string("the race has no body parts") : errors.front();
            else if (!errors.empty())
                body.mError = errors.front();
            return body;
        }

        std::string packEntryNameOf(const StockBody& body)
        {
            return Body::bodyEntryName(body.mKey.mRace, body.mKey.mSex, body.mKey.mView);
        }

        std::vector<std::uint8_t> bytesOf(const std::string& text)
        {
            return std::vector<std::uint8_t>(text.begin(), text.end());
        }

        std::string textOf(const std::vector<std::uint8_t>& bytes)
        {
            return std::string(bytes.begin(), bytes.end());
        }

        // ---- poses for the check: each bone moved rigidly by a seeded rotation and translation

        struct BoneMove
        {
            std::array<double, 9> mR{};
            std::array<double, 3> mT{};
        };

        std::uint64_t fnv1a(const std::string& text, std::uint64_t seed)
        {
            std::uint64_t h = 1469598103934665603ull ^ (seed * 0x9E3779B97F4A7C15ull);
            for (const char c : text)
                h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ull;
            return h;
        }

        double unitFrom(std::uint64_t& state)
        {
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            return static_cast<double>(state >> 11) / static_cast<double>(1ull << 53); // [0, 1)
        }

        BoneMove boneMove(const std::string& bone, int pose)
        {
            BoneMove move;
            if (pose == 0)
            {
                move.mR = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
                return move;
            }
            std::uint64_t state = fnv1a(bone, static_cast<std::uint64_t>(pose));
            double axis[3] = { unitFrom(state) - 0.5, unitFrom(state) - 0.5, unitFrom(state) - 0.5 };
            const double length = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
            for (double& a : axis)
                a = length > 1e-9 ? a / length : (&a == &axis[0] ? 1.0 : 0.0);
            const double angle = (unitFrom(state) - 0.5) * 1.2; // up to 0.6 rad
            const double c = std::cos(angle), s = std::sin(angle), t = 1 - c;
            move.mR = { t * axis[0] * axis[0] + c, t * axis[0] * axis[1] - s * axis[2],
                t * axis[0] * axis[2] + s * axis[1], t * axis[0] * axis[1] + s * axis[2], t * axis[1] * axis[1] + c,
                t * axis[1] * axis[2] - s * axis[0], t * axis[0] * axis[2] - s * axis[1],
                t * axis[1] * axis[2] + s * axis[0], t * axis[2] * axis[2] + c };
            for (double& value : move.mT)
                value = (unitFrom(state) - 0.5) * 2.0; // up to 1 unit
            return move;
        }

        std::array<double, 3> moveVertex(const BoneMove& m, const Body::Vec3& v)
        {
            return { m.mR[0] * v[0] + m.mR[1] * v[1] + m.mR[2] * v[2] + m.mT[0],
                m.mR[3] * v[0] + m.mR[4] * v[1] + m.mR[5] * v[2] + m.mT[1],
                m.mR[6] * v[0] + m.mR[7] * v[1] + m.mR[8] * v[2] + m.mT[2] };
        }

        // The skinned position of a stock vertex: the weighted sum of the vertex moved by each bone that holds it.
        std::array<double, 3> skinStock(const Body::PartMesh& part, std::size_t vertex, int pose)
        {
            std::array<double, 3> sum{};
            const auto add = [&](const std::string& bone, double weight) {
                const std::array<double, 3> moved = moveVertex(boneMove(bone, pose), part.mPositions[vertex]);
                for (int i = 0; i < 3; ++i)
                    sum[i] += weight * moved[i];
            };
            if (part.mSkin.empty())
            {
                add(part.mRigidBone, 1.0);
            }
            else
            {
                const Body::PartVertexSkin& skin = part.mSkin[vertex];
                for (std::size_t k = 0; k < skin.mCount; ++k)
                    add(part.mBones[skin.mInfluences[k].mBone], skin.mInfluences[k].mWeight);
            }
            return sum;
        }

        std::array<double, 3> skinMerged(const Body::MergedBody& body, std::size_t vertex, int pose)
        {
            std::array<double, 3> sum{};
            for (std::size_t k = 0; k < Body::maxMergedInfluences; ++k)
            {
                const float weight = body.mBoneWeights[vertex][k];
                if (weight == 0.f)
                    continue;
                const std::array<double, 3> moved
                    = moveVertex(boneMove(body.mBones[body.mBoneIndices[vertex][k]], pose), body.mPositions[vertex]);
                for (int i = 0; i < 3; ++i)
                    sum[i] += static_cast<double>(weight) * moved[i];
            }
            return sum;
        }

        double distance(const std::array<double, 3>& a, const std::array<double, 3>& b)
        {
            return std::sqrt(
                (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
        }

        // The status, check and pointer files are written whole under a temporary name and renamed: a reader sees the
        // old file or the new one. A failure is logged and returned (never silently "complete").
        bool writeAtomic(const std::filesystem::path& path, const std::string& text)
        {
            std::string error;
            if (Body::writeFileAtomic(path, text, error))
                return true;
            Log(Debug::Error) << "Body tools: " << error;
            return false;
        }

        std::string readFile(const std::filesystem::path& path)
        {
            std::ifstream in(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
    }

    struct BodyBaker::State
    {
        State(Mode mode, std::filesystem::path outFolder, Resource::ResourceSystem& resources)
            : mMode(mode)
            , mFolder(std::move(outFolder) / "openeden" / "bodies")
            , mResources(resources)
        {
        }

        Mode mMode;
        std::filesystem::path mFolder;
        Resource::ResourceSystem& mResources;
        std::optional<FileCache> mFiles;
        bool mStarted = false;
        bool mDone = false;
        std::vector<Unit> mUnits;
        std::size_t mNext = 0;

        // Bake
        std::optional<Body::PackageFiles> mOld; // the published cache, if any
        std::optional<Body::CacheManifest> mOldManifest;
        Body::PackageFiles mNewFiles;
        Body::CacheManifest mNewManifest;
        std::size_t mMerged = 0;
        std::size_t mReused = 0;
        std::uint64_t mPayloadBytes = 0;

        // Check
        Body::CacheManifest mCheckManifest;
        Body::PackageFiles mCheckFiles;
        bool mCheckLoaded = false;
        std::size_t mChecked = 0;
        std::size_t mProblems = 0;
        std::string mReport;
    };

    BodyBaker::BodyBaker(Mode mode, std::filesystem::path outFolder, Resource::ResourceSystem& resources)
        : mState(std::make_unique<State>(mode, std::move(outFolder), resources))
    {
    }

    BodyBaker::~BodyBaker() = default;

    namespace
    {
        void collectUnits(const MWWorld::ESMStore& store, std::vector<Unit>& units)
        {
            for (const ESM::Race& race : store.get<ESM::Race>())
            {
                units.push_back({ &race, false });
                units.push_back({ &race, true });
            }
            std::stable_sort(units.begin(), units.end(), [](const Unit& left, const Unit& right) {
                const std::string a = left.mRace->mId.getRefIdString();
                const std::string b = right.mRace->mId.getRefIdString();
                return a != b ? a < b : left.mFemale < right.mFemale;
            });
        }

        void failUnit(Body::CacheManifest& manifest, const StockBody& stock, const std::string& reason)
        {
            manifest.mFailures.push_back({ stock.mKey.mRace, stock.mKey.mSex, stock.mKey.mView, reason });
            Log(Debug::Warning) << "Body bake: " << stock.mKey.mRace << " " << stock.mKey.mSex << ": " << reason;
        }

        // Bake one unit. Returns true when the unit produced an entry.
        void bakeUnit(BodyBaker::Mode, const Unit& unit, const MWWorld::ESMStore& store, const VFS::Manager& vfs,
            FileCache& files, std::optional<Body::PackageFiles>& old, std::optional<Body::CacheManifest>& oldManifest,
            Body::PackageFiles& newFiles, Body::CacheManifest& manifest, std::size_t& merged, std::size_t& reused,
            std::uint64_t& payloadBytes)
        {
            StockBody stock = buildStockBody(unit, store, vfs, files);
            files.trim();
            if (!stock.mError.empty())
            {
                failUnit(manifest, stock, stock.mError);
                return;
            }
            const std::string key = Body::computeBodyKey(stock.mKey);
            const std::string name = packEntryNameOf(stock);
            if (newFiles.mFiles.find(name) != newFiles.mFiles.end())
            {
                // Two bodies whose (race, sex, view) give one entry name: refuse loudly, never overwrite.
                failUnit(manifest, stock, "its pack entry name '" + name + "' is already taken by another body");
                return;
            }
            std::vector<std::uint8_t> payload;
            if (old && oldManifest && Body::isEntryCurrent(*oldManifest, name, key))
            {
                const auto found = old->mFiles.find(name);
                if (found != old->mFiles.end() && Body::readBodyMesh(found->second).mBody.has_value())
                {
                    payload = found->second;
                    ++reused;
                }
            }
            if (payload.empty())
            {
                const Body::MergeResult result
                    = Body::mergeParts(stock.mParts, stock.mSeams, stock.mOptions, stock.mLimits);
                if (!result.ok())
                {
                    failUnit(manifest, stock, result.mErrors.front());
                    return;
                }
                std::string error;
                if (!Body::writeBodyMesh(result.mBody, payload, error))
                {
                    failUnit(manifest, stock, error);
                    return;
                }
                ++merged;
                Log(Debug::Info) << "Body bake: " << stock.mKey.mRace << " " << stock.mKey.mSex << ": "
                                 << result.mBody.mReport.mInputVertices << " vertices into "
                                 << result.mBody.mReport.mOutputVertices << ", " << result.mBody.mReport.mWeldSets
                                 << " weld sets, " << result.mBody.mReport.mWeldGroups << " weld groups, "
                                 << payload.size() << " bytes.";
            }
            payloadBytes += payload.size();
            newFiles.mFiles[name] = std::move(payload);
            manifest.mEntries.push_back({ name, stock.mKey.mRace, stock.mKey.mSex, stock.mKey.mView, key });
        }

        // Check one unit; appends a line to the report and returns the number of problems found.
        std::size_t checkUnit(const Unit& unit, const MWWorld::ESMStore& store, const VFS::Manager& vfs,
            FileCache& files, const Body::CacheManifest& manifest, const Body::PackageFiles& cache, std::string& report)
        {
            StockBody stock = buildStockBody(unit, store, vfs, files);
            files.trim();
            const std::string label = stock.mKey.mRace + " " + stock.mKey.mSex;
            if (!stock.mError.empty())
            {
                const bool listed = std::any_of(
                    manifest.mFailures.begin(), manifest.mFailures.end(), [&stock](const Body::CacheFailure& f) {
                        return f.mRace == stock.mKey.mRace && f.mSex == stock.mKey.mSex;
                    });
                report += label + ": no body (" + stock.mError
                    + (listed ? "), listed as a failure in the manifest\n" : "), NOT in the manifest's failures\n");
                return listed ? 0 : 1;
            }
            const std::string key = Body::computeBodyKey(stock.mKey);
            const std::string name = packEntryNameOf(stock);
            if (!Body::isEntryCurrent(manifest, name, key))
            {
                report += label + ": STALE or missing (the key now is " + key.substr(0, 12) + ")\n";
                return 1;
            }
            const auto found = cache.mFiles.find(name);
            if (found == cache.mFiles.end())
            {
                report += label + ": the manifest lists '" + name + "' but the pack has no such entry\n";
                return 1;
            }
            const Body::BodyMeshReadResult read = Body::readBodyMesh(found->second);
            if (!read.mBody)
            {
                report += label + ": the payload does not read: " + read.mError + "\n";
                return 1;
            }
            const Body::MergeResult again = Body::mergeParts(stock.mParts, stock.mSeams, stock.mOptions, stock.mLimits);
            std::vector<std::uint8_t> rewritten;
            std::string error;
            if (!again.ok() || !Body::writeBodyMesh(again.mBody, rewritten, error))
            {
                report += label + ": merging again failed: " + (again.ok() ? error : again.mErrors.front()) + "\n";
                return 1;
            }
            std::size_t problems = 0;
            const bool sameBytes = rewritten == found->second;
            if (!sameBytes)
                ++problems;

            // Every stock vertex: its merged vertex, how far it moved, and the skinned difference at five poses.
            double maxMove = 0;
            std::size_t moved = 0;
            std::size_t vertices = 0;

            // WELD-AFFECTED by the merger: the stock vertex's merged vertex is shared with another input vertex or
            // belongs to a weld group.
            std::vector<std::uint32_t> sharing(again.mBody.mPositions.size(), 0);
            for (const std::vector<std::uint32_t>& map : again.mBody.mVertexMap)
            {
                for (const std::uint32_t target : map)
                    ++sharing[target];
            }
            std::vector<char> grouped(again.mBody.mPositions.size(), 0);
            for (const std::vector<std::uint32_t>& group : again.mBody.mWeldGroups)
            {
                for (const std::uint32_t member : group)
                    grouped[member] = 1;
            }

            // WELD-AFFECTED by the declared seams, found WITHOUT the merger (review fix A1): brute force over the stock
            // parts for every vertex that lies within a seam's tolerance of a vertex of the other slot, joined into
            // connected groups. The merger's own view above must equal this one, and the weights a weld gives a vertex
            // must equal the mean this check computes from the stock weights.
            std::vector<std::size_t> offset(stock.mParts.size() + 1, 0);
            for (std::size_t p = 0; p < stock.mParts.size(); ++p)
                offset[p + 1] = offset[p] + stock.mParts[p].mPositions.size();
            std::vector<std::vector<std::uint32_t>> neighbours(offset.back());
            const auto partOf = [&offset](std::size_t global) {
                return static_cast<std::size_t>(std::upper_bound(offset.begin(), offset.end(), global) - offset.begin())
                    - 1;
            };
            for (const Body::SeamDeclaration& seam : stock.mSeams)
            {
                for (std::size_t pa = 0; pa < stock.mParts.size(); ++pa)
                {
                    for (std::size_t pb = 0; pb < stock.mParts.size(); ++pb)
                    {
                        if (pa == pb || stock.mParts[pa].mSlot != seam.mSlotA || stock.mParts[pb].mSlot != seam.mSlotB)
                            continue;
                        for (std::size_t va = 0; va < stock.mParts[pa].mPositions.size(); ++va)
                        {
                            for (std::size_t vb = 0; vb < stock.mParts[pb].mPositions.size(); ++vb)
                            {
                                const Body::Vec3& a = stock.mParts[pa].mPositions[va];
                                const Body::Vec3& b = stock.mParts[pb].mPositions[vb];
                                const double ex = static_cast<double>(a[0]) - b[0];
                                const double ey = static_cast<double>(a[1]) - b[1];
                                const double ez = static_cast<double>(a[2]) - b[2];
                                if (ex * ex + ey * ey + ez * ez <= seam.mTolerance * seam.mTolerance)
                                {
                                    neighbours[offset[pa] + va].push_back(static_cast<std::uint32_t>(offset[pb] + vb));
                                    neighbours[offset[pb] + vb].push_back(static_cast<std::uint32_t>(offset[pa] + va));
                                }
                            }
                        }
                    }
                }
            }
            // The stock weights of a vertex as bone name and weight, summed by bone.
            const auto stockWeights = [&stock](std::size_t part, std::size_t vertex) {
                std::map<std::string, double> weights;
                const Body::PartMesh& mesh = stock.mParts[part];
                if (mesh.mSkin.empty())
                {
                    weights[mesh.mRigidBone] += 1.0;
                }
                else
                {
                    const Body::PartVertexSkin& skin = mesh.mSkin[vertex];
                    for (std::size_t k = 0; k < skin.mCount; ++k)
                        weights[mesh.mBones[skin.mInfluences[k].mBone]] += skin.mInfluences[k].mWeight;
                }
                return weights;
            };
            const auto mergedWeights = [&again](std::uint32_t target) {
                std::map<std::string, double> weights;
                for (std::size_t k = 0; k < Body::maxMergedInfluences; ++k)
                {
                    if (again.mBody.mBoneWeights[target][k] > 0.f)
                        weights[again.mBody.mBones[again.mBody.mBoneIndices[target][k]]]
                            += again.mBody.mBoneWeights[target][k];
                }
                return weights;
            };

            std::size_t weldAffected = 0; // the merger's view
            std::size_t independentAffected = 0; // the seams' view
            std::size_t setMismatches = 0;
            std::size_t weightMismatches = 0;
            std::size_t weightsNotChecked = 0;
            double worstWeightDifference = 0;
            std::size_t boundViolations = 0;
            std::array<double, 5> maxSkinUnmoved{}; // vertices the weld did not touch: must equal the stock skin
            std::array<double, 5> maxSkinMoved{}; // weld-affected vertices: differ by the weight disagreement
            struct Worst
            {
                double mDiff = 0;
                std::size_t mPart = 0;
                std::size_t mVertex = 0;
                int mPose = 0;
            } worst;
            for (std::size_t p = 0; p < stock.mParts.size(); ++p)
            {
                const Body::PartMesh& part = stock.mParts[p];
                for (std::size_t v = 0; v < part.mPositions.size(); ++v)
                {
                    ++vertices;
                    const std::uint32_t target = again.mBody.mVertexMap[p][v];
                    const std::array<double, 3> stockPos
                        = { part.mPositions[v][0], part.mPositions[v][1], part.mPositions[v][2] };
                    const std::array<double, 3> mergedPos = { again.mBody.mPositions[target][0],
                        again.mBody.mPositions[target][1], again.mBody.mPositions[target][2] };
                    const double move = distance(stockPos, mergedPos);
                    maxMove = std::max(maxMove, move);
                    if (move > 1e-4)
                        ++moved;
                    const bool isWelded = sharing[target] > 1 || grouped[target] != 0;
                    const bool isCandidate = !neighbours[offset[p] + v].empty();
                    if (isWelded)
                        ++weldAffected;
                    if (isCandidate)
                        ++independentAffected;
                    if (isWelded != isCandidate)
                        ++setMismatches;

                    // The weights the seams alone give this vertex: the mean over its connected group.
                    std::map<std::string, double> expected;
                    if (isCandidate)
                    {
                        std::set<std::uint32_t> group{ static_cast<std::uint32_t>(offset[p] + v) };
                        std::vector<std::uint32_t> queue(group.begin(), group.end());
                        for (std::size_t head = 0; head < queue.size(); ++head)
                        {
                            for (const std::uint32_t next : neighbours[queue[head]])
                            {
                                if (group.insert(next).second)
                                    queue.push_back(next);
                            }
                        }
                        for (const std::uint32_t member : group)
                        {
                            const std::size_t memberPart = partOf(member);
                            for (const auto& [bone, weight] : stockWeights(memberPart, member - offset[memberPart]))
                                expected[bone] += weight / static_cast<double>(group.size());
                        }
                        if (expected.size() > Body::maxMergedInfluences)
                        {
                            ++weightsNotChecked; // the merger cuts to four: this check does not model the cut
                        }
                        else
                        {
                            const std::map<std::string, double> actual = mergedWeights(target);
                            double difference = 0;
                            for (const auto& [bone, weight] : expected)
                            {
                                const auto inActual = actual.find(bone);
                                const double merged = inActual != actual.end() ? inActual->second : 0.0;
                                difference = std::max(difference, std::abs(weight - merged));
                            }
                            for (const auto& [bone, weight] : actual)
                            {
                                if (expected.find(bone) == expected.end())
                                    difference = std::max(difference, weight);
                            }
                            worstWeightDifference = std::max(worstWeightDifference, difference);
                            if (difference > 1e-4)
                                ++weightMismatches;
                        }
                    }

                    const std::map<std::string, double> before = stockWeights(p, v);
                    const std::map<std::string, double> after = mergedWeights(target);
                    for (int pose = 0; pose < 5; ++pose)
                    {
                        const double diff = distance(skinStock(part, v, pose), skinMerged(again.mBody, target, pose));
                        double& slot = isWelded ? maxSkinMoved[pose] : maxSkinUnmoved[pose];
                        slot = std::max(slot, diff);
                        if (!isWelded && diff > worst.mDiff)
                            worst = { diff, p, v, pose };
                        if (isWelded)
                        {
                            // The skinned difference of a weld-affected vertex is bounded by what its weights and its
                            // position changed (a rigid motion keeps lengths): sum over bones of the weight change
                            // times how far the bone's motion carries the vertex, plus how far the vertex moved.
                            std::set<std::string> bones;
                            for (const auto& entry : before)
                                bones.insert(entry.first);
                            for (const auto& entry : after)
                                bones.insert(entry.first);
                            double bound = move + 1e-4;
                            for (const std::string& bone : bones)
                            {
                                const double was = before.count(bone) != 0 ? before.at(bone) : 0.0;
                                const double is = after.count(bone) != 0 ? after.at(bone) : 0.0;
                                const std::array<double, 3> carried
                                    = moveVertex(boneMove(bone, pose), part.mPositions[v]);
                                bound += std::abs(was - is)
                                    * std::sqrt(
                                        carried[0] * carried[0] + carried[1] * carried[1] + carried[2] * carried[2]);
                            }
                            if (diff > bound)
                                ++boundViolations;
                        }
                    }
                }
            }
            std::string worstText;
            if (worst.mDiff > 1e-4)
            {
                const Body::PartMesh& part = stock.mParts[worst.mPart];
                worstText = "; worst: part '" + part.mName + "' vertex " + std::to_string(worst.mVertex) + " pose "
                    + std::to_string(worst.mPose) + ", stock bones";
                if (part.mSkin.empty())
                {
                    worstText += " " + part.mRigidBone + " 1";
                }
                else
                {
                    for (std::size_t k = 0; k < part.mSkin[worst.mVertex].mCount; ++k)
                    {
                        worstText += " " + part.mBones[part.mSkin[worst.mVertex].mInfluences[k].mBone] + " "
                            + std::to_string(part.mSkin[worst.mVertex].mInfluences[k].mWeight);
                    }
                }
                worstText += ", merged bones";
                const std::uint32_t target = again.mBody.mVertexMap[worst.mPart][worst.mVertex];
                for (std::size_t k = 0; k < Body::maxMergedInfluences; ++k)
                {
                    if (again.mBody.mBoneWeights[target][k] > 0.f)
                    {
                        worstText += " " + again.mBody.mBones[again.mBody.mBoneIndices[target][k]] + " "
                            + std::to_string(again.mBody.mBoneWeights[target][k]);
                    }
                }
            }
            const double worstUnmoved = *std::max_element(maxSkinUnmoved.begin(), maxSkinUnmoved.end());
            const double worstMoved = *std::max_element(maxSkinMoved.begin(), maxSkinMoved.end());
            std::string trouble;
            if (maxMove > weldTolerance + 1e-6 || worstUnmoved > 1e-4)
                trouble += " a vertex moved or the other vertices' skinning changed;";
            if (setMismatches > 0)
                trouble += " the merger welded " + std::to_string(weldAffected) + " vertices but the seams alone give "
                    + std::to_string(independentAffected) + ";";
            if (weightMismatches > 0)
                trouble += " " + std::to_string(weightMismatches) + " welded vertices have weights other than the mean"
                    " of their group;";
            if (boundViolations > 0)
                trouble += " " + std::to_string(boundViolations)
                    + " welded vertices differ from the stock skin by more than their weights and position explain;";
            if (!trouble.empty())
                ++problems;
            report += label + ": " + (problems == 0 ? "ok" : "PROBLEM") + ", payload bytes "
                + (sameBytes ? "identical to a new merge" : "DIFFER from a new merge") + ", " + std::to_string(vertices)
                + " stock vertices into " + std::to_string(again.mBody.mPositions.size()) + ", "
                + std::to_string(weldAffected) + " weld-affected by the merger and "
                + std::to_string(independentAffected) + " by the seams alone (" + std::to_string(moved)
                + " moved, largest move " + std::to_string(maxMove)
                + "), skinning at 5 poses: largest difference at the other vertices " + std::to_string(worstUnmoved)
                + " (must be 0), at weld-affected vertices " + std::to_string(worstMoved) + " (weight disagreement "
                + std::to_string(again.mBody.mReport.mMaxWeightDisagreement) + "; largest departure from the mean of "
                + "the group " + std::to_string(worstWeightDifference) + ", " + std::to_string(weightsNotChecked)
                + " not checked)" + worstText + (trouble.empty() ? "" : " --" + trouble) + "\n";
            return problems;
        }
    }

    int BodyBaker::problems() const
    {
        return static_cast<int>(mState->mProblems);
    }

    bool BodyBaker::update()
    {
        State& state = *mState;
        if (state.mDone)
            return true;
        // No exception reaches the game loop (a hostile record or model name, bad_alloc, a failed lookup): the run
        // ends, says so, and counts as a problem.
        try
        {
            return step();
        }
        catch (const std::exception& e)
        {
            Log(Debug::Error) << "Body tools: stopped by an error at unit " << state.mNext << " of "
                              << state.mUnits.size() << ": " << e.what();
        }
        catch (...)
        {
            Log(Debug::Error) << "Body tools: stopped by an unknown error at unit " << state.mNext << " of "
                              << state.mUnits.size();
        }
        ++state.mProblems;
        state.mDone = true;
        return true;
    }

    bool BodyBaker::step()
    {
        State& state = *mState;
        if (state.mDone)
            return true;
        const MWWorld::ESMStore* store = MWBase::Environment::get().getESMStore();
        const VFS::Manager* vfs = state.mResources.getVFS();

        if (!state.mStarted)
        {
            state.mStarted = true;
            state.mFiles.emplace(*vfs);
            // A new game puts the player in a fight and a death stops the game's update and with it the run (the
            // object bake found this): a bake is no play, switch god mode on.
            if (MWBase::World* world = MWBase::Environment::get().getWorld();
                world != nullptr && !world->getGodModeState())
                world->toggleGodMode();
            collectUnits(*store, state.mUnits);
            std::string error;
            if (state.mMode == Mode::Bake)
            {
                state.mOld = Body::loadCurrentCache(state.mFolder, error);
                if (state.mOld)
                {
                    const auto manifest = state.mOld->mFiles.find("manifest.txt");
                    if (manifest != state.mOld->mFiles.end())
                    {
                        state.mOldManifest = Body::readManifest(textOf(manifest->second), error);
                    }
                }
                Log(Debug::Info) << "Body bake: " << state.mUnits.size() << " races and sexes; "
                                 << (state.mOldManifest
                                            ? "a cache is published (entries with an unchanged key are kept)"
                                            : "no usable cache is published");
            }
            else
            {
                state.mCheckLoaded = false;
                std::optional<Body::PackageFiles> files = Body::loadCurrentCache(state.mFolder, error);
                if (!files || files->mFiles.find("manifest.txt") == files->mFiles.end())
                {
                    Log(Debug::Error) << "Body check: no cache to check: "
                                      << (files ? "no manifest in the pack" : error);
                    state.mDone = true;
                    return true;
                }
                const std::optional<Body::CacheManifest> manifest
                    = Body::readManifest(textOf(files->mFiles.at("manifest.txt")), error);
                if (!manifest)
                {
                    Log(Debug::Error) << "Body check: the manifest does not read: " << error;
                    state.mDone = true;
                    return true;
                }
                state.mCheckManifest = *manifest;
                state.mCheckFiles = std::move(*files);
                state.mCheckLoaded = true;
                Log(Debug::Info) << "Body check: " << state.mCheckManifest.mEntries.size() << " entries, "
                                 << state.mCheckManifest.mFailures.size() << " failures in the cache; "
                                 << state.mUnits.size() << " races and sexes in the data.";
            }
            return false;
        }

        if (state.mNext < state.mUnits.size())
        {
            const Unit& unit = state.mUnits[state.mNext++];
            if (state.mMode == Mode::Bake)
            {
                bakeUnit(state.mMode, unit, *store, *vfs, *state.mFiles, state.mOld, state.mOldManifest,
                    state.mNewFiles, state.mNewManifest, state.mMerged, state.mReused, state.mPayloadBytes);
            }
            else
            {
                ++state.mChecked;
                state.mProblems += checkUnit(
                    unit, *store, *vfs, *state.mFiles, state.mCheckManifest, state.mCheckFiles, state.mReport);
            }
            return false;
        }

        if (state.mMode == Mode::Bake)
        {
            Body::CacheManifest& manifest = state.mNewManifest;
            manifest.mMergerVersion = Body::bodyMergerVersion;
            manifest.mLayoutVersion = Body::bodyMeshLayoutVersion;
            std::sort(manifest.mEntries.begin(), manifest.mEntries.end(),
                [](const Body::CacheEntry& a, const Body::CacheEntry& b) { return a.mName < b.mName; });
            std::error_code code;
            std::filesystem::create_directories(state.mFolder, code);
            if (manifest.mEntries.empty())
            {
                // Every body failed (the data or the files are missing, say): a cache of nothing must not replace a
                // good one. The pointer stays; the status file says why.
                Log(Debug::Error) << "Body bake: no body could be merged (" << manifest.mFailures.size()
                                  << " failures); the published cache, if any, is left as it is.";
                if (!writeAtomic(state.mFolder / "status.json", Body::writeStatusJson(manifest, "failed")))
                    ++state.mProblems;
                ++state.mProblems;
            }
            else
            {
                state.mNewFiles.mFiles["manifest.txt"] = bytesOf(Body::writeManifest(manifest));
                std::string packName;
                std::string error;
                std::string previous = readFile(state.mFolder / "current");
                if (!Body::publishCache(state.mFolder, state.mNewFiles, packName, error))
                {
                    Log(Debug::Error) << "Body bake: cannot publish the cache: " << error;
                    writeAtomic(state.mFolder / "status.json", Body::writeStatusJson(manifest, "failed"));
                    ++state.mProblems;
                }
                else
                {
                    if (!previous.empty() && previous.back() == '\n')
                        previous.pop_back();
                    const bool same = previous == packName;
                    // Housekeeping: the new pack and the one before it stay (a game that has the old one open keeps
                    // reading it); older ones go.
                    const std::size_t pruned = Body::pruneOldPacks(state.mFolder, { packName, previous });
                    if (!writeAtomic(state.mFolder / "status.json", Body::writeStatusJson(manifest, "complete")))
                        ++state.mProblems;
                    Log(Debug::Info) << "Body bake: " << (same ? "up to date (the same pack as before)" : "published")
                                     << " " << packName << ": " << manifest.mEntries.size() << " bodies ("
                                     << state.mMerged << " merged, " << state.mReused << " kept), "
                                     << manifest.mFailures.size() << " failures, " << state.mPayloadBytes
                                     << " payload bytes, " << pruned << " old pack(s) deleted.";
                }
            }
        }
        else if (state.mCheckLoaded)
        {
            state.mReport += std::to_string(state.mChecked) + " races and sexes checked, "
                + std::to_string(state.mProblems) + " problem(s).\n";
            std::error_code code;
            std::filesystem::create_directories(state.mFolder, code);
            if (!writeAtomic(state.mFolder / "check.txt", state.mReport))
                ++state.mProblems;
            Log(Debug::Info) << "Body check:\n" << state.mReport;
        }
        state.mDone = true;
        return true;
    }
}
