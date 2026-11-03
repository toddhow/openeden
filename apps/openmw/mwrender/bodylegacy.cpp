#include "bodylegacy.hpp"

#include <components/misc/constants.hpp>
#include <components/mwanimation/partbones.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bodyparts.hpp"

namespace MWRender
{
    namespace
    {
        // The body slots of the package, in ESM::PartReferenceType order (the order of the notes and meshes). Head and
        // hair are chosen separately. Skirt, shield, pauldrons and weapon are not body parts.
        struct Slot
        {
            ESM::PartReferenceType mType;
            const char* mName;
        };

        constexpr std::array<Slot, 20> sSlots = { {
            { ESM::PRT_Neck, "neck" },
            { ESM::PRT_Cuirass, "cuirass" },
            { ESM::PRT_Groin, "groin" },
            { ESM::PRT_RHand, "rhand" },
            { ESM::PRT_LHand, "lhand" },
            { ESM::PRT_RWrist, "rwrist" },
            { ESM::PRT_LWrist, "lwrist" },
            { ESM::PRT_RForearm, "rforearm" },
            { ESM::PRT_LForearm, "lforearm" },
            { ESM::PRT_RUpperarm, "rupperarm" },
            { ESM::PRT_LUpperarm, "lupperarm" },
            { ESM::PRT_RFoot, "rfoot" },
            { ESM::PRT_LFoot, "lfoot" },
            { ESM::PRT_RAnkle, "rankle" },
            { ESM::PRT_LAnkle, "lankle" },
            { ESM::PRT_RKnee, "rknee" },
            { ESM::PRT_LKnee, "lknee" },
            { ESM::PRT_RLeg, "rleg" },
            { ESM::PRT_LLeg, "lleg" },
            { ESM::PRT_Tail, "tail" },
        } };

        constexpr std::size_t sMaxPackageIdBytes = 64; // Body::isValidPackageId
        constexpr std::string_view sIdPrefix = "morrowind.";

        // The root bone of Morrowind's humanoid skeleton. **verify** against the user's data (the stage e report
        // shows it): the shipped skeleton profile does not name a root, so the name here is the one Morrowind's
        // skeleton.nif is believed to use, listed as an unmapped bone.
        constexpr std::string_view sRootBone = "Bip01";

        // Morrowind model paths use backslashes. Package paths use '/' (Body::isSafePackagePath): the one place where
        // the separator is converted.
        std::string packagePath(std::string model)
        {
            std::replace(model.begin(), model.end(), '\\', '/');
            return model;
        }

        std::string packageId(const ESM::RefId& race, bool female)
        {
            const std::string suffix = female ? ".female" : ".male";
            std::string name;
            for (const char c : race.getRefIdString())
            {
                if (c >= 'A' && c <= 'Z')
                {
                    name += static_cast<char>(c - 'A' + 'a');
                }
                else
                {
                    const bool valid
                        = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
                    name += valid ? c : '_';
                }
            }
            const std::size_t room = sMaxPackageIdBytes - sIdPrefix.size() - suffix.size();
            if (name.size() > room)
            {
                name.resize(room);
            }
            return std::string(sIdPrefix) + name + suffix;
        }

        bool isFirstPersonPart(const ESM::BodyPart& part)
        {
            // The same rule as getBodyParts: a record whose id ends in "1st" is a first person part.
            const std::string id = part.mId.getRefIdString();
            return id.size() >= 3 && id.compare(id.size() - 3, 3, "1st") == 0;
        }

        // The first record in store order that can serve as the reference head (or hair) of a plain human of this race
        // and sex: a skin part of the wanted kind, not a vampire part, playable, not first person. Asks the store; the
        // model path itself comes from getHeadModel / getHairModel.
        const ESM::BodyPart* firstSuitable(const MWWorld::Store<ESM::BodyPart>& store, const ESM::RefId& race,
            bool female, ESM::BodyPart::MeshPart wanted)
        {
            for (const ESM::BodyPart& record : store)
            {
                if (record.mData.mType != ESM::BodyPart::MT_Skin || record.mData.mPart != wanted
                    || record.mData.mVampire != 0 || (record.mData.mFlags & ESM::BodyPart::BPF_NotPlayable) != 0
                    || record.mRace != race || isFirstPersonPart(record))
                {
                    continue;
                }
                const bool isFemale = (record.mData.mFlags & ESM::BodyPart::BPF_Female) != 0;
                if (isFemale == female)
                {
                    return &record;
                }
            }
            return nullptr;
        }

        // Morrowind units to metres for the bounds; counts, names and weights are unit free.
        Body::MeshSummary inBodyUnits(Body::MeshSummary summary)
        {
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                summary.mBoundsMin[axis] = toBodyUnits(summary.mBoundsMin[axis]);
                summary.mBoundsMax[axis] = toBodyUnits(summary.mBoundsMax[axis]);
            }
            return summary;
        }

        void addMesh(LegacyBody& body, const LegacyOptions& options, const std::string& name, const std::string& model,
            Body::MeshRole role, const std::string& attachBone)
        {
            Body::MeshEntry mesh;
            mesh.mId = name;
            mesh.mPath = packagePath(model);
            mesh.mFormat = Body::MeshFormat::Nif;
            mesh.mLod = 0;
            mesh.mRole = role;
            mesh.mPartType = name;
            mesh.mAttachBone = attachBone;
            if (options.mMeshSource != nullptr)
            {
                std::optional<Body::MeshSummary> summary = options.mMeshSource->summarize(mesh.mPath, "nif");
                if (summary)
                {
                    mesh.mSummary = inBodyUnits(std::move(*summary));
                }
            }
            if (!mesh.mSummary)
            {
                body.mNotes.push_back(
                    { "LEGACY-MESH-001", name, "no mesh summary is available for part '" + name + "'" });
            }
            body.mPackage.mMeshes.push_back(std::move(mesh));
        }
    }

    double toBodyUnits(double morrowindUnits)
    {
        return morrowindUnits / static_cast<double>(Constants::UnitsPerMeter);
    }

    LegacyBody buildLegacyBody(
        const ESM::RefId& race, bool female, const MWWorld::Store<ESM::BodyPart>& store, const LegacyOptions& options)
    {
        LegacyBody body;
        body.mSupported = !options.mBeast;
        Body::BodyPackage& package = body.mPackage;

        const std::string sex = female ? "female" : "male";
        package.mIdentity.mId = packageId(race, female);
        package.mIdentity.mName = "Morrowind " + race.getRefIdString() + " " + sex + " (legacy body parts)";
        package.mIdentity.mVersion = "1.0.0";
        package.mIdentity.mAuthor = "assembled from the game's body part records";
        package.mIdentity.mLicense = "game data, not redistributable";
        package.mIdentity.mAgeClass = Body::AgeClass::Unspecified;

        if (options.mProfile)
        {
            package.mBinding.mSkeletonProfile = options.mProfile;
        }
        else
        {
            package.mBinding.mSkeletonPath = options.mProfilePath;
        }
        package.mBinding.mRootBone = std::string(sRootBone);
        package.mBinding.mUnmappedBones.emplace_back(sRootBone);

        // The slots. getBodyParts decides what stands in each; the adapter only reads its answer.
        const std::vector<const ESM::BodyPart*>& parts = getBodyParts(race, female, false, false, store);
        for (const Slot& slot : sSlots)
        {
            const ESM::BodyPart* part = parts[slot.mType];
            if (part == nullptr)
            {
                body.mNotes.push_back({ "LEGACY-MISSING-001", slot.mName,
                    std::string("no body part record for slot '") + slot.mName + "': armour or nothing covers it" });
                continue;
            }
            if (female && (part->mData.mFlags & ESM::BodyPart::BPF_Female) == 0)
            {
                body.mNotes.push_back({ "LEGACY-FALLBACK-001", slot.mName,
                    std::string("slot '") + slot.mName + "' uses the male part '" + part->mId.getRefIdString()
                        + "' in place of a female one" });
            }
            addMesh(body, options, slot.mName, part->mModel, Body::MeshRole::Skin, MWAnim::sPartBones.at(slot.mType));
        }

        // Head and hair.
        if (const ESM::BodyPart* head = firstSuitable(store, race, female, ESM::BodyPart::MP_Head))
        {
            const std::string model = getHeadModel(false, false, female, race, head->mId, store);
            body.mNotes.push_back({ "LEGACY-HEAD-001", "head",
                "head record '" + head->mId.getRefIdString() + "' is the first suitable one in store order" });
            addMesh(body, options, "head", model, Body::MeshRole::Head, MWAnim::sPartBones.at(ESM::PRT_Head));
        }
        else
        {
            body.mNotes.push_back(
                { "LEGACY-HEAD-002", "head", "the store has no suitable head record for this race and sex" });
        }
        if (const ESM::BodyPart* hair = firstSuitable(store, race, female, ESM::BodyPart::MP_Hair))
        {
            const std::string model = getHairModel(false, hair->mId, store);
            body.mNotes.push_back({ "LEGACY-HAIR-001", "hair",
                "hair record '" + hair->mId.getRefIdString() + "' is the first suitable one in store order" });
            addMesh(body, options, "hair", model, Body::MeshRole::Hair, MWAnim::sPartBones.at(ESM::PRT_Hair));
        }
        else
        {
            body.mNotes.push_back(
                { "LEGACY-HAIR-002", "hair", "the store has no suitable hair record for this race and sex" });
        }

        // Package-wide notes.
        if (options.mBeast)
        {
            body.mNotes.push_back({ "LEGACY-BEAST-001", "",
                "beast races use another skeleton: not supported yet, the package describes the human parts only" });
        }
        body.mNotes.push_back({ "LEGACY-AGE-001", "",
            "Morrowind records carry no age class: the package is \"unspecified\" and never eligible for a restricted "
            "tier" });
        return body;
    }

    std::vector<std::string> describeLegacyCheck(const LegacyBody& body, const Body::Report& report)
    {
        std::vector<std::string> lines;
        lines.push_back("bodycheck: " + body.mPackage.mIdentity.mId + (body.mSupported ? "" : " (not supported yet)"));
        lines.push_back("mesh summaries not available in this build");
        for (const LegacyNote& note : body.mNotes)
        {
            lines.push_back(
                "note " + note.mCode + (note.mPart.empty() ? "" : " [" + note.mPart + "]") + ": " + note.mMessage);
        }
        for (const Body::Finding& finding : report.mFindings)
        {
            lines.push_back((finding.mSeverity == Body::Severity::Error ? "error " : "warning ") + finding.mCode + " "
                + finding.mPath + ": " + finding.mMessage);
        }
        lines.push_back(std::to_string(report.errorCount()) + " error(s), " + std::to_string(report.warningCount())
            + " warning(s), humanoidGait=" + (report.mHumanoidGait ? "true" : "false"));
        return lines;
    }
}
