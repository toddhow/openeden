#include <components/plugin/dependency_resolver.hpp>
#include <components/plugin/manifest.hpp>
#include <openeden/version.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    using openeden::plugin::ContractRequirement;
    using openeden::plugin::PluginManifest;
    using openeden::plugin::ResolutionError;
    using openeden::plugin::ResolutionResult;
    using openeden::plugin::resolveDependencies;
    using openeden::sdk::Version;

    PluginManifest makeManifest(std::string id, std::vector<ContractRequirement> providedContracts = {},
        std::vector<ContractRequirement> requiredContracts = {},
        std::vector<ContractRequirement> optionalContracts = {}, std::vector<std::string> loadAfter = {},
        Version sdkVersion = openeden::sdk::kSdkVersion)
    {
        PluginManifest m;
        m.id = std::move(id);
        m.name = m.id;
        m.pluginVersion = "1.0.0";
        m.sdkVersion = sdkVersion;
        m.toolchain = "test-toolchain";
        m.providedContracts = std::move(providedContracts);
        m.requiredContracts = std::move(requiredContracts);
        m.optionalContracts = std::move(optionalContracts);
        m.loadAfter = std::move(loadAfter);
        return m;
    }

    int indexOf(const std::vector<std::string>& order, const std::string& id)
    {
        const auto it = std::find(order.begin(), order.end(), id);
        if (it == order.end())
            return -1;
        return static_cast<int>(std::distance(order.begin(), it));
    }

    bool isBefore(const std::vector<std::string>& order, const std::string& first, const std::string& second)
    {
        const int idxFirst = indexOf(order, first);
        const int idxSecond = indexOf(order, second);
        return idxFirst != -1 && idxSecond != -1 && idxFirst < idxSecond;
    }

    bool contains(const std::vector<std::string>& list, const std::string& item)
    {
        return std::find(list.begin(), list.end(), item) != list.end();
    }

    bool errorMentions(const std::vector<ResolutionError>& errors, const std::string& target)
    {
        for (const auto& err : errors)
        {
            if (err.pluginId.find(target) != std::string::npos || err.message.find(target) != std::string::npos)
                return true;
        }
        return false;
    }

    TEST(DependencyResolverTest, BasicOrdering_RequiredContractProviderLoadsFirst)
    {
        // Plugin B requires contract from Plugin A -> A before B in load order
        const PluginManifest pluginA = makeManifest("pluginA", { { "openeden.test", { 1, 0 } } });
        const PluginManifest pluginB = makeManifest("pluginB", {}, { { "openeden.test", { 1, 0 } } });

        // Provide input in reverse order to ensure resolver is sorting by dependencies
        const std::vector<PluginManifest> manifests{ pluginB, pluginA };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        EXPECT_TRUE(result.success);
        EXPECT_EQ(result.loadOrder.size(), 2u);
        EXPECT_TRUE(isBefore(result.loadOrder, "pluginA", "pluginB"));
        EXPECT_TRUE(result.disabledPlugins.empty());
        EXPECT_TRUE(result.errors.empty());
    }

    TEST(DependencyResolverTest, TransitiveChain_LoadsInDependencyOrder)
    {
        // C requires B, B requires A -> load order A, B, C
        const PluginManifest pluginA = makeManifest("pluginA", { { "contractA", { 1, 0 } } });
        const PluginManifest pluginB
            = makeManifest("pluginB", { { "contractB", { 1, 0 } } }, { { "contractA", { 1, 0 } } });
        const PluginManifest pluginC = makeManifest("pluginC", {}, { { "contractB", { 1, 0 } } });

        const std::vector<PluginManifest> manifests{ pluginC, pluginA, pluginB };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        EXPECT_TRUE(result.success);
        const std::vector<std::string> expectedOrder{ "pluginA", "pluginB", "pluginC" };
        EXPECT_EQ(result.loadOrder, expectedOrder);
        EXPECT_TRUE(result.disabledPlugins.empty());
    }

    TEST(DependencyResolverTest, MissingRequiredContract_DisablesPluginAndCascadesToDependents)
    {
        // Plugin B requires missing contract; Plugin C requires B; Plugin D is independent
        const PluginManifest pluginB
            = makeManifest("pluginB", { { "contractB", { 1, 0 } } }, { { "openeden.missing", { 1, 0 } } });
        const PluginManifest pluginC = makeManifest("pluginC", {}, { { "contractB", { 1, 0 } } });
        const PluginManifest pluginD = makeManifest("pluginD");

        const std::vector<PluginManifest> manifests{ pluginB, pluginC, pluginD };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        EXPECT_FALSE(result.success);
        // B and C should be disabled; D should succeed
        EXPECT_TRUE(contains(result.disabledPlugins, "pluginB"));
        EXPECT_TRUE(contains(result.disabledPlugins, "pluginC"));
        EXPECT_FALSE(contains(result.loadOrder, "pluginB"));
        EXPECT_FALSE(contains(result.loadOrder, "pluginC"));

        EXPECT_TRUE(contains(result.loadOrder, "pluginD"));

        // Error message must name the missing contract
        EXPECT_TRUE(errorMentions(result.errors, "openeden.missing"));
    }

    TEST(DependencyResolverTest, VersionIncompatibility_MajorMismatchFailsResolution)
    {
        // Provider has major 2, consumer requires major 1 -> incompatibility
        const PluginManifest pluginA = makeManifest("pluginA", { { "openeden.test", { 2, 0 } } });
        const PluginManifest pluginB = makeManifest("pluginB", {}, { { "openeden.test", { 1, 0 } } });

        const std::vector<PluginManifest> manifests{ pluginA, pluginB };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        EXPECT_FALSE(result.success);
        EXPECT_TRUE(contains(result.disabledPlugins, "pluginB"));
        EXPECT_TRUE(contains(result.loadOrder, "pluginA"));
    }

    TEST(DependencyResolverTest, VersionIncompatibility_ProviderMinorLessThanRequiredFailsResolution)
    {
        // Provider has 1.1, consumer requires 1.2 -> incompatibility
        const PluginManifest pluginA = makeManifest("pluginA", { { "openeden.test", { 1, 1 } } });
        const PluginManifest pluginB = makeManifest("pluginB", {}, { { "openeden.test", { 1, 2 } } });

        const std::vector<PluginManifest> manifests{ pluginA, pluginB };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        EXPECT_FALSE(result.success);
        EXPECT_TRUE(contains(result.disabledPlugins, "pluginB"));
        EXPECT_TRUE(contains(result.loadOrder, "pluginA"));
    }

    TEST(DependencyResolverTest, VersionCompatibility_ProviderMinorGreaterOrEqualSucceeds)
    {
        // Provider has 1.3, consumer requires 1.1 -> compatible!
        const PluginManifest pluginA = makeManifest("pluginA", { { "openeden.test", { 1, 3 } } });
        const PluginManifest pluginB = makeManifest("pluginB", {}, { { "openeden.test", { 1, 1 } } });

        const std::vector<PluginManifest> manifests{ pluginB, pluginA };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        EXPECT_TRUE(result.success);
        EXPECT_TRUE(isBefore(result.loadOrder, "pluginA", "pluginB"));
    }

    TEST(DependencyResolverTest, IncompatibleSdkVersion_MajorMismatchRejected)
    {
        const Version hostSdk{ 1, 2 };
        const PluginManifest pluginA = makeManifest("pluginA", {}, {}, {}, {}, Version{ 2, 0 });

        const std::vector<PluginManifest> manifests{ pluginA };
        const ResolutionResult result = resolveDependencies(manifests, hostSdk);

        EXPECT_FALSE(result.success);
        EXPECT_TRUE(contains(result.disabledPlugins, "pluginA"));
        EXPECT_FALSE(contains(result.loadOrder, "pluginA"));
    }

    TEST(DependencyResolverTest, IncompatibleSdkVersion_MinorGreaterThanHostRejected)
    {
        const Version hostSdk{ 1, 2 };
        // pluginA has sdk 1.3 (newer than host 1.2 -> rejected)
        const PluginManifest pluginA = makeManifest("pluginA", {}, {}, {}, {}, Version{ 1, 3 });
        // pluginB has sdk 1.1 (compatible with host 1.2 -> loads)
        const PluginManifest pluginB = makeManifest("pluginB", {}, {}, {}, {}, Version{ 1, 1 });

        const std::vector<PluginManifest> manifests{ pluginA, pluginB };
        const ResolutionResult result = resolveDependencies(manifests, hostSdk);

        EXPECT_FALSE(result.success);
        EXPECT_TRUE(contains(result.disabledPlugins, "pluginA"));
        EXPECT_TRUE(contains(result.loadOrder, "pluginB"));
    }

    TEST(DependencyResolverTest, Sdk1xManifestRefusedWithDefaultHostVersion)
    {
        const PluginManifest oldPlugin = makeManifest("oldPlugin", {}, {}, {}, {}, Version{ 1, 0 });
        const PluginManifest oldMinorPlugin = makeManifest("oldMinorPlugin", {}, {}, {}, {}, Version{ 1, 7 });
        const PluginManifest currentPlugin = makeManifest("currentPlugin");

        // No host version given: the default is the SDK's own, 2.0
        const ResolutionResult result = resolveDependencies({ oldPlugin, oldMinorPlugin, currentPlugin });

        EXPECT_FALSE(result.success);
        EXPECT_TRUE(contains(result.disabledPlugins, "oldPlugin"));
        EXPECT_TRUE(contains(result.disabledPlugins, "oldMinorPlugin"));
        EXPECT_FALSE(contains(result.loadOrder, "oldPlugin"));
        EXPECT_FALSE(contains(result.loadOrder, "oldMinorPlugin"));
        EXPECT_TRUE(contains(result.loadOrder, "currentPlugin"));

        const auto messageFor = [&result](const std::string& pluginId) -> std::string {
            for (const auto& err : result.errors)
            {
                if (err.pluginId == pluginId)
                    return err.message;
            }
            return {};
        };
        EXPECT_NE(messageFor("oldPlugin").find("built for SDK 1.0; this host provides 2.0"), std::string::npos)
            << messageFor("oldPlugin");
        EXPECT_NE(messageFor("oldMinorPlugin").find("built for SDK 1.7; this host provides 2.0"), std::string::npos)
            << messageFor("oldMinorPlugin");
    }

    TEST(DependencyResolverTest, Sdk1xProviderRefusalDisablesItsDependants)
    {
        const PluginManifest oldProvider
            = makeManifest("oldProvider", { { "openeden.test", { 1, 0 } } }, {}, {}, {}, Version{ 1, 0 });
        const PluginManifest consumer = makeManifest("consumer", {}, { { "openeden.test", { 1, 0 } } });

        const ResolutionResult result = resolveDependencies({ oldProvider, consumer });

        EXPECT_FALSE(result.success);
        EXPECT_TRUE(contains(result.disabledPlugins, "oldProvider"));
        EXPECT_TRUE(contains(result.disabledPlugins, "consumer"));
        EXPECT_TRUE(result.loadOrder.empty());
    }

    TEST(DependencyResolverTest, DuplicateProviders_SameContractMajorCausesFailure)
    {
        // Two plugins providing the same contract major (e.g. "openeden.body" v1.0 and v1.1)
        const PluginManifest pluginA = makeManifest("pluginA", { { "openeden.body", { 1, 0 } } });
        const PluginManifest pluginB = makeManifest("pluginB", { { "openeden.body", { 1, 1 } } });
        const PluginManifest pluginC = makeManifest("pluginC", {}, { { "openeden.body", { 1, 0 } } });
        const PluginManifest pluginD = makeManifest("pluginD");

        const std::vector<PluginManifest> manifests{ pluginA, pluginB, pluginC, pluginD };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        EXPECT_FALSE(result.success);
        // Conflicting providers disabled
        EXPECT_TRUE(contains(result.disabledPlugins, "pluginA"));
        EXPECT_TRUE(contains(result.disabledPlugins, "pluginB"));
        // Consumer depending on the ambiguous contract disabled
        EXPECT_TRUE(contains(result.disabledPlugins, "pluginC"));
        // Independent plugin loads
        EXPECT_TRUE(contains(result.loadOrder, "pluginD"));

        // Error message names the duplicate contract or plugins
        EXPECT_TRUE(errorMentions(result.errors, "openeden.body"));
    }

    TEST(DependencyResolverTest, CycleDetection_DirectCycleDetectedAndReported)
    {
        // A -> B -> A
        const PluginManifest pluginA
            = makeManifest("pluginA", { { "contractA", { 1, 0 } } }, { { "contractB", { 1, 0 } } });
        const PluginManifest pluginB
            = makeManifest("pluginB", { { "contractB", { 1, 0 } } }, { { "contractA", { 1, 0 } } });
        const PluginManifest pluginC = makeManifest("pluginC");

        const std::vector<PluginManifest> manifests{ pluginA, pluginB, pluginC };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        EXPECT_FALSE(result.success);
        EXPECT_TRUE(contains(result.disabledPlugins, "pluginA"));
        EXPECT_TRUE(contains(result.disabledPlugins, "pluginB"));
        EXPECT_TRUE(contains(result.loadOrder, "pluginC"));

        // Error reports cycle and names cycle members
        EXPECT_TRUE(errorMentions(result.errors, "pluginA"));
        EXPECT_TRUE(errorMentions(result.errors, "pluginB"));
    }

    TEST(DependencyResolverTest, CycleDetection_IndirectCycleDetectedAndReported)
    {
        // A -> B -> C -> A
        const PluginManifest pluginA
            = makeManifest("pluginA", { { "contractA", { 1, 0 } } }, { { "contractC", { 1, 0 } } });
        const PluginManifest pluginB
            = makeManifest("pluginB", { { "contractB", { 1, 0 } } }, { { "contractA", { 1, 0 } } });
        const PluginManifest pluginC
            = makeManifest("pluginC", { { "contractC", { 1, 0 } } }, { { "contractB", { 1, 0 } } });
        const PluginManifest pluginD = makeManifest("pluginD");

        const std::vector<PluginManifest> manifests{ pluginA, pluginB, pluginC, pluginD };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        EXPECT_FALSE(result.success);
        EXPECT_TRUE(contains(result.disabledPlugins, "pluginA"));
        EXPECT_TRUE(contains(result.disabledPlugins, "pluginB"));
        EXPECT_TRUE(contains(result.disabledPlugins, "pluginC"));
        EXPECT_TRUE(contains(result.loadOrder, "pluginD"));

        EXPECT_TRUE(errorMentions(result.errors, "pluginA"));
        EXPECT_TRUE(errorMentions(result.errors, "pluginB"));
        EXPECT_TRUE(errorMentions(result.errors, "pluginC"));
    }

    TEST(DependencyResolverTest, OptionalContracts_WhenPresentAndCompatibleIncludedInOrdering)
    {
        const PluginManifest pluginA = makeManifest("pluginA", { { "openeden.opt", { 1, 0 } } });
        const PluginManifest pluginB = makeManifest("pluginB", {}, {}, { { "openeden.opt", { 1, 0 } } });

        const std::vector<PluginManifest> manifests{ pluginB, pluginA };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        EXPECT_TRUE(result.success);
        EXPECT_TRUE(isBefore(result.loadOrder, "pluginA", "pluginB"));
    }

    TEST(DependencyResolverTest, OptionalContracts_WhenMissingConsumerStillSucceeds)
    {
        const PluginManifest pluginB = makeManifest("pluginB", {}, {}, { { "openeden.opt", { 1, 0 } } });

        const std::vector<PluginManifest> manifests{ pluginB };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        EXPECT_TRUE(result.success);
        EXPECT_TRUE(contains(result.loadOrder, "pluginB"));
        EXPECT_TRUE(result.disabledPlugins.empty());
    }

    TEST(DependencyResolverTest, OptionalContracts_WhenIncompatibleConsumerStillSucceeds)
    {
        // Provider has major 2, consumer optionally wants major 1
        const PluginManifest pluginA = makeManifest("pluginA", { { "openeden.opt", { 2, 0 } } });
        const PluginManifest pluginB = makeManifest("pluginB", {}, {}, { { "openeden.opt", { 1, 0 } } });

        const std::vector<PluginManifest> manifests{ pluginA, pluginB };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        EXPECT_TRUE(result.success);
        EXPECT_TRUE(contains(result.loadOrder, "pluginA"));
        EXPECT_TRUE(contains(result.loadOrder, "pluginB"));
        EXPECT_TRUE(result.disabledPlugins.empty());
    }

    TEST(DependencyResolverTest, LoadAfter_EnforcesOrderingWithoutContractDependency)
    {
        // Plugin B has loadAfter = ["pluginA"]
        const PluginManifest pluginA = makeManifest("pluginA");
        const PluginManifest pluginB = makeManifest("pluginB", {}, {}, {}, { "pluginA" });

        const std::vector<PluginManifest> manifests{ pluginB, pluginA };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        EXPECT_TRUE(result.success);
        EXPECT_TRUE(isBefore(result.loadOrder, "pluginA", "pluginB"));
    }

    TEST(DependencyResolverTest, DeterministicTieBreaking_AlphabeticalByPluginId)
    {
        const PluginManifest pAlpha = makeManifest("alpha");
        const PluginManifest pBravo = makeManifest("bravo");
        const PluginManifest pCharlie = makeManifest("charlie");
        const PluginManifest pDelta = makeManifest("delta");

        const std::vector<std::string> expectedOrder{ "alpha", "bravo", "charlie", "delta" };

        // Permutation 1
        const ResolutionResult res1
            = resolveDependencies({ pDelta, pAlpha, pCharlie, pBravo }, openeden::sdk::kSdkVersion);
        EXPECT_EQ(res1.loadOrder, expectedOrder);

        // Permutation 2
        const ResolutionResult res2
            = resolveDependencies({ pBravo, pDelta, pAlpha, pCharlie }, openeden::sdk::kSdkVersion);
        EXPECT_EQ(res2.loadOrder, expectedOrder);

        // Permutation 3
        const ResolutionResult res3
            = resolveDependencies({ pCharlie, pBravo, pDelta, pAlpha }, openeden::sdk::kSdkVersion);
        EXPECT_EQ(res3.loadOrder, expectedOrder);
    }

    TEST(DependencyResolverTest, FaultIsolation_FailureInOneTreeDoesNotPreventIndependentTreeFromLoading)
    {
        // Failing subtree: X requires missing, Y requires X
        const PluginManifest pluginX
            = makeManifest("pluginX", { { "contractX", { 1, 0 } } }, { { "openeden.missing", { 1, 0 } } });
        const PluginManifest pluginY = makeManifest("pluginY", {}, { { "contractX", { 1, 0 } } });

        // Valid subtree: P provides contractP, Q requires P
        const PluginManifest pluginP = makeManifest("pluginP", { { "contractP", { 1, 0 } } });
        const PluginManifest pluginQ = makeManifest("pluginQ", {}, { { "contractP", { 1, 0 } } });

        // Standalone valid plugin R
        const PluginManifest pluginR = makeManifest("pluginR");

        const std::vector<PluginManifest> manifests{ pluginX, pluginY, pluginP, pluginQ, pluginR };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        // Subtree 1 disabled
        EXPECT_TRUE(contains(result.disabledPlugins, "pluginX"));
        EXPECT_TRUE(contains(result.disabledPlugins, "pluginY"));
        EXPECT_FALSE(contains(result.loadOrder, "pluginX"));
        EXPECT_FALSE(contains(result.loadOrder, "pluginY"));

        // Subtree 2 and standalone loaded
        EXPECT_TRUE(contains(result.loadOrder, "pluginP"));
        EXPECT_TRUE(contains(result.loadOrder, "pluginQ"));
        EXPECT_TRUE(contains(result.loadOrder, "pluginR"));
        EXPECT_TRUE(isBefore(result.loadOrder, "pluginP", "pluginQ"));
    }
}
