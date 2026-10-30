#include "switches.hpp"

#include <string>

#include <vsg/core/ConstVisitor.h>
#include <vsg/core/Visitor.h>

#include <components/animation/clone.hpp>

namespace MWRender
{
    namespace
    {
        bool hasSwitchName(const vsg::Node& node, std::string_view name)
        {
            std::string value;
            return node.getValue("switch", value) && value == name;
        }

        class FindSwitch : public vsg::ConstVisitor
        {
        public:
            explicit FindSwitch(std::string_view name)
                : mName(name)
            {
            }

            const vsg::Switch* mFound = nullptr;

            void apply(const vsg::Node& node) override
            {
                if (!mFound)
                    node.traverse(*this);
            }

            void apply(const vsg::Switch& node) override
            {
                if (mFound)
                    return;
                if (hasSwitchName(node, mName))
                {
                    mFound = &node;
                    return;
                }
                // Every child, also those switched off (vsg::Switch::traverse skips them), as OpenMW's visitor did.
                for (const auto& child : node.children)
                {
                    child.node->accept(*this);
                    if (mFound)
                        return;
                }
            }

        private:
            std::string_view mName;
        };
    }

    const vsg::Switch* findSwitch(const vsg::Node& node, std::string_view name)
    {
        FindSwitch visitor(name);
        node.accept(visitor);
        return visitor.mFound;
    }

    vsg::Switch* findSwitch(vsg::Node& node, std::string_view name)
    {
        return const_cast<vsg::Switch*>(findSwitch(static_cast<const vsg::Node&>(node), name));
    }

    std::vector<vsg::Switch*> findSwitches(vsg::Node& node, std::string_view name)
    {
        // Every switch with that name, also inside matching switches and switched-off children, as OpenMW's
        // TRAVERSE_ALL_CHILDREN visitors did.
        struct Collect : vsg::Visitor
        {
            std::string_view mName;
            std::vector<vsg::Switch*> mFound;
            void apply(vsg::Node& n) override { n.traverse(*this); }
            void apply(vsg::Switch& n) override
            {
                if (hasSwitchName(n, mName))
                    mFound.push_back(&n);
                for (auto& child : n.children)
                    child.node->accept(*this);
            }
        } visitor;
        visitor.mName = name;
        node.accept(visitor);
        return std::move(visitor.mFound);
    }

    void setSingleChildOn(vsg::Switch& node, std::size_t index)
    {
        for (std::size_t i = 0; i < node.children.size(); ++i)
            node.children[i].mask = (i == index) ? vsg::MASK_ALL : vsg::MASK_OFF;
    }

    void makeSwitchesUnique(vsg::Group& group, std::string_view name)
    {
        const Anim::CloneCallback copySwitch = [name](const vsg::Node& node) -> vsg::ref_ptr<vsg::Node> {
            if (const auto* found = dynamic_cast<const vsg::Switch*>(&node); found && hasSwitchName(node, name))
            {
                auto copy = vsg::Switch::create(*found);
                copy->setValue("switch", std::string(name));
                return copy;
            }
            return {};
        };
        for (auto& child : group.children)
        {
            if (findSwitch(static_cast<const vsg::Node&>(*child), name) != nullptr)
                Anim::cloneIfRequired(child, copySwitch);
        }
    }
}
