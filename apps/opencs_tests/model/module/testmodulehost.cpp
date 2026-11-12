// Task 0031, stage A: CSMModule::ModuleHost, written from the spec alone.
//
// Assumptions about names the spec leaves open (see the report):
//  - Header: apps/opencs/model/module/modulehost.hpp (ModuleHost, ModuleSource, ModuleInfo, coreModules()) and
//    apps/opencs/model/module/moduleregistry.hpp, namespace CSMModule.
//  - ModuleInfo members are mId, mOrigin, mState, mMessage, with ModuleInfo::State { Loaded, Failed, NotInstalled }.
//    ModuleSource::Origin is { Core, Extension }; ModuleSource members are mId, mLibrary, mOrigin (in that order).
//  - ModuleHost's constructor connects the registry's "a module's callback threw" path to the host, so that an
//    exception caught by ModuleRegistry (runCommand and the like, see testmoduleregistry.cpp) turns that module's
//    ModuleInfo into Failed.
//  - coreModules() returns a container of { id, library file name } with members mId and mLibrary (a
//    std::string_view or std::string; compared with == to a string literal).
//  - SAMPLE_EDITOR_MODULE_PATH is a compile definition: a string literal naming the built sample module library.
//  - Incompatible-version messages print versions as "major.minor" (openeden::sdk::operator<<).
#include "apps/opencs/model/module/modulehost.hpp"
#include "apps/opencs/model/module/moduleregistry.hpp"

#include <openeden/editor_module/module.hpp>
#include <openeden/editor_module/registrar.hpp>
#include <openeden/editor_module/types.hpp>
#include <openeden/version.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace CSMModule
{
    namespace
    {
        using namespace openeden::editor;
        using Origin = ModuleSource::Origin;

        class NullContext : public EditorContext
        {
        public:
            void pushCommand(std::unique_ptr<Command>) override {}
            void markDirty(std::string_view) override {}
            void log(LogLevel, std::string_view) override {}
            void requestRedraw() override {}
            AssetDocument* openAsset(const std::filesystem::path&) override { return nullptr; }
            AssetDocument* activeAsset() override { return nullptr; }
        };

        // ---- scripted static modules --------------------------------------------------------------------------
        // Function pointers cannot capture, so each slot N has its own create/destroy and a script in sSlots[N].
        constexpr int sSlotCount = 4;

        struct Slot
        {
            std::function<void(EditorRegistrar&)> registerUi;
            bool createThrows = false;
            bool createReturnsNull = false;
            bool shutdownThrows = false;
            int registerUiCalls = 0;
            int created = 0;
            int destroyed = 0;
        };

        Slot sSlots[sSlotCount];
        std::vector<std::string> sEvents;
        ModuleRegistry* sRegistry = nullptr;

        std::string slotName(int slot)
        {
            return "m" + std::to_string(slot);
        }

        template <int N>
        class ScriptedModule : public EditorModule
        {
        public:
            void registerUi(EditorRegistrar& registrar) override
            {
                ++sSlots[N].registerUiCalls;
                sEvents.push_back(slotName(N) + ".registerUi");
                if (sSlots[N].registerUi)
                    sSlots[N].registerUi(registrar);
            }

            void shutdown() override
            {
                // At this point the module's registrations must still be there.
                sEvents.push_back(slotName(N) + ".shutdown commands=" + std::to_string(sRegistry->commands().size()));
                if (sSlots[N].shutdownThrows)
                    throw std::runtime_error("shutdown boom");
            }
        };

        template <int N>
        EditorModule* createModule()
        {
            if (sSlots[N].createThrows)
                throw std::runtime_error("create boom");
            if (sSlots[N].createReturnsNull)
                return nullptr;
            ++sSlots[N].created;
            return new ScriptedModule<N>();
        }

        template <int N>
        void destroyModule(EditorModule* module)
        {
            ++sSlots[N].destroyed;
            sEvents.push_back(slotName(N) + ".destroy commands=" + std::to_string(sRegistry->commands().size()));
            delete module;
        }

        template <int N>
        EditorModuleDescriptor descriptor(openeden::sdk::Version version = kApiVersion)
        {
            // The id is slotName(N), which has static storage via a function-local string.
            static const std::string id = slotName(N);
            return EditorModuleDescriptor{ version, id.c_str(), &createModule<N>, &destroyModule<N> };
        }

        CommandDesc commandDesc(const std::string& id)
        {
            CommandDesc desc;
            desc.id = id;
            desc.label = id;
            desc.handler = [](EditorContext&) {};
            return desc;
        }

        class CSMModuleHostTest : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                for (Slot& slot : sSlots)
                    slot = Slot();
                sEvents.clear();
                sRegistry = &mRegistry;
            }

            void TearDown() override { sRegistry = nullptr; }

            /// The returned pointer stays valid for the whole test (snapshots are kept in a deque).
            const ModuleInfo* find(const ModuleHost& host, const std::string& id)
            {
                mSnapshots.push_back(host.modules());
                for (const ModuleInfo& info : mSnapshots.back())
                    if (info.mId == id)
                        return &info;
                return nullptr;
            }

            std::deque<std::vector<ModuleInfo>> mSnapshots;

            ModuleRegistry mRegistry;
            NullContext mContext;
        };

        int indexOf(const std::string& event)
        {
            for (std::size_t i = 0; i < sEvents.size(); ++i)
                if (sEvents[i] == event)
                    return static_cast<int>(i);
            return -1;
        }

        // ---- API version -----------------------------------------------------------------------------------

        TEST(CSMModuleApiVersionTest, isOneDotTwo)
        {
            EXPECT_EQ(kApiVersion, (openeden::sdk::Version{ 1, 4 }));
        }

        TEST(CSMModuleApiVersionTest, sameMajorAndHostMinorAtLeastTheModulesIsCompatible)
        {
            using openeden::sdk::isCompatible;
            using openeden::sdk::Version;
            EXPECT_TRUE(isCompatible(Version{ 1, 0 }, kApiVersion));
            EXPECT_FALSE(isCompatible(Version{ 1, static_cast<std::uint16_t>(kApiVersion.minor + 1) },
                kApiVersion)); // module wants a newer minor
            EXPECT_FALSE(isCompatible(Version{ 0, 0 }, kApiVersion)); // different major
            EXPECT_FALSE(isCompatible(Version{ static_cast<std::uint16_t>(kApiVersion.major + 1), 0 }, kApiVersion));
        }

        // ---- static modules --------------------------------------------------------------------------------

        TEST_F(CSMModuleHostTest, loadsStaticModulesInOrderAndCallsRegisterUiOncePerModule)
        {
            sSlots[0].registerUi = [](EditorRegistrar& r) { r.registerCommand(commandDesc("m0.command")); };
            sSlots[1].registerUi = [](EditorRegistrar& r) { r.registerCommand(commandDesc("m1.command")); };

            ModuleHost host(mRegistry, mContext);
            const EditorModuleDescriptor d0 = descriptor<0>();
            const EditorModuleDescriptor d1 = descriptor<1>();
            host.loadStatic("m0", d0, Origin::Core);
            host.loadStatic("m1", d1, Origin::Extension);

            EXPECT_EQ(sSlots[0].registerUiCalls, 1);
            EXPECT_EQ(sSlots[1].registerUiCalls, 1);
            EXPECT_LT(indexOf("m0.registerUi"), indexOf("m1.registerUi"));

            const auto modules = host.modules();
            ASSERT_EQ(modules.size(), 2u);
            EXPECT_EQ(modules[0].mId, "m0");
            EXPECT_EQ(modules[0].mOrigin, Origin::Core);
            EXPECT_EQ(modules[0].mState, ModuleInfo::State::Loaded);
            EXPECT_EQ(modules[1].mId, "m1");
            EXPECT_EQ(modules[1].mOrigin, Origin::Extension);
            EXPECT_EQ(modules[1].mState, ModuleInfo::State::Loaded);

            ASSERT_EQ(mRegistry.commands().size(), 2u);
            EXPECT_EQ(mRegistry.commands()[0].mModule, "m0");
            EXPECT_EQ(mRegistry.commands()[1].mModule, "m1");
            host.shutdown();
        }

        TEST_F(CSMModuleHostTest, shutdownRunsInReverseOrderWithRegistrationsWithdrawnBetweenShutdownAndDestroy)
        {
            sSlots[0].registerUi = [](EditorRegistrar& r) { r.registerCommand(commandDesc("m0.command")); };
            sSlots[1].registerUi = [](EditorRegistrar& r) { r.registerCommand(commandDesc("m1.command")); };

            {
                ModuleHost host(mRegistry, mContext);
                const EditorModuleDescriptor d0 = descriptor<0>();
                const EditorModuleDescriptor d1 = descriptor<1>();
                host.loadStatic("m0", d0, Origin::Core);
                host.loadStatic("m1", d1, Origin::Core);
                sEvents.clear();
                host.shutdown();
            }

            // Reverse load order; shutdown() sees its own registrations (both modules' commands before m1 goes,
            // only m0's after), destroy() runs after the registrations of that module were withdrawn.
            const std::vector<std::string> expected = {
                "m1.shutdown commands=2",
                "m1.destroy commands=1",
                "m0.shutdown commands=1",
                "m0.destroy commands=0",
            };
            EXPECT_EQ(sEvents, expected);
            EXPECT_EQ(sSlots[0].destroyed, 1);
            EXPECT_EQ(sSlots[1].destroyed, 1);
            EXPECT_TRUE(mRegistry.commands().empty());
        }

        TEST_F(CSMModuleHostTest, shutdownTwiceAndDestructionDoNotDestroyTwice)
        {
            {
                ModuleHost host(mRegistry, mContext);
                const EditorModuleDescriptor d0 = descriptor<0>();
                host.loadStatic("m0", d0, Origin::Core);
                host.shutdown();
                host.shutdown();
            }
            EXPECT_EQ(sSlots[0].created, 1);
            EXPECT_EQ(sSlots[0].destroyed, 1);
        }

        TEST_F(CSMModuleHostTest, destructorShutsDownLoadedModules)
        {
            {
                ModuleHost host(mRegistry, mContext);
                const EditorModuleDescriptor d0 = descriptor<0>();
                host.loadStatic("m0", d0, Origin::Core);
            }
            EXPECT_EQ(sSlots[0].destroyed, 1);
        }

        TEST_F(CSMModuleHostTest, aThrowingShutdownDoesNotStopTheOthersFromBeingDestroyed)
        {
            sSlots[1].shutdownThrows = true;
            {
                ModuleHost host(mRegistry, mContext);
                const EditorModuleDescriptor d0 = descriptor<0>();
                const EditorModuleDescriptor d1 = descriptor<1>();
                host.loadStatic("m0", d0, Origin::Core);
                host.loadStatic("m1", d1, Origin::Core);
                EXPECT_NO_THROW(host.shutdown());
            }
            EXPECT_EQ(sSlots[0].destroyed, 1);
            EXPECT_EQ(sSlots[1].destroyed, 1);
        }

        TEST_F(CSMModuleHostTest, aThrowingRegisterUiFailsThatModuleWithdrawsPartialRegistrationsAndTheNextStillLoads)
        {
            sSlots[0].registerUi = [](EditorRegistrar& r) {
                r.registerCommand(commandDesc("m0.partial"));
                throw std::runtime_error("ui boom");
            };
            sSlots[1].registerUi = [](EditorRegistrar& r) { r.registerCommand(commandDesc("m1.command")); };

            ModuleHost host(mRegistry, mContext);
            const EditorModuleDescriptor d0 = descriptor<0>();
            const EditorModuleDescriptor d1 = descriptor<1>();
            EXPECT_NO_THROW(host.loadStatic("m0", d0, Origin::Core));
            EXPECT_NO_THROW(host.loadStatic("m1", d1, Origin::Core));

            const ModuleInfo* bad = find(host, "m0");
            ASSERT_NE(bad, nullptr);
            EXPECT_EQ(bad->mState, ModuleInfo::State::Failed);
            EXPECT_NE(bad->mMessage.find("ui boom"), std::string::npos);

            const ModuleInfo* good = find(host, "m1");
            ASSERT_NE(good, nullptr);
            EXPECT_EQ(good->mState, ModuleInfo::State::Loaded);

            ASSERT_EQ(mRegistry.commands().size(), 1u);
            EXPECT_EQ(mRegistry.commands().front().mDesc.id, "m1.command");
            // The failed module's object was created, so it must also have been destroyed.
            EXPECT_EQ(sSlots[0].created, sSlots[0].destroyed);
            host.shutdown();
        }

        TEST_F(CSMModuleHostTest, anIncompatibleApiVersionFailsAndNamesBothVersions)
        {
            ModuleHost host(mRegistry, mContext);
            const EditorModuleDescriptor tooNewMajor = descriptor<0>(openeden::sdk::Version{ 2, 0 });
            const EditorModuleDescriptor tooNewMinor = descriptor<1>(
                openeden::sdk::Version{ kApiVersion.major, static_cast<std::uint16_t>(kApiVersion.minor + 1) });
            host.loadStatic("m0", tooNewMajor, Origin::Core);
            host.loadStatic("m1", tooNewMinor, Origin::Core);

            const ModuleInfo* major = find(host, "m0");
            ASSERT_NE(major, nullptr);
            EXPECT_EQ(major->mState, ModuleInfo::State::Failed);
            EXPECT_NE(major->mMessage.find("2.0"), std::string::npos) << major->mMessage;
            EXPECT_NE(major->mMessage.find("1.4"), std::string::npos) << major->mMessage;

            const ModuleInfo* minor = find(host, "m1");
            ASSERT_NE(minor, nullptr);
            EXPECT_EQ(minor->mState, ModuleInfo::State::Failed);
            EXPECT_FALSE(minor->mMessage.empty());

            EXPECT_EQ(sSlots[0].registerUiCalls, 0);
            EXPECT_EQ(sSlots[1].registerUiCalls, 0);
        }

        TEST_F(CSMModuleHostTest, anOlderMinorVersionLoads)
        {
            ModuleHost host(mRegistry, mContext);
            const EditorModuleDescriptor older = descriptor<0>(openeden::sdk::Version{ kApiVersion.major, 0 });
            host.loadStatic("m0", older, Origin::Core);
            const ModuleInfo* info = find(host, "m0");
            ASSERT_NE(info, nullptr);
            EXPECT_EQ(info->mState, ModuleInfo::State::Loaded);
            host.shutdown();
        }

        TEST_F(CSMModuleHostTest, anIdThatDiffersFromTheSourcesIdFails)
        {
            ModuleHost host(mRegistry, mContext);
            const EditorModuleDescriptor d0 = descriptor<0>(); // its own id is "m0"
            host.loadStatic("something-else", d0, Origin::Core);

            const ModuleInfo* info = find(host, "something-else");
            ASSERT_NE(info, nullptr);
            EXPECT_EQ(info->mState, ModuleInfo::State::Failed);
            EXPECT_NE(info->mMessage.find("m0"), std::string::npos) << info->mMessage;
            EXPECT_EQ(sSlots[0].registerUiCalls, 0);
        }

        TEST_F(CSMModuleHostTest, aNullCreateFails)
        {
            sSlots[0].createReturnsNull = true;
            ModuleHost host(mRegistry, mContext);
            const EditorModuleDescriptor d0 = descriptor<0>();
            host.loadStatic("m0", d0, Origin::Core);
            const ModuleInfo* info = find(host, "m0");
            ASSERT_NE(info, nullptr);
            EXPECT_EQ(info->mState, ModuleInfo::State::Failed);
            EXPECT_FALSE(info->mMessage.empty());
            EXPECT_EQ(sSlots[0].destroyed, 0); // nothing to destroy
        }

        TEST_F(CSMModuleHostTest, aThrowingCreateFailsAndTheNextStillLoads)
        {
            sSlots[0].createThrows = true;
            ModuleHost host(mRegistry, mContext);
            const EditorModuleDescriptor d0 = descriptor<0>();
            const EditorModuleDescriptor d1 = descriptor<1>();
            EXPECT_NO_THROW(host.loadStatic("m0", d0, Origin::Core));
            host.loadStatic("m1", d1, Origin::Core);

            const ModuleInfo* bad = find(host, "m0");
            ASSERT_NE(bad, nullptr);
            EXPECT_EQ(bad->mState, ModuleInfo::State::Failed);
            EXPECT_NE(bad->mMessage.find("create boom"), std::string::npos) << bad->mMessage;
            const ModuleInfo* good = find(host, "m1");
            ASSERT_NE(good, nullptr);
            EXPECT_EQ(good->mState, ModuleInfo::State::Loaded);
            host.shutdown();
        }

        TEST_F(CSMModuleHostTest, aNullDescriptorEntryPointFunctionsFail)
        {
            ModuleHost host(mRegistry, mContext);
            EditorModuleDescriptor broken = descriptor<0>();
            broken.create = nullptr;
            EXPECT_NO_THROW(host.loadStatic("m0", broken, Origin::Core));
            const ModuleInfo* info = find(host, "m0");
            ASSERT_NE(info, nullptr);
            EXPECT_EQ(info->mState, ModuleInfo::State::Failed);
        }

        // ---- callbacks that throw later, through the registry, disable only that module ---------------------

        TEST_F(CSMModuleHostTest, aThrowingCommandHandlerDisablesOnlyThatModule)
        {
            sSlots[0].registerUi = [](EditorRegistrar& r) {
                CommandDesc desc = commandDesc("m0.command");
                desc.handler = [](EditorContext&) { throw std::runtime_error("handler boom"); };
                r.registerCommand(desc);
            };
            sSlots[1].registerUi = [](EditorRegistrar& r) { r.registerCommand(commandDesc("m1.command")); };

            ModuleHost host(mRegistry, mContext);
            const EditorModuleDescriptor d0 = descriptor<0>();
            const EditorModuleDescriptor d1 = descriptor<1>();
            host.loadStatic("m0", d0, Origin::Core);
            host.loadStatic("m1", d1, Origin::Core);

            EXPECT_FALSE(mRegistry.runCommand("m0.command"));

            const ModuleInfo* bad = find(host, "m0");
            ASSERT_NE(bad, nullptr);
            EXPECT_EQ(bad->mState, ModuleInfo::State::Failed);
            EXPECT_NE(bad->mMessage.find("handler boom"), std::string::npos) << bad->mMessage;
            const ModuleInfo* good = find(host, "m1");
            ASSERT_NE(good, nullptr);
            EXPECT_EQ(good->mState, ModuleInfo::State::Loaded);

            ASSERT_EQ(mRegistry.commands().size(), 1u);
            EXPECT_EQ(mRegistry.commands().front().mDesc.id, "m1.command");
            EXPECT_TRUE(mRegistry.runCommand("m1.command"));
            host.shutdown();
            EXPECT_EQ(sSlots[0].destroyed, sSlots[0].created);
            EXPECT_EQ(sSlots[1].destroyed, sSlots[1].created);
        }

        // ---- core list ----------------------------------------------------------------------------------------

        TEST(CSMModuleCoreListTest, holdsCreativeMotionEditor)
        {
            const auto core = coreModules();
            bool found = false;
            for (const auto& entry : core)
            {
                if (entry.mId == "creativemotion_editor")
                {
                    found = true;
                    const std::string library(entry.mLibrary);
                    EXPECT_NE(library.find("creativemotion_editor"), std::string::npos) << library;
                }
            }
            EXPECT_TRUE(found);
        }

        // ---- dynamic libraries -------------------------------------------------------------------------------

        class CSMModuleHostDynamicTest : public CSMModuleHostTest
        {
        protected:
            void SetUp() override
            {
                CSMModuleHostTest::SetUp();
                const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
                mDir = std::filesystem::temp_directory_path() / (std::string("eden-studio-mod-") + info->name());
                std::filesystem::remove_all(mDir);
                std::filesystem::create_directories(mDir);
            }

            void TearDown() override
            {
                std::filesystem::remove_all(mDir);
                CSMModuleHostTest::TearDown();
            }

            std::filesystem::path mDir;
        };

        TEST_F(CSMModuleHostDynamicTest, sampleModuleLoadsRegistersAndShutsDownCleanly)
        {
            const std::filesystem::path library = SAMPLE_EDITOR_MODULE_PATH;
            ASSERT_TRUE(std::filesystem::exists(library)) << library;

            {
                ModuleHost host(mRegistry, mContext);
                host.load({ ModuleSource{ "sample", library, Origin::Extension } });

                const auto modules = host.modules();
                ASSERT_EQ(modules.size(), 1u);
                EXPECT_EQ(modules[0].mId, "sample");
                EXPECT_EQ(modules[0].mState, ModuleInfo::State::Loaded) << modules[0].mMessage;

                EXPECT_EQ(mRegistry.workspaces().size(), 1u);
                EXPECT_EQ(mRegistry.panels().size(), 1u);
                EXPECT_EQ(mRegistry.commands().size(), 2u);
                EXPECT_EQ(mRegistry.gizmoProviders().size(), 1u);
                EXPECT_EQ(mRegistry.assetTypes().size(), 1u);
                EXPECT_EQ(mRegistry.sections().size(), 1u);
                EXPECT_EQ(mRegistry.trackProviders().size(), 1u);
                EXPECT_NE(mRegistry.findAssetType("a.note.json"), nullptr);

                host.shutdown();
                EXPECT_TRUE(mRegistry.commands().empty());
                EXPECT_TRUE(mRegistry.assetTypes().empty());
            }
            // Destroying the host after shutdown() must also be safe (the library is already unloaded).
            SUCCEED();
        }

        TEST_F(CSMModuleHostDynamicTest, sampleModuleLoadedTwiceInTheSameProcessDoesNotCrash)
        {
            const std::filesystem::path library = SAMPLE_EDITOR_MODULE_PATH;
            for (int round = 0; round < 2; ++round)
            {
                ModuleRegistry registry;
                ModuleHost host(registry, mContext);
                host.load({ ModuleSource{ "sample", library, Origin::Core } });
                ASSERT_EQ(host.modules().size(), 1u);
                EXPECT_EQ(host.modules()[0].mState, ModuleInfo::State::Loaded);
                host.shutdown();
            }
        }

        TEST_F(CSMModuleHostDynamicTest, aMissingCoreLibraryIsNotInstalled)
        {
            ModuleHost host(mRegistry, mContext);
            host.load({ ModuleSource{ "ghost", mDir / "no-such-library.dll", Origin::Core } });
            const auto modules = host.modules();
            ASSERT_EQ(modules.size(), 1u);
            EXPECT_EQ(modules[0].mState, ModuleInfo::State::NotInstalled);
        }

        TEST_F(CSMModuleHostDynamicTest, aMissingExtensionLibraryFailsWithAReason)
        {
            ModuleHost host(mRegistry, mContext);
            host.load({ ModuleSource{ "ghost", mDir / "no-such-library.dll", Origin::Extension } });
            const auto modules = host.modules();
            ASSERT_EQ(modules.size(), 1u);
            EXPECT_EQ(modules[0].mState, ModuleInfo::State::Failed);
            EXPECT_FALSE(modules[0].mMessage.empty());
        }

        TEST_F(CSMModuleHostDynamicTest, aFileThatIsNotALibraryFailsWithoutACrash)
        {
            const std::filesystem::path fake = mDir / "not-a-library.dll";
            std::ofstream(fake) << "this is plain text, not a shared library";

            ModuleHost host(mRegistry, mContext);
            EXPECT_NO_THROW(host.load({ ModuleSource{ "fake", fake, Origin::Core } }));
            const auto modules = host.modules();
            ASSERT_EQ(modules.size(), 1u);
            EXPECT_EQ(modules[0].mState, ModuleInfo::State::Failed);
            EXPECT_FALSE(modules[0].mMessage.empty());
        }

        TEST_F(CSMModuleHostDynamicTest, aLibraryWithoutTheEntryPointFailsWithoutACrash)
        {
#if defined(_WIN32)
            const std::filesystem::path system = "kernel32.dll";
#else
            const std::filesystem::path system = "libc.so.6";
#endif
            ModuleHost host(mRegistry, mContext);
            EXPECT_NO_THROW(host.load({ ModuleSource{ "system", system, Origin::Extension } }));
            const auto modules = host.modules();
            ASSERT_EQ(modules.size(), 1u);
            // If the platform cannot open the bare name from here the module is Failed as well, never Loaded.
            EXPECT_EQ(modules[0].mState, ModuleInfo::State::Failed);
            EXPECT_FALSE(modules[0].mMessage.empty());
        }

        TEST_F(CSMModuleHostDynamicTest, aBrokenModuleDoesNotStopTheNextOneFromLoading)
        {
            const std::filesystem::path fake = mDir / "not-a-library.dll";
            std::ofstream(fake) << "garbage";

            ModuleHost host(mRegistry, mContext);
            host.load({ ModuleSource{ "fake", fake, Origin::Extension },
                ModuleSource{ "sample", std::filesystem::path(SAMPLE_EDITOR_MODULE_PATH), Origin::Extension } });
            const auto modules = host.modules();
            ASSERT_EQ(modules.size(), 2u);
            EXPECT_EQ(modules[0].mState, ModuleInfo::State::Failed);
            EXPECT_EQ(modules[1].mState, ModuleInfo::State::Loaded) << modules[1].mMessage;
            host.shutdown();
        }
    }
}
