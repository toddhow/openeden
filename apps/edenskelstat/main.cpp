// edenskelstat (task 0070, stage a): the read-only measurement of the extended skeletons of body replacers, run on the
// user's own installed files.
//
//   edenskelstat --manifest <file> --profile <humanoid-to-eden.skeletonprofile.json> --out <folder> [--detail]
//
// The manifest (Body::parseSkelStatManifest) names the files: the Morrowind base skeleton, the skeletons to measure and
// the body meshes that use each. The files are opened for reading only; nothing is written beside them. The output
// folder gets report.txt (aggregates: labels, canonical role names, heuristic ids, token stems and numbers; it names no
// bone and no path) and, with --detail, detail.txt (every bone by name: for the work folder only; refused inside a git
// work tree). Same files, same bytes.
//
// Units: lengths in the files are converted to metres with Constants::UnitsPerMeter (Morrowind's 64 units per yard,
// components/misc/constants.hpp). That Skyrim and Fallout 4 files use the same unit is an assumption; the skeleton
// heights in the report let a reader check it.
//
// Reading: Nif::Reader::setDefaultReadOptions with VersionSet::Any, as niftest does, so that files other than
// Morrowind's 4.0.0.2 are parsed (components/nif/niffile.cpp, Reader::parse refuses other versions otherwise).

#include <components/body/skeletonstats.hpp>
#include <components/bodynif/skeletonread.hpp>
#include <components/files/constrainedfilestream.hpp>
#include <components/files/conversion.hpp>
#include <components/misc/constants.hpp>
#include <components/nif/niffile.hpp>
#include <components/nif/node.hpp>
#include <components/vfs/pathutil.hpp>

#include <opas/anim/SkeletonProfile.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
    /// A manifest is a short list of lines; anything larger is a mistake.
    constexpr std::uintmax_t sMaxManifestBytes = 1u << 20;

    struct Loaded
    {
        std::unique_ptr<Nif::NIFFile> mFile;
        const Nif::NiAVObject* mRoot = nullptr;
        std::string mError; // names the file: stderr only, never the report
    };

    std::string versionText(std::uint32_t version)
    {
        return std::to_string((version >> 24) & 0xFF) + "." + std::to_string((version >> 16) & 0xFF) + "."
            + std::to_string((version >> 8) & 0xFF) + "." + std::to_string(version & 0xFF);
    }

    Loaded load(const std::string& path)
    {
        Loaded loaded;
        try
        {
            const std::filesystem::path file = Files::pathFromUnicodeString(path);
            loaded.mFile = std::make_unique<Nif::NIFFile>(VFS::Path::Normalized(path));
            Nif::Reader reader(*loaded.mFile, nullptr);
            reader.parse(Files::openConstrainedFileStream(file));
            for (const Nif::Record* root : loaded.mFile->mRoots)
            {
                if (const auto* object = dynamic_cast<const Nif::NiAVObject*>(root))
                {
                    loaded.mRoot = object;
                    break;
                }
            }
            if (loaded.mRoot == nullptr)
            {
                loaded.mError = path + ": no root object";
            }
        }
        catch (const std::exception& e)
        {
            loaded.mError = path + ": " + e.what();
        }
        return loaded;
    }

    std::vector<Body::RestBone> toRestBones(const BodyNif::SkeletonRead& read)
    {
        std::vector<Body::RestBone> bones;
        bones.reserve(read.mNodes.size());
        const double unitsPerMetre = static_cast<double>(Constants::UnitsPerMeter);
        for (const BodyNif::SkeletonNode& node : read.mNodes)
        {
            Body::RestBone bone;
            bone.mName = node.mName;
            bone.mParent = node.mParent;
            bone.mTranslation = { node.mTranslation[0] / unitsPerMetre, node.mTranslation[1] / unitsPerMetre,
                node.mTranslation[2] / unitsPerMetre };
            bone.mRotation = node.mRotation;
            bones.push_back(std::move(bone));
        }
        return bones;
    }

    void addFileFacts(std::vector<std::pair<std::string, std::string>>& facts, const Nif::NIFFile& file)
    {
        facts.emplace_back("nif version", versionText(file.mVersion));
        facts.emplace_back("user version", std::to_string(file.mUserVersion));
        facts.emplace_back("stream version", std::to_string(file.mBethVersion));
        facts.emplace_back("records", std::to_string(file.mRecords.size()));
    }

    std::string fixed(double value, int decimals)
    {
        std::ostringstream text;
        text.imbue(std::locale::classic());
        text.setf(std::ios::fixed);
        text.precision(decimals);
        text << value;
        return text.str();
    }

    std::string readText(const std::filesystem::path& path, std::string& error)
    {
        std::error_code code;
        const std::uintmax_t size = std::filesystem::file_size(path, code);
        if (code || size > sMaxManifestBytes)
        {
            error = code ? code.message() : "larger than 1 MiB";
            return {};
        }
        std::ifstream stream(path, std::ios::binary);
        std::ostringstream text;
        text << stream.rdbuf();
        if (!stream && !stream.eof())
        {
            error = "cannot be read";
        }
        return text.str();
    }

    bool insideGitWorkTree(const std::filesystem::path& folder)
    {
        std::error_code code;
        std::filesystem::path current = std::filesystem::weakly_canonical(folder, code);
        if (code)
        {
            current = std::filesystem::absolute(folder);
        }
        while (true)
        {
            if (std::filesystem::exists(current / ".git", code))
            {
                return true;
            }
            const std::filesystem::path parent = current.parent_path();
            if (parent == current || parent.empty())
            {
                return false;
            }
            current = parent;
        }
    }

    bool writeFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream << text;
        return static_cast<bool>(stream);
    }

    int usage()
    {
        std::cerr
            << "usage: edenskelstat --manifest <file> --profile <skeletonprofile.json> --out <folder> [--detail]\n";
        return 1;
    }
}

int main(int argc, char** argv)
{
    std::string manifestPath;
    std::string profilePath;
    std::string outPath;
    bool detail = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--detail")
        {
            detail = true;
        }
        else if ((arg == "--manifest" || arg == "--profile" || arg == "--out") && i + 1 < argc)
        {
            std::string& target = arg == "--manifest" ? manifestPath : (arg == "--profile" ? profilePath : outPath);
            target = argv[++i];
        }
        else
        {
            return usage();
        }
    }
    if (manifestPath.empty() || profilePath.empty() || outPath.empty())
    {
        return usage();
    }
    const std::filesystem::path outFolder = Files::pathFromUnicodeString(outPath);
    if (detail && insideGitWorkTree(outFolder))
    {
        std::cerr << "edenskelstat: --detail names the bones of the files read; it is refused inside a git work tree\n";
        return 1;
    }

    std::string error;
    const std::string manifestText = readText(Files::pathFromUnicodeString(manifestPath), error);
    if (!error.empty())
    {
        std::cerr << "edenskelstat: the manifest " << error << "\n";
        return 2;
    }
    const Body::SkelStatManifestResult parsed = Body::parseSkelStatManifest(manifestText);
    if (!parsed.mValue)
    {
        std::cerr << "edenskelstat: manifest " << parsed.mError << "\n";
        return 2;
    }
    const Body::SkelStatManifest& manifest = *parsed.mValue;
    const opas::anim::SkeletonProfileLoadResult profile
        = opas::anim::loadSkeletonProfileFile(Files::pathFromUnicodeString(profilePath));
    if (!profile.value)
    {
        std::cerr << "edenskelstat: the profile could not be read: " << profile.error.message << "\n";
        return 2;
    }
    std::vector<Body::RoleBinding> baseRoles;
    for (const opas::anim::BoneMapping& mapping : profile.value->bones)
    {
        baseRoles.push_back({ mapping.source, mapping.target, {} });
    }

    Nif::ReadOptions readOptions;
    readOptions.mAllowedVersions = Nif::VersionSet::Any;
    Nif::Reader::setDefaultReadOptions(readOptions);
    bool failures = false;

    std::map<std::string, std::vector<Body::RestBone>> baseSkeletons;
    for (const Body::SkelStatManifest::Base& base : manifest.mBases)
    {
        const Loaded loaded = load(base.mPath);
        if (!loaded.mError.empty())
        {
            std::cerr << "edenskelstat: base " << base.mLabel << ": " << loaded.mError << "\n";
            failures = true;
            baseSkeletons[base.mLabel] = {};
            continue;
        }
        baseSkeletons[base.mLabel] = toRestBones(BodyNif::readSkeleton(*loaded.mRoot));
    }

    std::vector<Body::SkeletonStats> stats;
    std::map<std::string, std::vector<Body::RestBone>> skeletons;
    std::map<std::string, std::vector<Body::RoleBinding>> roleTables;
    for (const Body::SkelStatManifest::Skeleton& entry : manifest.mSkeletons)
    {
        Body::SkeletonStatInput input;
        input.mLabel = entry.mLabel;
        input.mBaseLabel = entry.mBase;
        input.mBase = baseSkeletons[entry.mBase];
        input.mBaseRoles = baseRoles;
        const Loaded loaded = load(entry.mPath);
        if (!loaded.mError.empty())
        {
            std::cerr << "edenskelstat: skeleton " << entry.mLabel << ": " << loaded.mError << "\n";
            failures = true;
            input.mFacts.emplace_back("read", "failed");
            stats.push_back(Body::computeSkeletonStats(input));
            continue;
        }
        const BodyNif::SkeletonRead read = BodyNif::readSkeleton(*loaded.mRoot);
        addFileFacts(input.mFacts, *loaded.mFile);
        input.mFacts.emplace_back("nodes", std::to_string(read.mNodes.size()));
        input.mFacts.emplace_back("geometry", std::to_string(read.mGeometry));
        input.mFacts.emplace_back("other objects", std::to_string(read.mOtherObjects));
        input.mFacts.emplace_back("unnamed", std::to_string(read.mUnnamed));
        input.mFacts.emplace_back("scaled", std::to_string(read.mScaled));
        input.mFacts.emplace_back("non-orthonormal", std::to_string(read.mNonOrthonormal));
        input.mFacts.emplace_back("revisits", std::to_string(read.mRevisits));
        input.mFacts.emplace_back("reader errors", std::to_string(read.mErrors.size()));
        for (const std::string& message : read.mErrors)
        {
            std::cerr << "edenskelstat: skeleton " << entry.mLabel << ": " << message << "\n";
        }
        input.mSkeleton = toRestBones(read);
        for (const BodyNif::SkeletonNode& node : read.mNodes)
        {
            input.mUntrusted.push_back(!node.mOrthonormal || std::abs(node.mScale - 1.0) > 1e-6);
        }
        for (const Body::SkelStatManifest::Role& role : manifest.mRoles)
        {
            if (role.mSkeleton == entry.mLabel)
            {
                input.mRoles.push_back({ role.mRole, role.mBone, {} });
            }
        }
        skeletons[entry.mLabel] = input.mSkeleton;
        roleTables[entry.mLabel] = input.mRoles;

        for (const Body::SkelStatManifest::BodySet& set : manifest.mBodySets)
        {
            if (set.mSkeleton != entry.mLabel)
            {
                continue;
            }
            Body::BodySetInput body;
            body.mLabel = set.mLabel;
            std::size_t filesFailed = 0;
            std::size_t shapes = 0;
            std::size_t unskinned = 0;
            std::size_t vertices = 0;
            std::size_t weighted = 0;
            std::size_t unweighted = 0;
            std::size_t badSums = 0;
            std::size_t outOfRange = 0;
            std::size_t nonFinite = 0;
            std::size_t readerErrors = 0;
            std::size_t derivedCounts = 0;
            double partitionAgreement = -1;
            double globalAgreement = -1;
            double localAgreement = -1;
            std::map<std::string, std::size_t> kinds;
            std::map<std::string, std::size_t> routes;
            std::set<std::string> versions;
            for (const std::string& path : set.mPaths)
            {
                const Loaded mesh = load(path);
                if (!mesh.mError.empty())
                {
                    std::cerr << "edenskelstat: body set " << set.mLabel << ": " << mesh.mError << "\n";
                    ++filesFailed;
                    failures = true;
                    continue;
                }
                versions.insert(versionText(mesh.mFile->mVersion) + "/" + std::to_string(mesh.mFile->mUserVersion) + "/"
                    + std::to_string(mesh.mFile->mBethVersion));
                const BodyNif::SkinWeightsRead weights = BodyNif::readSkinWeights(*mesh.mRoot);
                unskinned += weights.mUnskinnedShapes;
                readerErrors += weights.mErrors.size();
                for (const std::string& message : weights.mErrors)
                {
                    std::cerr << "edenskelstat: body set " << set.mLabel << ": " << message << "\n";
                }
                for (const BodyNif::ShapeWeights& shape : weights.mShapes)
                {
                    ++shapes;
                    ++kinds[shape.mKind];
                    std::string route = BodyNif::toString(shape.mRoute);
                    if (shape.mRoute == BodyNif::WeightRoute::VertexData)
                    {
                        route += shape.mGlobalIndices ? "(global)" : "(local)";
                    }
                    ++routes[route];
                    vertices += shape.mVertices;
                    if (!shape.mVertexCountFromFile)
                    {
                        ++derivedCounts;
                    }
                    weighted += shape.mWeightedVertices;
                    unweighted += shape.mUnweightedVertices;
                    badSums += shape.mBadSums;
                    outOfRange += shape.mOutOfRange;
                    nonFinite += shape.mNonFinite;
                    partitionAgreement = std::max(partitionAgreement, shape.mPartitionAgreement);
                    globalAgreement = std::max(globalAgreement, shape.mVertexDataGlobalAgreement);
                    localAgreement = std::max(localAgreement, shape.mVertexDataLocalAgreement);
                    for (std::size_t b = 0; b < shape.mBones.size() && b < shape.mMass.size(); ++b)
                    {
                        body.mMasses.push_back({ shape.mBones[b], shape.mMass[b] });
                    }
                }
            }
            const auto agreement = [](double value) { return value < 0 ? std::string("-") : fixed(value, 6); };
            std::string versionList;
            for (const std::string& v : versions)
            {
                versionList += (versionList.empty() ? "" : ", ") + v;
            }
            body.mFacts.emplace_back("files", std::to_string(set.mPaths.size()));
            body.mFacts.emplace_back("failed", std::to_string(filesFailed));
            body.mFacts.emplace_back("version/user/stream", versionList.empty() ? "-" : versionList);
            body.mFacts.emplace_back("skinned shapes", std::to_string(shapes));
            for (const auto& [kind, n] : kinds)
            {
                body.mFacts.emplace_back(kind, std::to_string(n));
            }
            for (const auto& [route, n] : routes)
            {
                body.mFacts.emplace_back("route " + route, std::to_string(n));
            }
            body.mFacts.emplace_back("unskinned shapes", std::to_string(unskinned));
            body.mFacts.emplace_back("vertices", std::to_string(vertices));
            body.mFacts.emplace_back(
                "shapes whose vertex count came from the partition", std::to_string(derivedCounts));
            body.mFacts.emplace_back("weighted", std::to_string(weighted));
            body.mFacts.emplace_back("unweighted", std::to_string(unweighted));
            body.mFacts.emplace_back("weight sums off by more than 1e-2", std::to_string(badSums));
            body.mFacts.emplace_back("indices out of range", std::to_string(outOfRange));
            body.mFacts.emplace_back("weights not finite or negative", std::to_string(nonFinite));
            body.mFacts.emplace_back("largest share difference to partitions", agreement(partitionAgreement));
            body.mFacts.emplace_back("to packed data with global indices", agreement(globalAgreement));
            body.mFacts.emplace_back("to packed data with local indices", agreement(localAgreement));
            body.mFacts.emplace_back("reader errors", std::to_string(readerErrors));
            input.mBodySets.push_back(std::move(body));
        }
        stats.push_back(Body::computeSkeletonStats(input));
    }

    std::vector<Body::RestPairStats> pairs;
    for (const auto& [first, second] : manifest.mCompares)
    {
        pairs.push_back(Body::compareSkeletonRests(
            first, skeletons[first], second, skeletons[second], roleTables[first], roleTables[second]));
    }

    std::error_code code;
    std::filesystem::create_directories(outFolder, code);
    if (!writeFile(outFolder / "report.txt", Body::writeSkeletonStatsReport(stats, pairs)))
    {
        std::cerr << "edenskelstat: report.txt could not be written\n";
        return 4;
    }
    if (detail && !writeFile(outFolder / "detail.txt", Body::writeSkeletonStatsDetail(stats)))
    {
        std::cerr << "edenskelstat: detail.txt could not be written\n";
        return 4;
    }
    std::cout << "edenskelstat: " << stats.size() << " skeletons, " << pairs.size() << " pairs"
              << (failures ? ", some files could not be read (see above)" : "") << "\n";
    return failures ? 3 : 0;
}
