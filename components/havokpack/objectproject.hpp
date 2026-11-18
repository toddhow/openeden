#ifndef OPENEDEN_COMPONENTS_HAVOKPACK_OBJECTPROJECT_H
#define OPENEDEN_COMPONENTS_HAVOKPACK_OBJECTPROJECT_H

// Task 0289 (dungeon mechanics): the animation project of a placed Skyrim object (a puzzle pillar, a gate, a lever,
// a keyhole ring). A model such as meshes/clutter/ruins/pillar/ruinspuzzlepillar01.nif names its animations through
// meshes/animationdata/<model name>.txt: "1", a count N, N file names, "0". The first three names are the behaviour
// graph, the character and the skeleton, the others are the clips (VERIFIED on the pillar, lever, keyhole, ring and
// portcullis projects of the installed game: 6, 4, 5, 7 and 5 entries; the clip files sit under the model's folder).
//
// The behaviour graph (an hkbBehaviorGraph state machine) is NOT read. The game's scripts start a clip by an event
// name (Trigger01, Open, StartState02 ...); this library says which clip each name stands for from the clip's own
// file name. That mapping is inferred, not read from the graph (see docs/tasks/0289-skyrim-dungeon-mechanics.md).
//
// Pure; standard library only. Formats layer.

#include <string>
#include <string_view>
#include <vector>

namespace HavokPack
{
    inline constexpr std::size_t kMaxProjectEntries = 64;

    struct ObjectProject
    {
        std::string mBehavior; ///< lower case, relative to the model's folder, forward slashes
        std::string mCharacter;
        std::string mSkeleton;
        std::vector<std::string> mClips;
    };

    /// False (and `out` unspecified) for text that is not a project: fewer than 3 entries, a count that is not a
    /// number, more than kMaxProjectEntries, fewer lines than the count says, or a name that goes up a folder.
    bool parseObjectProject(std::string_view text, ObjectProject& out);

    /// "meshes/animationdata/<model stem>.txt" (lower case) for a model path; empty when it is not a .nif.
    std::string objectProjectPath(std::string_view modelPath);
    /// "meshes/clutter/ruins/pillar/" (lower case, forward slashes, trailing slash) for a model path; empty with no
    /// folder.
    std::string objectFolder(std::string_view modelPath);
    /// "ruinspillartrigger01" for "Animations\\RuinsPillarTrigger01.hkx".
    std::string clipStem(std::string_view path);

    /// The lower-case event names a script may call a clip by: its own stem; "trigger0N" when the stem ends in the
    /// digits N (a pillar's or ring's N-th turn, 1 to 99); "open" when it ends in "down" (a ring or keyhole sinking
    /// into the door).
    std::vector<std::string> eventNamesOfClip(std::string_view stem);

    /// "trigger02" for "StartState02": the event that names the clip whose FIRST pose the object takes at once. Empty
    /// for any other name.
    std::string startStateClipEvent(std::string_view event);
}

#endif
