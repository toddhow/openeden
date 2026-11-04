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

#include "mwbase/environment.hpp"
#include "mwbase/world.hpp"
#include "mwrender/bodyparts.hpp"
#include "mwworld/esmstore.hpp"

namespace OMW
{
    namespace
    {
        struct SlotDef
        {
            ESM::PartReferenceType mType;
            const char* mName;
        };

        // The slots of a body from the neck down (research note 0032 section 1; the same list as the analysis); the
        // head is a part of its own (decision H14). The slot number of a region is the engine's part type.
        constexpr SlotDef slotDefs[] = {
            { ESM::PRT_Neck, "neck" },
            { ESM::PRT_Cuirass, "cuirass" },
            { ESM::PRT_Groin, "groin" },
            { ESM::PRT_RUpperarm, "rupperarm" },
            { ESM::PRT_LUpperarm, "lupperarm" },
            { ESM::PRT_RForearm, "rforearm" },
            { ESM::PRT_LForearm, "lforearm" },
            { ESM::PRT_RWrist, "rwrist" },
            { ESM::PRT_LWrist, "lwrist" },
            { ESM::PRT_RHand, "rhand" },
            { ESM::PRT_LHand, "lhand" },
            { ESM::PRT_RLeg, "rleg" },
            { ESM::PRT_LLeg, "lleg" },
            { ESM::PRT_RKnee, "rknee" },
            { ESM::PRT_LKnee, "lknee" },
            { ESM::PRT_RAnkle, "rankle" },
            { ESM::PRT_LAnkle, "lankle" },
            { ESM::PRT_RFoot, "rfoot" },
            { ESM::PRT_LFoot, "lfoot" },
            { ESM::PRT_Tail, "tail" },
        };

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

        struct Unit
        {
            const ESM::Race* mRace = nullptr;
            bool mFemale = false;
        };

        std::string sexName(bool female)
        {
            return female ? "female" : "male";
        }

        // One file read from the VFS: its digest (of the bytes: a loose file has none), size and parsed records.
        struct ParsedFile
        {
            std::unique_ptr<Nif::NIFFile> mFile;
            std::string mDigest;
            std::string mError;
        };

        ParsedFile parseFile(const VFS::Manager& vfs, const std::string& path)
        {
            ParsedFile parsed;
            try
            {
                {
                    Files::IStreamPtr stream = vfs.get(VFS::Path::Normalized(path));
                    const std::string bytes(
                        (std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
                    EdenArchive::Hasher hasher;
                    hasher.update(bytes.data(), bytes.size());
                    parsed.mDigest = EdenArchive::toHex(hasher.finalize());
                }
                parsed.mFile = std::make_unique<Nif::NIFFile>(VFS::Path::Normalized(path));
                Nif::Reader reader(*parsed.mFile);
                reader.parse(vfs.get(VFS::Path::Normalized(path)));
            }
            catch (const std::exception& e)
            {
                parsed.mFile.reset();
                parsed.mError = e.what();
            }
            return parsed;
        }

        std::vector<BodyNif::Shape> shapesOf(const ParsedFile& parsed, const std::string& filter,
            const BodyNif::Placement& placement, std::vector<std::string>& errors)
        {
            std::vector<BodyNif::Shape> shapes;
            if (!parsed.mFile)
                return shapes;
            for (const Nif::Record* root : parsed.mFile->mRoots)
            {
                const auto* node = dynamic_cast<const Nif::NiAVObject*>(root);
                if (node == nullptr)
                    continue;
                BodyNif::ReadResult result = BodyNif::readShapes(*node, filter, {}, placement);
                for (BodyNif::Shape& shape : result.mShapes)
                    shapes.push_back(std::move(shape));
                for (std::string& error : result.mErrors)
                    errors.push_back(std::move(error));
            }
            return shapes;
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
            body.mKey.mWeldTolerance = weldTolerance;
            body.mKey.mLayoutVersion = Body::bodyMeshLayoutVersion;
            body.mSeams = exactSeams();
            for (const SlotDef& def : slotDefs)
                body.mOptions.mBoneOrder.push_back(Misc::StringUtils::lowerCase(MWAnim::sPartBones.at(def.mType)));

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

        void writeFile(const std::filesystem::path& path, const std::string& text)
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
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
                const Body::MergeResult result = Body::mergeParts(stock.mParts, stock.mSeams, stock.mOptions);
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
            const Body::MergeResult again = Body::mergeParts(stock.mParts, stock.mSeams, stock.mOptions);
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
            // A stock vertex is WELD-AFFECTED when its merged vertex is shared with another input vertex or belongs to
            // a weld group: there the merger gives the mean of the members' weights, which differs from the stock
            // weights by design (a rigid forearm and a rigid wrist hold the seam with weight 1 on different bones).
            std::vector<std::uint32_t> sharing(again.mBody.mPositions.size(), 0);
            for (const std::vector<std::uint32_t>& map : again.mBody.mVertexMap)
                for (const std::uint32_t target : map)
                    ++sharing[target];
            std::vector<char> grouped(again.mBody.mPositions.size(), 0);
            for (const std::vector<std::uint32_t>& group : again.mBody.mWeldGroups)
                for (const std::uint32_t member : group)
                    grouped[member] = 1;
            std::size_t weldAffected = 0;
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
                    if (isWelded)
                        ++weldAffected;
                    for (int pose = 0; pose < 5; ++pose)
                    {
                        const double diff = distance(skinStock(part, v, pose), skinMerged(again.mBody, target, pose));
                        double& slot = isWelded ? maxSkinMoved[pose] : maxSkinUnmoved[pose];
                        slot = std::max(slot, diff);
                        if (!isWelded && diff > worst.mDiff)
                            worst = { diff, p, v, pose };
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
                    worstText += " " + part.mRigidBone + " 1";
                else
                    for (std::size_t k = 0; k < part.mSkin[worst.mVertex].mCount; ++k)
                        worstText += " " + part.mBones[part.mSkin[worst.mVertex].mInfluences[k].mBone] + " "
                            + std::to_string(part.mSkin[worst.mVertex].mInfluences[k].mWeight);
                worstText += ", merged bones";
                const std::uint32_t target = again.mBody.mVertexMap[worst.mPart][worst.mVertex];
                for (std::size_t k = 0; k < Body::maxMergedInfluences; ++k)
                    if (again.mBody.mBoneWeights[target][k] > 0.f)
                        worstText += " " + again.mBody.mBones[again.mBody.mBoneIndices[target][k]] + " "
                            + std::to_string(again.mBody.mBoneWeights[target][k]);
            }
            const double worstUnmoved = *std::max_element(maxSkinUnmoved.begin(), maxSkinUnmoved.end());
            const double worstMoved = *std::max_element(maxSkinMoved.begin(), maxSkinMoved.end());
            if (maxMove > weldTolerance + 1e-6 || worstUnmoved > 1e-4)
                ++problems;
            report += label + ": " + (problems == 0 ? "ok" : "PROBLEM") + ", payload bytes "
                + (sameBytes ? "identical to a new merge" : "DIFFER from a new merge") + ", " + std::to_string(vertices)
                + " stock vertices into " + std::to_string(again.mBody.mPositions.size()) + ", "
                + std::to_string(weldAffected) + " weld-affected (" + std::to_string(moved) + " moved, largest move "
                + std::to_string(maxMove) + "), skinning at 5 poses: largest difference at " + "the other vertices "
                + std::to_string(worstUnmoved) + " (must be 0), at weld-affected vertices " + std::to_string(worstMoved)
                + " (weight disagreement " + std::to_string(again.mBody.mReport.mMaxWeightDisagreement) + ")"
                + worstText + "\n";
            return problems;
        }
    }

    bool BodyBaker::update()
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
            state.mNewFiles.mFiles["manifest.txt"] = bytesOf(Body::writeManifest(manifest));
            std::string packName;
            std::string error;
            const std::string previous = readFile(state.mFolder / "current");
            if (!Body::publishCache(state.mFolder, state.mNewFiles, packName, error))
            {
                Log(Debug::Error) << "Body bake: cannot publish the cache: " << error;
                writeFile(state.mFolder / "status.json", Body::writeStatusJson(manifest, "failed"));
            }
            else
            {
                writeFile(state.mFolder / "status.json", Body::writeStatusJson(manifest, "complete"));
                const bool upToDate = previous == packName + "\n";
                Log(Debug::Info) << "Body bake: " << (upToDate ? "up to date (the same pack as before)" : "published")
                                 << " " << packName << ": " << manifest.mEntries.size() << " bodies (" << state.mMerged
                                 << " merged, " << state.mReused << " kept), " << manifest.mFailures.size()
                                 << " failures, " << state.mPayloadBytes << " payload bytes.";
            }
        }
        else if (state.mCheckLoaded)
        {
            state.mReport += std::to_string(state.mChecked) + " races and sexes checked, "
                + std::to_string(state.mProblems) + " problem(s).\n";
            std::error_code code;
            std::filesystem::create_directories(state.mFolder, code);
            writeFile(state.mFolder / "check.txt", state.mReport);
            Log(Debug::Info) << "Body check:\n" << state.mReport;
        }
        state.mDone = true;
        return true;
    }
}
