#include "bodyanalyser.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <components/bodynif/analysisreport.hpp>
#include <components/bodynif/seamanalysis.hpp>
#include <components/bodynif/skinnedshape.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm3/loadbody.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/files/conversion.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/mwanimation/partbones.hpp>
#include <components/nif/niffile.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/settings/settings.hpp>
#include <components/vfs/manager.hpp>

#include "mwbase/environment.hpp"
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

        // The slots of a body from the neck down (research note 0032 section 1); the head is a part of its own.
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

        // A boundary vertex counts as on a seam when the other part has a boundary vertex within this many Morrowind
        // units (about 70 to the metre, Constants::UnitsPerMeter: a human is about 130 units tall). The first run used
        // 2 and found the overlaps larger than that; 6 is wider than every overlap seen in it and narrower than the
        // distance between two joints.
        constexpr double seamWindow = 6.0;

        BodyNif::SeamOptions seamOptions()
        {
            BodyNif::SeamOptions options;
            options.mWindow = seamWindow;
            return options;
        }

        struct SeamDef
        {
            const char* mLabel;
            const char* mA;
            const char* mB;
        };

        // Each adjacent pair of slots; both sides of a limb share a label so that they are pooled in the aggregate.
        constexpr SeamDef seamDefs[] = {
            { "neck-chest", "neck", "cuirass" },
            { "chest-groin", "cuirass", "groin" },
            { "chest-upperarm", "cuirass", "rupperarm" },
            { "chest-upperarm", "cuirass", "lupperarm" },
            { "upperarm-forearm", "rupperarm", "rforearm" },
            { "upperarm-forearm", "lupperarm", "lforearm" },
            { "forearm-wrist", "rforearm", "rwrist" },
            { "forearm-wrist", "lforearm", "lwrist" },
            { "wrist-hand", "rwrist", "rhand" },
            { "wrist-hand", "lwrist", "lhand" },
            { "groin-upperleg", "groin", "rleg" },
            { "groin-upperleg", "groin", "lleg" },
            { "upperleg-knee", "rleg", "rknee" },
            { "upperleg-knee", "lleg", "lknee" },
            { "knee-ankle", "rknee", "rankle" },
            { "knee-ankle", "lknee", "lankle" },
            { "ankle-foot", "rankle", "rfoot" },
            { "ankle-foot", "lankle", "lfoot" },
            { "groin-tail", "groin", "tail" },
        };

        struct Unit
        {
            const ESM::Race* mRace = nullptr;
            bool mFemale = false;
        };

        // One parsed NIF file kept for the unit being analysed (a stock hand file is read once for both sides).
        struct ParsedFile
        {
            std::unique_ptr<Nif::NIFFile> mFile;
            std::uint64_t mBytes = 0;
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
                    parsed.mBytes = bytes.size();
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

        // Reads the shapes the skin filter selects from every root of a parsed file; returns the shapes and appends
        // reader errors to `errors`. `fileShapes` gets the number of shapes the file holds without a filter.
        std::vector<BodyNif::Shape> shapesOf(const ParsedFile& parsed, const std::string& filter,
            const BodyNif::Placement& placement, std::vector<std::string>& errors, std::size_t* fileShapes)
        {
            std::vector<BodyNif::Shape> shapes;
            if (fileShapes != nullptr)
                *fileShapes = 0;
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
                if (fileShapes != nullptr)
                    *fileShapes += BodyNif::readShapes(*node, "").mShapes.size();
            }
            return shapes;
        }

        std::string materialKey(const BodyNif::Shape& shape)
        {
            return shape.mTexture + (shape.mAlphaTest ? "|a" : shape.mAlphaBlend ? "|b" : "|-");
        }

        // Fills a slot report from the shapes; returns the part.
        BodyNif::PartMesh describeSlot(BodyNif::SlotReport& slot, const std::vector<BodyNif::Shape>& shapes)
        {
            BodyNif::PartMesh part = BodyNif::makePartMesh(slot.mSlot, shapes);
            slot.mStats = BodyNif::analysePart(part);
            std::set<std::string> materials, bones;
            for (const BodyNif::Shape& shape : shapes)
            {
                slot.mShapeNames.push_back(shape.mName);
                materials.insert(materialKey(shape));
                bones.insert(shape.mBones.begin(), shape.mBones.end());
                slot.mAllIdentity = slot.mAllIdentity && shape.mIdentityTransform;
                slot.mAnyMirrored = slot.mAnyMirrored || shape.mMirrored;
                slot.mPlaced = slot.mPlaced || shape.mPlaced;
            }
            slot.mMaterials.assign(materials.begin(), materials.end());
            slot.mBoneNames.assign(bones.begin(), bones.end());
            slot.mRead = !shapes.empty();
            return part;
        }

        BodyNif::UnitReport analyseUnit(const Unit& unit, const MWWorld::ESMStore& store, const VFS::Manager& vfs)
        {
            BodyNif::UnitReport report;
            report.mRace = unit.mRace->mId.getRefIdString();
            report.mFemale = unit.mFemale;
            report.mBeast = (unit.mRace->mData.mFlags & ESM::Race::Beast) != 0;
            report.mPlayable = (unit.mRace->mData.mFlags & ESM::Race::Playable) != 0;

            const auto& bodyParts = store.get<ESM::BodyPart>();
            const std::vector<const ESM::BodyPart*>& chosen
                = MWRender::getBodyParts(unit.mRace->mId, unit.mFemale, false, false, bodyParts);

            // The skeleton whose bones the rigid parts hang on (Context::pickSkeleton: beast, female, else the base
            // one).
            const char* skeletonSetting = report.mBeast ? "baseanimkna" : unit.mFemale ? "baseanimfemale" : "baseanim";
            report.mSkeleton = VFS::Path::Normalized(Settings::Manager::getString(skeletonSetting, "Models")).value();
            std::map<std::string, ParsedFile> files;
            const auto fileFor = [&](const std::string& path) -> const ParsedFile& {
                auto it = files.find(path);
                if (it == files.end())
                    it = files.emplace(path, parseFile(vfs, path)).first;
                return it->second;
            };
            BodyNif::RestPose rest;
            {
                const ParsedFile& skeleton = fileFor(report.mSkeleton);
                if (!skeleton.mError.empty())
                    Log(Debug::Warning) << "Body analysis: skeleton " << report.mSkeleton << ": " << skeleton.mError;
                else if (skeleton.mFile != nullptr)
                {
                    for (const Nif::Record* root : skeleton.mFile->mRoots)
                    {
                        const auto* node = dynamic_cast<const Nif::NiAVObject*>(root);
                        if (node != nullptr)
                            rest.merge(BodyNif::readRestPose(*node));
                    }
                }
                report.mSkeletonBones = rest.size();
            }

            std::map<std::string, BodyNif::PartMesh> parts;
            for (const SlotDef& def : slotDefs)
            {
                BodyNif::SlotReport slot;
                slot.mSlot = def.mName;
                const ESM::BodyPart* record = def.mType < chosen.size() ? chosen[def.mType] : nullptr;
                if (record != nullptr)
                {
                    slot.mRecord = record->mId.getRefIdString();
                    slot.mModel = Misc::ResourceHelpers::correctMeshPath(VFS::Path::Normalized(record->mModel)).value();
                    const ParsedFile& parsed = fileFor(slot.mModel);
                    slot.mFileBytes = parsed.mBytes;
                    if (!parsed.mError.empty())
                        slot.mErrors.push_back("file: " + parsed.mError);
                    const std::string& filter = MWAnim::sPartBones.at(def.mType);
                    const BodyNif::Placement placement{ &rest, filter };
                    std::vector<BodyNif::Shape> shapes
                        = shapesOf(parsed, filter, placement, slot.mErrors, &slot.mFileShapes);
                    if (shapes.empty() && parsed.mError.empty())
                        slot.mErrors.push_back("no shape selected by the skin filter '" + filter + "'");
                    BodyNif::PartMesh part = describeSlot(slot, shapes);
                    if (slot.mRead)
                        parts.emplace(slot.mSlot, std::move(part));
                }
                report.mSlots.push_back(std::move(slot));
            }

            for (const SeamDef& def : seamDefs)
            {
                const auto a = parts.find(def.mA);
                const auto b = parts.find(def.mB);
                if (a == parts.end() || b == parts.end())
                    continue;
                report.mSeams.push_back(
                    { def.mLabel, def.mA, def.mB, BodyNif::analyseSeam(a->second, b->second, seamOptions()) });
            }

            // The head records of this race and sex (vampire heads are another set and are left out).
            std::vector<const ESM::BodyPart*> heads;
            for (const ESM::BodyPart& record : bodyParts)
            {
                if (record.mData.mPart != ESM::BodyPart::MP_Head || record.mData.mType != ESM::BodyPart::MT_Skin
                    || record.mData.mVampire != 0 || record.mRace != unit.mRace->mId)
                    continue;
                if (((record.mData.mFlags & ESM::BodyPart::BPF_Female) != 0) != unit.mFemale)
                    continue;
                heads.push_back(&record);
            }
            std::sort(heads.begin(), heads.end(), [](const ESM::BodyPart* left, const ESM::BodyPart* right) {
                return left->mId.getRefIdString() < right->mId.getRefIdString();
            });
            const auto neck = parts.find("neck");
            for (const ESM::BodyPart* record : heads)
            {
                BodyNif::HeadEntry head;
                head.mRecord = record->mId.getRefIdString();
                head.mModel = Misc::ResourceHelpers::correctMeshPath(VFS::Path::Normalized(record->mModel)).value();
                const ParsedFile& parsed = fileFor(head.mModel);
                std::vector<std::string> errors;
                const BodyNif::Placement placement{ &rest, "Head" };
                const std::vector<BodyNif::Shape> shapes = shapesOf(parsed, "Head", placement, errors, nullptr);
                if (!shapes.empty())
                {
                    const BodyNif::PartMesh part = BodyNif::makePartMesh("head", shapes);
                    head.mRead = true;
                    head.mStats = BodyNif::analysePart(part);
                    if (neck != parts.end())
                        head.mNeckSeam = BodyNif::analyseSeam(neck->second, part, seamOptions());
                }
                report.mHeads.push_back(std::move(head));
            }

            BodyNif::computeTotals(report);
            return report;
        }

        void writeFile(const std::filesystem::path& path, const std::string& text)
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
        }
    }

    struct BodyAnalyser::State
    {
        State(std::filesystem::path outFolder, Resource::ResourceSystem& resources)
            : mOut(std::move(outFolder))
            , mResources(resources)
        {
        }

        std::filesystem::path mOut;
        Resource::ResourceSystem& mResources;
        bool mStarted = false;
        bool mDone = false;
        std::vector<Unit> mUnits;
        std::size_t mNext = 0;
        BodyNif::AnalysisReport mReport;
    };

    BodyAnalyser::BodyAnalyser(std::filesystem::path outFolder, Resource::ResourceSystem& resources)
        : mState(std::make_unique<State>(std::move(outFolder), resources))
    {
    }

    BodyAnalyser::~BodyAnalyser() = default;

    bool BodyAnalyser::update()
    {
        State& state = *mState;
        if (state.mDone)
            return true;
        const MWWorld::ESMStore* store = MWBase::Environment::get().getESMStore();
        const VFS::Manager* vfs = state.mResources.getVFS();

        if (!state.mStarted)
        {
            state.mStarted = true;
            for (const ESM::Race& race : store->get<ESM::Race>())
            {
                state.mUnits.push_back({ &race, false });
                state.mUnits.push_back({ &race, true });
            }
            std::stable_sort(state.mUnits.begin(), state.mUnits.end(), [](const Unit& left, const Unit& right) {
                const std::string a = left.mRace->mId.getRefIdString();
                const std::string b = right.mRace->mId.getRefIdString();
                return a != b ? a < b : left.mFemale < right.mFemale;
            });
            Log(Debug::Info) << "Body analysis: " << state.mUnits.size() << " races and sexes.";
            return false;
        }

        if (state.mNext < state.mUnits.size())
        {
            const Unit& unit = state.mUnits[state.mNext++];
            state.mReport.mUnits.push_back(analyseUnit(unit, *store, *vfs));
            const BodyNif::UnitReport& done = state.mReport.mUnits.back();
            Log(Debug::Info) << "Body analysis: " << done.mRace << (done.mFemale ? " female" : " male") << ": "
                             << done.mVertices << " vertices, " << done.mSkinnedParts << " skinned and "
                             << done.mRigidParts << " rigid parts, " << done.mSeams.size() << " seams, "
                             << done.mHeads.size() << " heads.";
            return false;
        }

        const std::filesystem::path folder = state.mOut / "analysis";
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        writeFile(folder / "bodies.json", BodyNif::toJson(state.mReport));
        writeFile(folder / "bodies.txt", BodyNif::toText(state.mReport));
        Log(Debug::Info) << "Body analysis written to " << Files::pathToUnicodeString(folder);
        state.mDone = true;
        return true;
    }
}
