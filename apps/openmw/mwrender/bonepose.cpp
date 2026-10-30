#include "bonepose.hpp"

#include <cstring>

#include <components/animation/bones.hpp>
#include <components/animation/transform.hpp>

namespace MWRender
{
    namespace
    {
        // osg::Matrixf (row vectors, translation in the last row) and vsg::mat4 (columns, translation in the last
        // column) share their memory layout (components/vsgadapters/osgcompat.hpp, toOsg).
        osg::Matrixf toOsgMatrix(const vsg::mat4& matrix)
        {
            osg::Matrixf result;
            std::memcpy(result.ptr(), matrix.data(), sizeof(float) * 16);
            return result;
        }

        osg::Matrixf localMatrix(const Anim::Transform& transform)
        {
            return toOsgMatrix(transform.t_transform(vsg::mat4()));
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

    osg::Matrixf BonePose::getLocal(const Bone& bone) const
    {
        return bone ? localMatrix(*bone.mNode) : osg::Matrixf();
    }

    void BonePose::setLocal(const Bone& bone, const osg::Matrixf& local) const
    {
        if (!bone)
            return;
        osg::Vec3f translation;
        osg::Quat rotation;
        osg::Vec3f scale;
        osg::Quat scaleOrientation;
        local.decompose(translation, rotation, scale, scaleOrientation);
        bone.mNode->translation = vsg::vec3(translation.x(), translation.y(), translation.z());
        bone.mNode->scale = vsg::vec3(scale.x(), scale.y(), scale.z());
        bone.mNode->setAttitude(vsg::quat(rotation.x(), rotation.y(), rotation.z(), rotation.w()));
    }

    void BonePose::setLocalRotation(const Bone& bone, const osg::Quat& rotation) const
    {
        if (bone)
            bone.mNode->setAttitude(vsg::quat(rotation.x(), rotation.y(), rotation.z(), rotation.w()));
    }

    osg::Matrixf BonePose::getModel(const Bone& bone) const
    {
        // osg multiplies row vectors, so a child's local matrix comes first: model = local(bone) * ... * local(root).
        osg::Matrixf model;
        for (auto it = bone.mPath.rbegin(); it != bone.mPath.rend(); ++it)
            model = model * localMatrix(**it);
        return model;
    }

    osg::Matrixf BonePose::getRoot() const
    {
        return mRoot ? localMatrix(*mRoot) : osg::Matrixf();
    }

    osg::Matrixf BonePose::getWorld(const Bone& bone) const
    {
        return getModel(bone) * getRoot();
    }
}
