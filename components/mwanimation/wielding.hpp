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
        // OpenEden (task 0074b): what an equipment change empties (MWRender::Npc::clearControllers,
        // MWRender::WieldingCreature::removeParts): the lists whose controllers all belong to what the change links
        // again. Never the object's own auto-play groups (autoPlay: the inventory lights of MWRender::addItemLight, a
        // creature's own model): nothing adds those again, and their clocks must keep running.
        void clearPartControllers()
        {
            mWeaponControllers.clear();
            mStaticControllers.clear();
        }
        Anim::Update mWeaponControllers;
        Anim::Update mStaticControllers;
    };
}

#endif
