#ifndef OPENEDEN_MWRENDER_BONEPOSE_H
#define OPENEDEN_MWRENDER_BONEPOSE_H

#include <string>
#include <vector>

#include <components/misc/vsgmath.hpp>

namespace Anim
{
    class Bones;
    class Transform;
}

namespace MWRender
{
    /**
     * @brief The engine-side bone interface of OpenEden's post-animation features (task 0011): foot IK, look-at,
     * smoother turns, compound hitbox sampling, the inertial lean and the CreativeMotion pose driver read and write
     * bones only through it, never through renderer nodes, so a later renderer change touches this file only.
     *
     * Bones are vsgopenmw's Anim::Transform nodes, found by name in an actor's Anim::Bones (MWAnim::Play::bones).
     * "Model" space is the actor model's space (the product of the bone path, without the actor's own position);
     * "world" space also applies the actor's root transform.
     *
     * Ownership: BonePose holds no ownership; the Bones and the root transform must outlive it (they are owned by the
     * actor's MWAnim::Object). Handles store the bone path found once at setup, so per-frame reads and writes do not
     * allocate. Used from the main thread, after MWAnim::Play::runAnimation and MWAnim::Actor::manualAnimation and
     * before the scene update runs skinning (CharacterController::runAnimation, apps/openmw/mwmechanics/character.cpp).
     */
    class BonePose
    {
    public:
        struct Bone
        {
            Anim::Transform* mNode = nullptr;
            std::vector<const Anim::Transform*> mPath; // from the model root to the bone, inclusive

            explicit operator bool() const { return mNode != nullptr; }
        };

        /// @param root the actor's root transform (MWAnim::Object::transform()); may be null, then world == model.
        BonePose(Anim::Bones& bones, const Anim::Transform* root);

        /// Finds a bone by name (case as the Bones' normalisation allows). Returns an empty handle if missing.
        Bone find(const std::string& name) const;

        /// The bone's local transform (relative to its parent bone).
        vsg::mat4 getLocal(const Bone& bone) const;
        /// Replaces the bone's local transform; the matrix is decomposed into translation, rotation and scale.
        void setLocal(const Bone& bone, const vsg::mat4& local) const;
        /// Replaces only the bone's local rotation.
        void setLocalRotation(const Bone& bone, const vsg::dquat& rotation) const;
        /// The bone's local rotation alone (no scale, no translation) as a rotation matrix.
        vsg::mat4 getLocalRotation(const Bone& bone) const;
        /// Replaces the bone's local rotation block with a rotation matrix; translation and scale stay.
        void setLocalRotationMatrix(const Bone& bone, const vsg::mat4& rotation) const;
        vsg::vec3 getLocalTranslation(const Bone& bone) const;
        /// Replaces only the bone's local translation; rotation and scale stay.
        void setLocalTranslation(const Bone& bone, const vsg::vec3& translation) const;

        /// The bone's transform in the actor model's space.
        vsg::mat4 getModel(const Bone& bone) const;
        /// The bone's transform in world space.
        vsg::mat4 getWorld(const Bone& bone) const;
        /// The bone's rotation in world space with every scale left out: the root's rotation times the bones' own
        /// rotations. Scaled bones (race weight) make the full world matrix sheared once rotated; use this to compute
        /// rotations to write back with setLocalRotation.
        vsg::mat4 getWorldRotation(const Bone& bone) const;
        /// The same for the bone's parent (the actor root's rotation for a model's first bone).
        vsg::mat4 getParentWorldRotation(const Bone& bone) const;
        /// The actor's root transform in world space (identity without a root).
        vsg::mat4 getRoot() const;

    private:
        Anim::Bones& mBones;
        const Anim::Transform* mRoot;
    };
}

#endif
