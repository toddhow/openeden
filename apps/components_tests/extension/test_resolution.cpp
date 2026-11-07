#include <components/extension/dependency_resolver.hpp>
#include <components/extension/manifest.hpp>
#include <openeden/version.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    using openeden::extension::ContractRequirement;
    using openeden::extension::ExtensionManifest;
    using openeden::extension::ResolutionError;
    using openeden::extension::ResolutionResult;
    using openeden::extension::resolveDependencies;
    using openeden::sdk::Version;

    ExtensionManifest makeManifest(std::string id, std::vector<ContractRequirement> providedContracts = {},
        std::vector<ContractRequirement> requiredContracts = {},
        std::vector<ContractRequirement> optionalContracts = {}, std::vector<std::string> loadAfter = {},
        Version sdkVersion = openeden::sdk::kSdkVersion)
    {
        ExtensionManifest m;
        m.id = std::move(id);
        m.name = m.id;
        m.extensionVersion = "1.0.0";
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
            if (err.extensionId.find(target) != std::string::npos || err.message.find(target) != std::string::npos)
                return true;
        }
        return false;
    }

    TEST(DependencyResolverTest, BasicOrdering_RequiredContractProviderLoadsFirst)
    {
        // Extension B requires contract from Extension A -> A before B in load order
        const ExtensionManifest extensionA = makeManifest("extensionA", { { "openeden.test", { 1, 0 } } });
        const ExtensionManifest extensionB = makeManifest("extensionB", {}, { { "openeden.test", { 1, 0 } } });

        // Provide input in reverse order to ensure resolver is sorting by dependencies
        const std::vector<ExtensionManifest> manifests{ extensionB, extensionA };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        EXPECT_TRUE(result.success);
        EXPECT_EQ(result.loadOrder.size(), 2u);
        EXPECT_TRUE(isBefore(result.loadOrder, "extensionA", "extensionB"));
        EXPECT_TRUE(result.disabledExtensions.empty());
        EXPECT_TRUE(result.errors.empty());
    }

    TEST(DependencyResolverTest, TransitiveChain_LoadsInDependencyOrder)
    {
        // C requires B, B requires A -> load order A, B, C
        const ExtensionManifest extensionA = makeManifest("extensionA", { { "contractA", { 1, 0 } } });
        const ExtensionManifest extensionB
            = makeManifest("extensionB", { { "contractB", { 1, 0 } } }, { { "contractA", { 1, 0 } } });
        const ExtensionManifest extensionC = makeManifest("extensionC", {}, { { "contractB", { 1, 0 } } });

        const std::vector<ExtensionManifest> manifests{ extensionC, extensionA, extensionB };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        EXPECT_TRUE(result.success);
        const std::vector<std::string> expectedOrder{ "extensionA", "extensionB", "extensionC" };
        EXPECT_EQ(result.loadOrder, expectedOrder);
        EXPECT_TRUE(result.disabledExtensions.empty());
    }

    TEST(DependencyResolverTest, MissingRequiredContract_DisablesExtensionAndCascadesToDependents)
    {
        // Extension B requires missing contract; Extension C requires B; Extension D is independent
        const ExtensionManifest extensionB
            = makeManifest("extensionB", { { "contractB", { 1, 0 } } }, { { "openeden.missing", { 1, 0 } } });
        const ExtensionManifest extensionC = makeManifest("extensionC", {}, { { "contractB", { 1, 0 } } });
        const ExtensionManifest extensionD = makeManifest("extensionD");

        const std::vector<ExtensionManifest> manifests{ extensionB, extensionC, extensionD };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        EXPECT_FALSE(result.success);
        // B and C should be disabled; D should succeed
        EXPECT_TRUE(contains(result.disabledExtensions, "extensionB"));
        EXPECT_TRUE(contains(result.disabledExtensions, "extensionC"));
        EXPECT_FALSE(contains(result.loadOrder, "extensionB"));
        EXPECT_FALSE(contains(result.loadOrder, "extensionC"));

        EXPECT_TRUE(contains(result.loadOrder, "extensionD"));

        // Error message must name the missing contract
        EXPECT_TRUE(errorMentions(result.errors, "openeden.missing"));
    }

    TEST(DependencyResolverTest, VersionIncompatibility_MajorMismatchFailsResolution)
    {
        // Provider has major 2, consumer requires major 1 -> incompatibility
        const ExtensionManifest extensionA = makeManifest("extensionA", { { "openeden.test", { 2, 0 } } });
        const ExtensionManifest extensionB = makeManifest("extensionB", {}, { { "openeden.test", { 1, 0 } } });

        const std::vector<ExtensionManifest> manifests{ extensionA, extensionB };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        EXPECT_FALSE(result.success);
        EXPECT_TRUE(contains(result.disabledExtensions, "extensionB"));
        EXPECT_TRUE(contains(result.loadOrder, "extensionA"));
    }

    TEST(DependencyResolverTest, VersionIncompatibility_ProviderMinorLessThanRequiredFailsResolution)
    {
        // Provider has 1.1, consumer requires 1.2 -> incompatibility
        const ExtensionManifest extensionA = makeManifest("extensionA", { { "openeden.test", { 1, 1 } } });
        const ExtensionManifest extensionB = makeManifest("extensionB", {}, { { "openeden.test", { 1, 2 } } });

        const std::vector<ExtensionManifest> manifests{ extensionA, extensionB };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        EXPECT_FALSE(result.success);
        EXPECT_TRUE(contains(result.disabledExtensions, "extensionB"));
        EXPECT_TRUE(contains(result.loadOrder, "extensionA"));
    }

    TEST(DependencyResolverTest, VersionCompatibility_ProviderMinorGreaterOrEqualSucceeds)
    {
        // Provider has 1.3, consumer requires 1.1 -> compatible!
        const ExtensionManifest extensionA = makeManifest("extensionA", { { "openeden.test", { 1, 3 } } });
        const ExtensionManifest extensionB = makeManifest("extensionB", {}, { { "openeden.test", { 1, 1 } } });

        const std::vector<ExtensionManifest> manifests{ extensionB, extensionA };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        EXPECT_TRUE(result.success);
        EXPECT_TRUE(isBefore(result.loadOrder, "extensionA", "extensionB"));
    }

    TEST(DependencyResolverTest, IncompatibleSdkVersion_MajorMismatchRejected)
    {
        const Version hostSdk{ 1, 2 };
        const ExtensionManifest extensionA = makeManifest("extensionA", {}, {}, {}, {}, Version{ 2, 0 });

        const std::vector<ExtensionManifest> manifests{ extensionA };
        const ResolutionResult result = resolveDependencies(manifests, hostSdk);

        EXPECT_FALSE(result.success);
        EXPECT_TRUE(contains(result.disabledExtensions, "extensionA"));
        EXPECT_FALSE(contains(result.loadOrder, "extensionA"));
    }

    TEST(DependencyResolverTest, IncompatibleSdkVersion_MinorGreaterThanHostRejected)
    {
        const Version hostSdk{ 1, 2 };
        // extensionA has sdk 1.3 (newer than host 1.2 -> rejected)
        const ExtensionManifest extensionA = makeManifest("extensionA", {}, {}, {}, {}, Version{ 1, 3 });
        // extensionB has sdk 1.1 (compatible with host 1.2 -> loads)
        const ExtensionManifest extensionB = makeManifest("extensionB", {}, {}, {}, {}, Version{ 1, 1 });

        const std::vector<ExtensionManifest> manifests{ extensionA, extensionB };
        const ResolutionResult result = resolveDependencies(manifests, hostSdk);

        EXPECT_FALSE(result.success);
        EXPECT_TRUE(contains(result.disabledExtensions, "extensionA"));
        EXPECT_TRUE(contains(result.loadOrder, "extensionB"));
    }

    TEST(DependencyResolverTest, Sdk1xManifestRefusedWithDefaultHostVersion)
    {
        const ExtensionManifest oldExtension = makeManifest("oldExtension", {}, {}, {}, {}, Version{ 1, 0 });
        const ExtensionManifest oldMinorExtension = makeManifest("oldMinorExtension", {}, {}, {}, {}, Version{ 1, 7 });
        const ExtensionManifest currentExtension = makeManifest("currentExtension");

        // No host version given: the default is the SDK's own, 3.0
        const ResolutionResult result = resolveDependencies({ oldExtension, oldMinorExtension, currentExtension });

        EXPECT_FALSE(result.success);
        EXPECT_TRUE(contains(result.disabledExtensions, "oldExtension"));
        EXPECT_TRUE(contains(result.disabledExtensions, "oldMinorExtension"));
        EXPECT_FALSE(contains(result.loadOrder, "oldExtension"));
        EXPECT_FALSE(contains(result.loadOrder, "oldMinorExtension"));
        EXPECT_TRUE(contains(result.loadOrder, "currentExtension"));

        const auto messageFor = [&result](const std::string& extensionId) -> std::string {
            for (const auto& err : result.errors)
            {
                if (err.extensionId == extensionId)
                    return err.message;
            }
            return {};
        };
        EXPECT_NE(messageFor("oldExtension").find("built for SDK 1.0; this host provides 4.0"), std::string::npos)
            << messageFor("oldExtension");
        EXPECT_NE(messageFor("oldMinorExtension").find("built for SDK 1.7; this host provides 4.0"), std::string::npos)
            << messageFor("oldMinorExtension");
    }

    // Task 0031 stage C: SDK 3.0 removed the editor hooks from Host, so a manifest built for 2.x is refused.
    TEST(DependencyResolverTest, Sdk2xManifestRefusedWithDefaultHostVersion)
    {
        const ExtensionManifest oldExtension = makeManifest("oldExtension", {}, {}, {}, {}, Version{ 2, 0 });
        const ExtensionManifest oldMinorExtension = makeManifest("oldMinorExtension", {}, {}, {}, {}, Version{ 2, 7 });
        const ExtensionManifest currentExtension = makeManifest("currentExtension");

        const ResolutionResult result = resolveDependencies({ oldExtension, oldMinorExtension, currentExtension });

        EXPECT_FALSE(result.success);
        EXPECT_TRUE(contains(result.disabledExtensions, "oldExtension"));
        EXPECT_TRUE(contains(result.disabledExtensions, "oldMinorExtension"));
        EXPECT_TRUE(contains(result.loadOrder, "currentExtension"));

        const auto messageFor = [&result](const std::string& extensionId) -> std::string {
            for (const auto& err : result.errors)
            {
                if (err.extensionId == extensionId)
                    return err.message;
            }
            return {};
        };
        EXPECT_NE(messageFor("oldExtension").find("built for SDK 2.0; this host provides 4.0"), std::string::npos)
            << messageFor("oldExtension");
        EXPECT_NE(messageFor("oldMinorExtension").find("built for SDK 2.7; this host provides 4.0"), std::string::npos)
            << messageFor("oldMinorExtension");
    }

    TEST(DependencyResolverTest, Sdk1xProviderRefusalDisablesItsDependants)
    {
        const ExtensionManifest oldProvider
            = makeManifest("oldProvider", { { "openeden.test", { 1, 0 } } }, {}, {}, {}, Version{ 1, 0 });
        const ExtensionManifest consumer = makeManifest("consumer", {}, { { "openeden.test", { 1, 0 } } });

        const ResolutionResult result = resolveDependencies({ oldProvider, consumer });

        EXPECT_FALSE(result.success);
        EXPECT_TRUE(contains(result.disabledExtensions, "oldProvider"));
        EXPECT_TRUE(contains(result.disabledExtensions, "consumer"));
        EXPECT_TRUE(result.loadOrder.empty());
    }

    TEST(DependencyResolverTest, DuplicateProviders_SameContractMajorCausesFailure)
    {
        // Two extensions providing the same contract major (e.g. "openeden.body" v1.0 and v1.1)
        const ExtensionManifest extensionA = makeManifest("extensionA", { { "openeden.body", { 1, 0 } } });
        const ExtensionManifest extensionB = makeManifest("extensionB", { { "openeden.body", { 1, 1 } } });
        const ExtensionManifest extensionC = makeManifest("extensionC", {}, { { "openeden.body", { 1, 0 } } });
        const ExtensionManifest extensionD = makeManifest("extensionD");

        const std::vector<ExtensionManifest> manifests{ extensionA, extensionB, extensionC, extensionD };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        EXPECT_FALSE(result.success);
        // Conflicting providers disabled
        EXPECT_TRUE(contains(result.disabledExtensions, "extensionA"));
        EXPECT_TRUE(contains(result.disabledExtensions, "extensionB"));
        // Consumer depending on the ambiguous contract disabled
        EXPECT_TRUE(contains(result.disabledExtensions, "extensionC"));
        // Independent extension loads
        EXPECT_TRUE(contains(result.loadOrder, "extensionD"));

        // Error message names the duplicate contract or extensions
        EXPECT_TRUE(errorMentions(result.errors, "openeden.body"));
    }

    TEST(DependencyResolverTest, CycleDetection_DirectCycleDetectedAndReported)
    {
        // A -> B -> A
        const ExtensionManifest extensionA
            = makeManifest("extensionA", { { "contractA", { 1, 0 } } }, { { "contractB", { 1, 0 } } });
        const ExtensionManifest extensionB
            = makeManifest("extensionB", { { "contractB", { 1, 0 } } }, { { "contractA", { 1, 0 } } });
        const ExtensionManifest extensionC = makeManifest("extensionC");

        const std::vector<ExtensionManifest> manifests{ extensionA, extensionB, extensionC };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        EXPECT_FALSE(result.success);
        EXPECT_TRUE(contains(result.disabledExtensions, "extensionA"));
        EXPECT_TRUE(contains(result.disabledExtensions, "extensionB"));
        EXPECT_TRUE(contains(result.loadOrder, "extensionC"));

        // Error reports cycle and names cycle members
        EXPECT_TRUE(errorMentions(result.errors, "extensionA"));
        EXPECT_TRUE(errorMentions(result.errors, "extensionB"));
    }

    TEST(DependencyResolverTest, CycleDetection_IndirectCycleDetectedAndReported)
    {
        // A -> B -> C -> A
        const ExtensionManifest extensionA
            = makeManifest("extensionA", { { "contractA", { 1, 0 } } }, { { "contractC", { 1, 0 } } });
        const ExtensionManifest extensionB
            = makeManifest("extensionB", { { "contractB", { 1, 0 } } }, { { "contractA", { 1, 0 } } });
        const ExtensionManifest extensionC
            = makeManifest("extensionC", { { "contractC", { 1, 0 } } }, { { "contractB", { 1, 0 } } });
        const ExtensionManifest extensionD = makeManifest("extensionD");

        const std::vector<ExtensionManifest> manifests{ extensionA, extensionB, extensionC, extensionD };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        EXPECT_FALSE(result.success);
        EXPECT_TRUE(contains(result.disabledExtensions, "extensionA"));
        EXPECT_TRUE(contains(result.disabledExtensions, "extensionB"));
        EXPECT_TRUE(contains(result.disabledExtensions, "extensionC"));
        EXPECT_TRUE(contains(result.loadOrder, "extensionD"));

        EXPECT_TRUE(errorMentions(result.errors, "extensionA"));
        EXPECT_TRUE(errorMentions(result.errors, "extensionB"));
        EXPECT_TRUE(errorMentions(result.errors, "extensionC"));
    }

    TEST(DependencyResolverTest, OptionalContracts_WhenPresentAndCompatibleIncludedInOrdering)
    {
        const ExtensionManifest extensionA = makeManifest("extensionA", { { "openeden.opt", { 1, 0 } } });
        const ExtensionManifest extensionB = makeManifest("extensionB", {}, {}, { { "openeden.opt", { 1, 0 } } });

        const std::vector<ExtensionManifest> manifests{ extensionB, extensionA };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        EXPECT_TRUE(result.success);
        EXPECT_TRUE(isBefore(result.loadOrder, "extensionA", "extensionB"));
    }

    TEST(DependencyResolverTest, OptionalContracts_WhenMissingConsumerStillSucceeds)
    {
        const ExtensionManifest extensionB = makeManifest("extensionB", {}, {}, { { "openeden.opt", { 1, 0 } } });

        const std::vector<ExtensionManifest> manifests{ extensionB };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        EXPECT_TRUE(result.success);
        EXPECT_TRUE(contains(result.loadOrder, "extensionB"));
        EXPECT_TRUE(result.disabledExtensions.empty());
    }

    TEST(DependencyResolverTest, OptionalContracts_WhenIncompatibleConsumerStillSucceeds)
    {
        // Provider has major 2, consumer optionally wants major 1
        const ExtensionManifest extensionA = makeManifest("extensionA", { { "openeden.opt", { 2, 0 } } });
        const ExtensionManifest extensionB = makeManifest("extensionB", {}, {}, { { "openeden.opt", { 1, 0 } } });

        const std::vector<ExtensionManifest> manifests{ extensionA, extensionB };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        EXPECT_TRUE(result.success);
        EXPECT_TRUE(contains(result.loadOrder, "extensionA"));
        EXPECT_TRUE(contains(result.loadOrder, "extensionB"));
        EXPECT_TRUE(result.disabledExtensions.empty());
    }

    TEST(DependencyResolverTest, LoadAfter_EnforcesOrderingWithoutContractDependency)
    {
        // Extension B has loadAfter = ["extensionA"]
        const ExtensionManifest extensionA = makeManifest("extensionA");
        const ExtensionManifest extensionB = makeManifest("extensionB", {}, {}, {}, { "extensionA" });

        const std::vector<ExtensionManifest> manifests{ extensionB, extensionA };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        EXPECT_TRUE(result.success);
        EXPECT_TRUE(isBefore(result.loadOrder, "extensionA", "extensionB"));
    }

    TEST(DependencyResolverTest, DeterministicTieBreaking_AlphabeticalByExtensionId)
    {
        const ExtensionManifest pAlpha = makeManifest("alpha");
        const ExtensionManifest pBravo = makeManifest("bravo");
        const ExtensionManifest pCharlie = makeManifest("charlie");
        const ExtensionManifest pDelta = makeManifest("delta");

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
        const ExtensionManifest extensionX
            = makeManifest("extensionX", { { "contractX", { 1, 0 } } }, { { "openeden.missing", { 1, 0 } } });
        const ExtensionManifest extensionY = makeManifest("extensionY", {}, { { "contractX", { 1, 0 } } });

        // Valid subtree: P provides contractP, Q requires P
        const ExtensionManifest extensionP = makeManifest("extensionP", { { "contractP", { 1, 0 } } });
        const ExtensionManifest extensionQ = makeManifest("extensionQ", {}, { { "contractP", { 1, 0 } } });

        // Standalone valid extension R
        const ExtensionManifest extensionR = makeManifest("extensionR");

        const std::vector<ExtensionManifest> manifests{ extensionX, extensionY, extensionP, extensionQ, extensionR };
        const ResolutionResult result = resolveDependencies(manifests, openeden::sdk::kSdkVersion);

        // Subtree 1 disabled
        EXPECT_TRUE(contains(result.disabledExtensions, "extensionX"));
        EXPECT_TRUE(contains(result.disabledExtensions, "extensionY"));
        EXPECT_FALSE(contains(result.loadOrder, "extensionX"));
        EXPECT_FALSE(contains(result.loadOrder, "extensionY"));

        // Subtree 2 and standalone loaded
        EXPECT_TRUE(contains(result.loadOrder, "extensionP"));
        EXPECT_TRUE(contains(result.loadOrder, "extensionQ"));
        EXPECT_TRUE(contains(result.loadOrder, "extensionR"));
        EXPECT_TRUE(isBefore(result.loadOrder, "extensionP", "extensionQ"));
    }
}
