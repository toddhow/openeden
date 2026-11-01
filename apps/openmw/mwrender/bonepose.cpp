#include "bonepose.hpp"

#include <components/animation/bones.hpp>
#include <components/animation/transform.hpp>
#include <components/misc/vsgmath.hpp>

namespace MWRender
{
    namespace
    {
        vsg::mat4 localMatrix(const Anim::Transform& transform)
        {
            return transform.t_transform(vsg::mat4());
        }
    }

    BonePose::BonePose(Anim::Bones& bones, const Anim::Transform* root)
        : mBones(bones)
        , mRoot(root)
    {
    }

    BonePose::Bone BonePose::find(const std::string& name) const
    {
        Bone result;
        Anim::Bone* bone = mBones.search(name);
        if (bone == nullptr || bone->path.empty())
            return result;
        result.mNode = bone->node();
        result.mPath.reserve(bone->path.size());
        for (const auto& node : bone->path)
            result.mPath.push_back(node.get());
        return result;
    }

    vsg::mat4 BonePose::getLocal(const Bone& bone) const
    {
        return bone ? localMatrix(*bone.mNode) : vsg::mat4();
    }

    void BonePose::setLocal(const Bone& bone, const vsg::mat4& local) const
    {
        if (!bone)
            return;
        vsg::vec3 translation;
        vsg::dquat rotation;
        vsg::vec3 scale;
        vsg::dquat scaleOrientation;
        Misc::VsgMath::decompose(local, translation, rotation, scale, scaleOrientation);
        bone.mNode->translation = translation;
        bone.mNode->scale = scale;
        bone.mNode->setAttitude(vsg::quat(rotation));
    }

    void BonePose::setLocalRotation(const Bone& bone, const vsg::dquat& rotation) const
    {
        if (bone)
            bone.mNode->setAttitude(vsg::quat(rotation));
    }

    vsg::mat4 BonePose::getModel(const Bone& bone) const
    {
        // A parent's local matrix comes first: model = local(root) * ... * local(bone).
        vsg::mat4 model;
        for (auto it = bone.mPath.rbegin(); it != bone.mPath.rend(); ++it)
            model = localMatrix(**it) * model;
        return model;
    }

    namespace
    {
        // The bone's rotation alone, without its scale or translation.
        vsg::mat4 rotationMatrixOf(const Anim::Transform& transform)
        {
            vsg::mat4 matrix;
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    matrix[i][j] = transform.rotation[i][j];
            return matrix;
        }
    }

    vsg::mat4 BonePose::getWorldRotation(const Bone& bone) const
    {
        vsg::mat4 rotation = mRoot ? rotationMatrixOf(*mRoot) : vsg::mat4();
        for (const Anim::Transform* node : bone.mPath)
            rotation = rotation * rotationMatrixOf(*node);
        return rotation;
    }

    vsg::mat4 BonePose::getParentWorldRotation(const Bone& bone) const
    {
        vsg::mat4 rotation = mRoot ? rotationMatrixOf(*mRoot) : vsg::mat4();
        for (std::size_t i = 0; i + 1 < bone.mPath.size(); ++i)
            rotation = rotation * rotationMatrixOf(*bone.mPath[i]);
        return rotation;
    }

    vsg::mat4 BonePose::getRoot() const
    {
        return mRoot ? localMatrix(*mRoot) : vsg::mat4();
    }

    vsg::mat4 BonePose::getWorld(const Bone& bone) const
    {
        return getRoot() * getModel(bone);
    }
}
