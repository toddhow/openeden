// Task 0091, stage b: modules subscribe to the event bus through the registrar. Written from the spec alone.
//
// Rules pinned: a subscription made in registerUi belongs to the module and is delivered in load order; one made
// outside registerUi, or with an empty handler, is refused with a reason; a handler that throws withdraws its whole
// module (state Failed, message names the handler) and nothing else; a module's subscriptions go with the module.
#include "apps/opencs/model/module/modulehost.hpp"
#include "apps/opencs/model/module/moduleregistry.hpp"

#include <openeden/editor_module/module.hpp>
#include <openeden/editor_module/registrar.hpp>
#include <openeden/editor_module/types.hpp>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace openeden::editor;

    class Context final : public EditorContext
    {
    public:
        void pushCommand(std::unique_ptr<Command>) override {}
        void markDirty(std::string_view) override {}
        void log(LogLevel, std::string_view) override {}
        void requestRedraw() override {}
        AssetDocument* openAsset(const std::filesystem::path&) override { return nullptr; }
        AssetDocument* activeAsset() override { return nullptr; }
    };

    std::vector<std::string> sLog;
    EditorRegistrar* sSavedRegistrar = nullptr;

    template <int N>
    struct Behaviour
    {
        static inline bool sThrow = false;
        static inline bool sRegisterCommand = true;
        static inline bool sRefusedEmpty = false;
    };

    template <int N>
    class SubscribingModule final : public EditorModule
    {
    public:
        void registerUi(EditorRegistrar& registrar) override
        {
            sSavedRegistrar = &registrar;
            CommandDesc command;
            command.id = "m" + std::to_string(N) + ".command";
            command.label = "c";
            command.handler = [](EditorContext&) {};
            registrar.registerCommand(command);

            const Registration empty = registrar.subscribe(EventHandler());
            Behaviour<N>::sRefusedEmpty = !empty.ok && !empty.error.empty();

            const Registration registration = registrar.subscribe([](const EditorEvent& event) {
                sLog.push_back("m" + std::to_string(N) + ":" + event.subject);
                if (Behaviour<N>::sThrow)
                    throw std::runtime_error("handler boom");
            });
            EXPECT_TRUE(registration.ok) << registration.error;
        }
        void shutdown() override {}
    };

    template <int N>
    EditorModule* create()
    {
        return new SubscribingModule<N>();
    }

    void destroy(EditorModule* module)
    {
        delete module;
    }

    template <int N>
    EditorModuleDescriptor descriptor()
    {
        static const std::string id = "m" + std::to_string(N);
        return EditorModuleDescriptor{ kApiVersion, id.c_str(), &create<N>, &destroy };
    }

    EditorEvent opened(const std::string& subject)
    {
        EditorEvent event;
        event.type = EditorEventType::AssetOpened;
        event.subject = subject;
        return event;
    }

    class ModuleEventsTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            sLog.clear();
            Behaviour<0>::sThrow = false;
            Behaviour<1>::sThrow = false;
        }

        CSMModule::ModuleRegistry mRegistry;
        Context mContext;
    };

    TEST_F(ModuleEventsTest, subscriptionsMadeInRegisterUiAreDeliveredInLoadOrder)
    {
        CSMModule::ModuleHost host(mRegistry, mContext);
        const EditorModuleDescriptor d0 = descriptor<0>();
        const EditorModuleDescriptor d1 = descriptor<1>();
        host.loadStatic("m0", d0, CSMModule::ModuleSource::Origin::Core);
        host.loadStatic("m1", d1, CSMModule::ModuleSource::Origin::Core);

        mRegistry.bus().publish(opened("a"));
        EXPECT_EQ(sLog, (std::vector<std::string>{ "m0:a", "m1:a" }));
        EXPECT_TRUE(Behaviour<0>::sRefusedEmpty);
    }

    TEST_F(ModuleEventsTest, aSubscriptionOutsideRegisterUiIsRefusedWithAReason)
    {
        CSMModule::ModuleHost host(mRegistry, mContext);
        const EditorModuleDescriptor d0 = descriptor<0>();
        host.loadStatic("m0", d0, CSMModule::ModuleSource::Origin::Core);
        ASSERT_NE(sSavedRegistrar, nullptr);
        const Registration late = sSavedRegistrar->subscribe([](const EditorEvent&) {});
        EXPECT_FALSE(late.ok);
        EXPECT_FALSE(late.error.empty());
        EXPECT_EQ(mRegistry.bus().subscriberCount(), 1u);
    }

    TEST_F(ModuleEventsTest, aHandlerThatThrowsFailsItsModuleAloneAndTheOtherStillGetsTheEvent)
    {
        CSMModule::ModuleHost host(mRegistry, mContext);
        const EditorModuleDescriptor d0 = descriptor<0>();
        const EditorModuleDescriptor d1 = descriptor<1>();
        host.loadStatic("m0", d0, CSMModule::ModuleSource::Origin::Core);
        host.loadStatic("m1", d1, CSMModule::ModuleSource::Origin::Core);
        Behaviour<0>::sThrow = true;

        mRegistry.bus().publish(opened("a"));
        mRegistry.bus().publish(opened("b"));

        EXPECT_EQ(sLog, (std::vector<std::string>{ "m0:a", "m1:a", "m1:b" }));
        const auto modules = host.modules();
        ASSERT_EQ(modules.size(), 2u);
        EXPECT_EQ(modules[0].mState, CSMModule::ModuleInfo::State::Failed);
        EXPECT_NE(modules[0].mMessage.find("handler boom"), std::string::npos) << modules[0].mMessage;
        EXPECT_EQ(modules[1].mState, CSMModule::ModuleInfo::State::Loaded);
        // The failed module's other registrations are withdrawn with it.
        ASSERT_EQ(mRegistry.commands().size(), 1u);
        EXPECT_EQ(mRegistry.commands().front().mDesc.id, "m1.command");
    }

    TEST_F(ModuleEventsTest, shuttingAModuleDownRemovesItsSubscriptions)
    {
        {
            CSMModule::ModuleHost host(mRegistry, mContext);
            const EditorModuleDescriptor d0 = descriptor<0>();
            host.loadStatic("m0", d0, CSMModule::ModuleSource::Origin::Core);
            EXPECT_EQ(mRegistry.bus().subscriberCount(), 1u);
        }
        EXPECT_EQ(mRegistry.bus().subscriberCount(), 0u);
        mRegistry.bus().publish(opened("a"));
        EXPECT_TRUE(sLog.empty());
    }
}
