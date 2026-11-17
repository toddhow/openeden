#include <components/havokpack/animation.hpp>
#include <components/havokpack/idle.hpp>
#include <components/havokpack/packfile.hpp>
#include <components/havokpack/skeleton.hpp>

#include <gtest/gtest.h>

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

// Task 0267: a survey of the paired (two actor) animations of Skyrim SE: what a paired clip file holds. Opt-in:
// OPENEDEN_PAIRED_ROOT names a folder with the extracted files (it contains meshes/...). Prints PAIRED lines (the
// numbers in docs/tasks/0267-skyrim-paired-animations.md section 1); asserts only that the files read.
namespace
{
    using namespace HavokPack;

    std::vector<std::uint8_t> readFile(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    std::string lower(std::string s)
    {
        for (char& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // The names the animation's annotation tracks give its tracks: an array at +40, 24 bytes each, name pointer first.
    std::vector<std::string> annotationNames(const Packfile& pack)
    {
        std::vector<std::string> names;
        const Object* o = pack.objectsOfClass("hkaSplineCompressedAnimation").at(0);
        const ArrayRef tracks = pack.arrayAt(Ref{ o->mSection, o->mOffset + 40 });
        for (std::uint32_t i = 0; tracks.mData && i < tracks.mSize && i < 4096; ++i)
        {
            const auto name = pack.pointerAt(Ref{ tracks.mData->mSection, tracks.mData->mOffset + 24 * i });
            names.push_back(name ? std::string(pack.cstring(*name)) : std::string("<null>"));
        }
        return names;
    }

    void print(const char* what, float time, const Transform& t)
    {
        std::cout << "PAIRED   t " << time << " " << what << " pos " << t.mTranslation[0] << " " << t.mTranslation[1]
                  << " " << t.mTranslation[2] << " rot " << t.mRotation[0] << " " << t.mRotation[1] << " "
                  << t.mRotation[2] << " " << t.mRotation[3] << "\n";
    }
}

TEST(SkyrimPairedSurvey, what_each_paired_file_holds)
{
    const char* root = std::getenv("OPENEDEN_PAIRED_ROOT");
    if (root == nullptr)
        GTEST_SKIP() << "OPENEDEN_PAIRED_ROOT is not set";
    const char* files[] = {
        "meshes/actors/sharedkillmoves/human&horse/paired_mount.hkx",
        "meshes/actors/sharedkillmoves/human&horse/paired_dismount.hkx",
        "meshes/auxbones/tail/animations/horse_paired_mount.hkx",
        "meshes/auxbones/tail/animations/horse_paired_dismount.hkx",
        "meshes/actors/character/_1stperson/animations/paired_mount.hkx",
        "meshes/actors/character/animations/horse_rider/idle.hkx",
        "meshes/actors/horse/animations/idle.hkx",
    };
    for (const char* f : files)
    {
        const auto bytes = readFile(std::string(root) + "/" + f);
        ASSERT_FALSE(bytes.empty()) << f;
        const Packfile pack = Packfile::parse(bytes);
        std::cout << "PAIRED file " << f << " bytes " << bytes.size() << " objects " << pack.objects().size() << "\n";
        const std::vector<std::string> names = annotationNames(pack);
        std::cout << "PAIRED   annotation tracks " << names.size();
        for (std::size_t i = 0; i < names.size() && i < 4; ++i)
            std::cout << " [" << names[i] << "]";
        std::cout << "\n";
        for (const Clip& c : readClips(pack))
        {
            std::cout << "PAIRED   clip skeleton '" << c.mSkeletonName << "' tracks " << c.mNumTransformTracks
                      << " floats " << c.mNumFloatTracks << " duration " << c.mDuration << " frames " << c.mNumFrames
                      << " blocks " << c.mBlocks.size() << " named " << c.mTrackNames.size() << "\n";
        }
    }
}

// Which skeleton owns each annotation-named track: "2_" names the second actor (the horse).
TEST(SkyrimPairedSurvey, the_track_names_split_between_the_human_and_the_horse_skeleton)
{
    const char* root = std::getenv("OPENEDEN_PAIRED_ROOT");
    if (root == nullptr)
        GTEST_SKIP() << "OPENEDEN_PAIRED_ROOT is not set";
    auto parse = [&](const std::string& rel) { return Packfile::parse(readFile(std::string(root) + "/" + rel)); };
    const Skeleton humanSkel = readSkeletons(parse("meshes/actors/character/character assets/skeleton.hkx")).at(0);
    const Skeleton horseSkel = readSkeletons(parse("meshes/actors/horse/character assets/skeleton.hkx")).at(0);
    std::map<std::string, std::size_t> humanBones, horseBones;
    for (std::size_t i = 0; i < humanSkel.mBoneNames.size(); ++i)
        humanBones[lower(humanSkel.mBoneNames[i])] = i;
    for (std::size_t i = 0; i < horseSkel.mBoneNames.size(); ++i)
        horseBones[lower(horseSkel.mBoneNames[i])] = i;
    std::cout << "PAIRED horse bones:";
    for (const std::string& n : horseSkel.mBoneNames)
        std::cout << " [" << n << "]";
    std::cout << "\n";
    for (const char* f : { "meshes/actors/sharedkillmoves/human&horse/paired_mount.hkx",
             "meshes/actors/sharedkillmoves/human&horse/paired_dismount.hkx",
             "meshes/actors/character/_1stperson/animations/paired_mount.hkx" })
    {
        std::size_t second = 0, secondFound = 0, first = 0, firstFound = 0, shared = 0;
        std::string missing;
        for (const std::string& name : annotationNames(parse(f)))
        {
            const std::string n = lower(name);
            if (n == "pairedroot")
            {
                ++shared;
            }
            else if (n.rfind("2_", 0) == 0)
            {
                ++second;
                if (horseBones.count(n.substr(2)) != 0)
                    ++secondFound;
                else
                    missing += " [" + n + "]";
            }
            else
            {
                ++first;
                if (humanBones.count(n) != 0)
                    ++firstFound;
                else
                    missing += " [" + n + "]";
            }
        }
        std::cout << "PAIRED " << f << ": root " << shared << ", second actor " << second << " (" << secondFound
                  << " on the horse skeleton), first actor " << first << " (" << firstFound
                  << " on the human skeleton); not found:" << missing.substr(0, 300) << "\n";
    }
}

// The root tracks of the mount and dismount clips at the start, the middle and the end.
TEST(SkyrimPairedSurvey, the_root_tracks_of_the_mount_clips)
{
    const char* root = std::getenv("OPENEDEN_PAIRED_ROOT");
    if (root == nullptr)
        GTEST_SKIP() << "OPENEDEN_PAIRED_ROOT is not set";
    for (const char* f : { "meshes/actors/sharedkillmoves/human&horse/paired_mount.hkx",
             "meshes/actors/sharedkillmoves/human&horse/paired_dismount.hkx" })
    {
        const Packfile pack = Packfile::parse(readFile(std::string(root) + "/" + f));
        const Clip clip = readClips(pack).at(0);
        const std::vector<std::string> names = annotationNames(pack);
        std::cout << "PAIRED " << f << " order:";
        for (std::size_t i = 0; i < names.size(); ++i)
            if (i < 6 || (i >= 52 && i < 62) || i + 3 >= names.size())
                std::cout << " " << i << "=" << names[i];
        std::cout << "\n";
        std::vector<Transform> out(clip.mNumTransformTracks);
        const char* wanted[] = { "PairedRoot", "NPC", "NPC Root [Root]", "2_", "2_Horse", "SaddleBone",
            "NPC COM [COM ]", "2_Horse_COM" };
        for (float at : { 0.f, clip.mDuration * 0.5f, clip.mDuration })
        {
            sample(clip, at, out);
            for (const char* w : wanted)
                for (std::size_t i = 0; i < names.size(); ++i)
                    if (names[i] == w)
                        print(w, at, out[i]);
        }
    }
}

// The riding idle clip's root and centre of mass, to compare with the end of the mount clip.
TEST(SkyrimPairedSurvey, the_rider_idle_root)
{
    const char* root = std::getenv("OPENEDEN_PAIRED_ROOT");
    if (root == nullptr)
        GTEST_SKIP() << "OPENEDEN_PAIRED_ROOT is not set";
    auto parse = [&](const std::string& rel) { return Packfile::parse(readFile(std::string(root) + "/" + rel)); };
    const Skeleton skel = readSkeletons(parse("meshes/actors/character/character assets/skeleton.hkx")).at(0);
    Clip clip = readClips(parse("meshes/actors/character/animations/horse_rider/idle.hkx")).at(0);
    const BoundClip bound = bindClip(skel, std::move(clip));
    std::vector<Transform> out(bound.mClip.mNumTransformTracks);
    sample(bound.mClip, 0.f, out);
    for (std::size_t i = 0; i < bound.mTrackBones.size(); ++i)
    {
        const std::string& n = bound.mTrackBones[i];
        if (n == "npc root [root]" || n == "npc com [com ]" || n.rfind("npc pelvis", 0) == 0)
            print(n.c_str(), 0.f, out[i]);
    }
    const Skeleton horse = readSkeletons(parse("meshes/actors/horse/character assets/skeleton.hkx")).at(0);
    for (std::size_t i = 0; i < horse.mBoneNames.size(); ++i)
        if (i < 3 || horse.mBoneNames[i] == "SaddleBone")
            print(horse.mBoneNames[i].c_str(), 0.f, horse.mReferencePose[i]);
}
