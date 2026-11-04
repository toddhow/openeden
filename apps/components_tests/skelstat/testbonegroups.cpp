// Task 0070 (stage a), synthetic check 2: how the animation system assigns a bone group (lower body, torso, left arm,
// right arm: the four blend masks of MWAnim) to a bone whose name it does not know.
//
// The registration is Anim::CopyBones (components/animation/bones.cpp), used by MWAnim::Bones(node, MWAnim::groups) as
// apps/openmw/mwrender/npc.cpp builds an NPC's skeleton. MWAnim::groups (components/mwanimation/groups.hpp) names three
// bones: "bip01 spine1" (torso), "bip01 l clavicle" (left arm), "bip01 r clavicle" (right arm). A bone whose normalised
// (lower-cased) name is in that table starts its group; every other bone takes the group of its nearest ancestor that
// started one, and a bone with none above it is group 0 (lower body). Custom bones added by Bones::addCustomBones take
// the group of the bone they are added under (CopyBones::startUnder). These tests pin that, on synthetic skeletons.
#include <components/animation/bones.hpp>
#include <components/animation/transform.hpp>
#include <components/mwanimation/bones.hpp>
#include <components/mwanimation/groups.hpp>
#include <components/vsgutil/name.hpp>

#include <gtest/gtest.h>

#include <map>
#include <string>

namespace
{
    class Skeleton
    {
    public:
        Skeleton(const std::string& rootName)
            : mRoot(make(rootName))
        {
        }

        Anim::Transform& add(const std::string& parent, const std::string& name)
        {
            vsg::ref_ptr<Anim::Transform> node = make(name);
            mNodes.at(parent)->addChild(node);
            return *node;
        }

        const Anim::Transform& root() const { return *mRoot; }

    private:
        vsg::ref_ptr<Anim::Transform> make(const std::string& name)
        {
            auto node = Anim::Transform::create();
            vsgUtil::setName(*node, name);
            mNodes[name] = node;
            return node;
        }

        std::map<std::string, vsg::ref_ptr<Anim::Transform>> mNodes;
        vsg::ref_ptr<Anim::Transform> mRoot;
    };

    std::uint32_t groupOf(Anim::Bones& bones, const std::string& name)
    {
        const Anim::Bone* bone = bones.search(name);
        EXPECT_NE(bone, nullptr) << name;
        return bone != nullptr ? bone->group : 999u;
    }

    // A Morrowind-named skeleton with invented extra bones in every region.
    Skeleton morrowindNamed()
    {
        Skeleton s("Bip01");
        s.add("Bip01", "Bip01 Pelvis");
        s.add("Bip01", "Xtra At Root");
        s.add("Bip01 Pelvis", "Bip01 Spine");
        s.add("Bip01 Pelvis", "Xtra Under Pelvis");
        s.add("Bip01 Spine", "Bip01 Spine1");
        s.add("Bip01 Spine1", "Xtra Breast L");
        s.add("Bip01 Spine1", "Bip01 Neck");
        s.add("Bip01 Neck", "Xtra Under Neck");
        s.add("Bip01 Spine1", "Bip01 L Clavicle");
        s.add("Bip01 L Clavicle", "Bip01 L UpperArm");
        s.add("Bip01 L UpperArm", "Xtra L Twist");
        s.add("Bip01 Spine1", "Bip01 R Clavicle");
        s.add("Bip01 R Clavicle", "Xtra R Shoulder Pad");
        return s;
    }

    TEST(BoneGroups, a_bone_with_an_unknown_name_takes_the_group_of_its_nearest_grouped_ancestor)
    {
        const Skeleton s = morrowindNamed();
        Anim::Bones bones = MWAnim::Bones(s.root(), MWAnim::groups);
        EXPECT_EQ(groupOf(bones, "Bip01"), MWAnim::BoneGroup_LowerBody);
        EXPECT_EQ(groupOf(bones, "Xtra At Root"), MWAnim::BoneGroup_LowerBody);
        EXPECT_EQ(groupOf(bones, "Bip01 Pelvis"), MWAnim::BoneGroup_LowerBody);
        EXPECT_EQ(groupOf(bones, "Xtra Under Pelvis"), MWAnim::BoneGroup_LowerBody);
        EXPECT_EQ(groupOf(bones, "Bip01 Spine"), MWAnim::BoneGroup_LowerBody);
        EXPECT_EQ(groupOf(bones, "Bip01 Spine1"), MWAnim::BoneGroup_Torso);
        EXPECT_EQ(groupOf(bones, "Xtra Breast L"), MWAnim::BoneGroup_Torso);
        EXPECT_EQ(groupOf(bones, "Xtra Under Neck"), MWAnim::BoneGroup_Torso);
        EXPECT_EQ(groupOf(bones, "Bip01 L Clavicle"), MWAnim::BoneGroup_LeftArm);
        EXPECT_EQ(groupOf(bones, "Xtra L Twist"), MWAnim::BoneGroup_LeftArm);
        EXPECT_EQ(groupOf(bones, "Bip01 R Clavicle"), MWAnim::BoneGroup_RightArm);
        EXPECT_EQ(groupOf(bones, "Xtra R Shoulder Pad"), MWAnim::BoneGroup_RightArm);
    }

    TEST(BoneGroups, the_group_table_matches_the_lower_cased_name)
    {
        Skeleton s("BIP01");
        s.add("BIP01", "BIP01 SPINE1");
        s.add("BIP01 SPINE1", "Xtra Under Spine1");
        Anim::Bones bones = MWAnim::Bones(s.root(), MWAnim::groups);
        EXPECT_EQ(groupOf(bones, "Xtra Under Spine1"), MWAnim::BoneGroup_Torso);
    }

    // A skeleton named in another scheme starts no group at all: every bone is lower body, the arms included.
    TEST(BoneGroups, a_skeleton_with_other_names_puts_every_bone_in_the_lower_body)
    {
        Skeleton s("NPC Root [Root]");
        s.add("NPC Root [Root]", "NPC Spine1 [Spn1]");
        s.add("NPC Spine1 [Spn1]", "NPC L Clavicle [LClv]");
        s.add("NPC L Clavicle [LClv]", "NPC L UpperArm [LUar]");
        Anim::Bones bones = MWAnim::Bones(s.root(), MWAnim::groups);
        EXPECT_EQ(groupOf(bones, "NPC Spine1 [Spn1]"), MWAnim::BoneGroup_LowerBody);
        EXPECT_EQ(groupOf(bones, "NPC L Clavicle [LClv]"), MWAnim::BoneGroup_LowerBody);
        EXPECT_EQ(groupOf(bones, "NPC L UpperArm [LUar]"), MWAnim::BoneGroup_LowerBody);
    }

    // The way stage e would add virtual bones: custom bones under an existing bone take that bone's group, and so do
    // their children.
    TEST(BoneGroups, a_custom_bone_takes_the_group_of_the_bone_it_is_added_under)
    {
        const Skeleton s = morrowindNamed();
        Anim::Bones bones = MWAnim::Bones(s.root(), MWAnim::groups);

        Skeleton extra("Bip01 L Clavicle");
        Anim::Transform& custom = extra.add("Bip01 L Clavicle", "Virtual Bone");
        custom.setValue(Anim::sCustomBoneKey, true);
        extra.add("Virtual Bone", "Virtual Child");
        Anim::Transform& underTorso = extra.add("Bip01 L Clavicle", "Bip01 Spine1 Copy");
        (void)underTorso;
        bones.addCustomBones(extra.root());

        EXPECT_EQ(groupOf(bones, "Virtual Bone"), MWAnim::BoneGroup_LeftArm);
        EXPECT_EQ(groupOf(bones, "Virtual Child"), MWAnim::BoneGroup_LeftArm);
        // A plain (not custom) node of the other graph is not added.
        EXPECT_EQ(bones.search("Bip01 Spine1 Copy"), nullptr);
    }

    TEST(BoneGroups, a_custom_bone_named_like_a_group_bone_does_not_start_a_group)
    {
        const Skeleton s = morrowindNamed();
        Anim::Bones bones = MWAnim::Bones(s.root(), MWAnim::groups);

        Skeleton extra("Bip01 Pelvis");
        Anim::Transform& custom = extra.add("Bip01 Pelvis", "Bip01 L Clavicle Spare");
        custom.setValue(Anim::sCustomBoneKey, true);
        extra.add("Bip01 L Clavicle Spare", "bip01 r clavicle");
        bones.addCustomBones(extra.root());
        // The child is named like a group bone, but addCustomBones copies with no group table: the existing bone of
        // that name stays, and nothing new is added under it.
        EXPECT_EQ(groupOf(bones, "Bip01 L Clavicle Spare"), MWAnim::BoneGroup_LowerBody);
        EXPECT_EQ(groupOf(bones, "Bip01 R Clavicle"), MWAnim::BoneGroup_RightArm);
    }
}
