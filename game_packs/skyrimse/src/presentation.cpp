#include "game_skyrimse/presentation.hpp"

#include <functional>
#include <span>

// See the header and docs/tasks/0215-skyrim-session-presentation.md.

namespace game_skyrimse::presentation
{
    namespace
    {
        using openeden::sdk::BindingResult;
        using openeden::sdk::BindingType;
        using openeden::sdk::BindingValue;

        class Fn final : public openeden::sdk::BindingHandler
        {
        public:
            explicit Fn(bool answer)
                : mAnswer(answer)
            {
            }
            BindingResult call(std::span<const BindingValue>) override
            {
                BindingResult r;
                r.value.type = BindingType::Bool;
                r.value.boolean = mAnswer;
                return r;
            }

        private:
            bool mAnswer;
        };
    }

    struct PresentationService::Impl
    {
        std::vector<std::unique_ptr<Fn>> mHandlers;
        std::vector<openeden::sdk::FunctionDescriptor> mFunctions;
    };

    PresentationService::PresentationService(Flags flags)
        : mImpl(std::make_unique<Impl>())
        , mFlags(flags)
    {
    }

    PresentationService::~PresentationService() = default;

    openeden::sdk::DeclareResult PresentationService::declare(openeden::sdk::BindingsService& service)
    {
        if (!mFlags.declared())
            return openeden::sdk::DeclareResult{ true, {} };
        Impl& m = *mImpl;
        const auto add = [&m](std::string_view name, bool answer) {
            m.mHandlers.push_back(std::make_unique<Fn>(answer));
            openeden::sdk::FunctionDescriptor f;
            f.name = name;
            f.hasResult = true;
            f.result = BindingType::Bool;
            f.access = openeden::sdk::BindingAccess::Read;
            f.handler = m.mHandlers.back().get();
            m.mFunctions.push_back(f);
        };
        add("menus", mFlags.mMenus);
        add("music", mFlags.mMusic);
        add("owns_music", mFlags.ownsMusic());

        openeden::sdk::NamespaceDescriptor ns;
        ns.name = kNamespace;
        ns.version = { 1, 0 };
        ns.functions = m.mFunctions;
        ns.menuSafe = true;
        return service.declareNamespace(ns);
    }
}
