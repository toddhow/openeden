#ifndef VSGOPENMW_MWANIMATION_WIELDING_H
#define VSGOPENMW_MWANIMATION_WIELDING_H

#include "actor.hpp"

namespace vsg
{
    class Switch;
    class Transform;
}
namespace MWAnim
{
    /*
     * Optionally carries weapons.
     */
    class Wielding : public Actor
    {
    public:
        Wielding(const Context& ctx);
        ~Wielding();

        virtual void updateEquipment() {}

        float weaponAnimationTime = 0.f;

        enum class Wield
        {
            Weapon = 0,
            CarriedLeft = 1,
            Ammo = 2
        };
        virtual void show(Wield w, bool visible);
        bool isShown(Wield w) const;
        void update(float dt) override;
        std::vector<const Anim::Transform*> getWieldWorldTransform(Wield w) const;

        /// openeden-vsg: whether a shield is on the actor's back while nothing is drawn ([Game] shield sheathing).
        virtual bool hasSheathedShield() const { return false; }
        /// openeden-vsg: whether the shield moves between hand and back in step with the current weapon.
        virtual bool canAnimateShield() const { return false; }

    protected:
        vsg::Transform* mAttachAmmo{};
        std::array<vsg::ref_ptr<vsg::Switch>, 3> mSwitches;
        void addSwitch(vsg::ref_ptr<vsg::Node>& node, Wield w);
        Anim::Update mWeaponControllers;
        Anim::Update mStaticControllers;
    };
}

#endif
