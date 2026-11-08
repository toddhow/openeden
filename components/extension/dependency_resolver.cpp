#include <components/extension/dependency_resolver.hpp>

#include <algorithm>
#include <functional>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace openeden::extension
{
    namespace
    {
        std::string versionToString(const openeden::sdk::Version& v)
        {
            std::ostringstream ss;
            ss << v.major << '.' << v.minor;
            return ss.str();
        }
    } // namespace

    ResolutionResult resolveDependencies(
        const std::vector<ExtensionManifest>& manifests, openeden::sdk::Version hostSdkVersion)
    {
        ResolutionResult result;

        // Map manifests by extension ID
        std::map<std::string, const ExtensionManifest*> manifestMap;
        for (const auto& manifest : manifests)
        {
            manifestMap[manifest.id] = &manifest;
        }

        std::set<std::string> disabled;
        std::vector<ResolutionError> errors;

        // Step 1: SDK version compatibility check
        for (const auto& manifest : manifests)
        {
            if (manifest.sdkVersion.major != hostSdkVersion.major || manifest.sdkVersion.minor > hostSdkVersion.minor)
            {
                disabled.insert(manifest.id);
                errors.push_back({ manifest.id,
                    "Incompatible SDK version: built for SDK " + versionToString(manifest.sdkVersion)
                        + "; this host provides " + versionToString(hostSdkVersion) });
            }
        }

        // Helper to get active manifests
        auto getActiveManifests = [&]() {
            std::vector<const ExtensionManifest*> active;
            for (const auto& [id, mPtr] : manifestMap)
            {
                if (disabled.find(id) == disabled.end())
                    active.push_back(mPtr);
            }
            return active;
        };

        // Step 2: Check for duplicate providers of the same contract major
        std::map<std::pair<std::string, std::uint16_t>, std::vector<std::string>> contractProviders;
        for (const auto* m : getActiveManifests())
        {
            for (const auto& provision : m->providedContracts)
            {
                contractProviders[{ provision.contract, provision.version.major }].push_back(m->id);
            }
        }

        for (const auto& [contractKey, providerIds] : contractProviders)
        {
            if (providerIds.size() > 1)
            {
                std::string conflictingList;
                for (size_t i = 0; i < providerIds.size(); ++i)
                {
                    if (i > 0)
                        conflictingList += ", ";
                    conflictingList += providerIds[i];
                }

                for (const auto& pId : providerIds)
                {
                    disabled.insert(pId);
                    errors.push_back({ pId,
                        "Duplicate provider for contract " + contractKey.first + " major "
                            + std::to_string(contractKey.second) + " provided by: " + conflictingList
                            + (contractKey.first == "openeden.game"
                                    ? " (only one game's compatibility pack can be enabled in a session: remove"
                                      " the extension= line of the other game, or the game= line)"
                                    : "") });
                }
            }
        }

        // Step 3: Check required contracts
        // Rebuild contract provider map with non-disabled extensions
        std::map<std::pair<std::string, std::uint16_t>, std::pair<std::string, openeden::sdk::Version>> validProviders;
        for (const auto* m : getActiveManifests())
        {
            for (const auto& provision : m->providedContracts)
            {
                validProviders[{ provision.contract, provision.version.major }] = { m->id, provision.version };
            }
        }

        for (const auto* m : getActiveManifests())
        {
            for (const auto& req : m->requiredContracts)
            {
                auto it = validProviders.find({ req.contract, req.version.major });
                if (it == validProviders.end())
                {
                    disabled.insert(m->id);
                    errors.push_back({ m->id,
                        "Missing required contract: " + req.contract + " (version " + versionToString(req.version)
                            + ")" });
                    break;
                }
                else if (!openeden::sdk::isCompatible(req.version, it->second.second))
                {
                    disabled.insert(m->id);
                    errors.push_back({ m->id,
                        "Incompatible contract version for " + req.contract + ": required "
                            + versionToString(req.version) + ", provided " + versionToString(it->second.second) });
                    break;
                }
            }
        }

        // Step 4: Cascade disabling of dependents
        bool newlyDisabled = true;
        while (newlyDisabled)
        {
            newlyDisabled = false;
            validProviders.clear();
            for (const auto* m : getActiveManifests())
            {
                for (const auto& provision : m->providedContracts)
                {
                    validProviders[{ provision.contract, provision.version.major }] = { m->id, provision.version };
                }
            }

            for (const auto* m : getActiveManifests())
            {
                for (const auto& req : m->requiredContracts)
                {
                    auto it = validProviders.find({ req.contract, req.version.major });
                    if (it == validProviders.end() || disabled.find(it->second.first) != disabled.end())
                    {
                        disabled.insert(m->id);
                        errors.push_back(
                            { m->id, "Disabled due to unavailable dependency for contract: " + req.contract });
                        newlyDisabled = true;
                        break;
                    }
                }
            }
        }

        // Step 5: Cycle detection and resolution loop
        while (true)
        {
            const auto active = getActiveManifests();
            std::set<std::string> activeSet;
            for (const auto* m : active)
                activeSet.insert(m->id);

            // Rebuild valid providers for active set
            validProviders.clear();
            for (const auto* m : active)
            {
                for (const auto& provision : m->providedContracts)
                {
                    validProviders[{ provision.contract, provision.version.major }] = { m->id, provision.version };
                }
            }

            // Build adjacency list: provider -> consumer (u -> v means u must load before v)
            std::map<std::string, std::set<std::string>> adj;
            for (const auto* m : active)
            {
                adj[m->id]; // ensure entry exists

                // Required contracts
                for (const auto& req : m->requiredContracts)
                {
                    auto it = validProviders.find({ req.contract, req.version.major });
                    if (it != validProviders.end() && it->second.first != m->id)
                    {
                        adj[it->second.first].insert(m->id);
                    }
                }

                // Optional contracts
                for (const auto& opt : m->optionalContracts)
                {
                    auto it = validProviders.find({ opt.contract, opt.version.major });
                    if (it != validProviders.end() && it->second.first != m->id
                        && openeden::sdk::isCompatible(opt.version, it->second.second))
                    {
                        adj[it->second.first].insert(m->id);
                    }
                }

                // loadAfter hints: target -> m->id (target must load before m)
                for (const auto& target : m->loadAfter)
                {
                    if (activeSet.find(target) != activeSet.end() && target != m->id)
                    {
                        adj[target].insert(m->id);
                    }
                }
            }

            // Find cycles using DFS coloring (0 = unvisited, 1 = visiting, 2 = visited)
            std::map<std::string, int> state;
            std::map<std::string, std::string> parent;
            std::set<std::string> cycleNodes;

            std::function<bool(const std::string&)> dfs = [&](const std::string& u) -> bool {
                state[u] = 1;
                for (const auto& v : adj[u])
                {
                    if (state[v] == 1)
                    {
                        cycleNodes.insert(v);
                        for (std::string curr = u; curr != v && !curr.empty(); curr = parent[curr])
                        {
                            cycleNodes.insert(curr);
                        }
                        return true;
                    }
                    else if (state[v] == 0)
                    {
                        parent[v] = u;
                        if (dfs(v))
                            return true;
                    }
                }
                state[u] = 2;
                return false;
            };

            for (const auto* m : active)
            {
                if (state[m->id] == 0)
                {
                    if (dfs(m->id))
                        break;
                }
            }

            if (cycleNodes.empty())
                break; // No cycles!

            // Record error naming all cycle nodes
            std::string cycleMembers;
            for (const auto& node : cycleNodes)
            {
                if (!cycleMembers.empty())
                    cycleMembers += ", ";
                cycleMembers += node;
            }

            for (const auto& node : cycleNodes)
            {
                disabled.insert(node);
                errors.push_back({ node, "Dependency cycle detected involving: " + cycleMembers });
            }

            // Cascade disable dependents of newly disabled cycle nodes
            newlyDisabled = true;
            while (newlyDisabled)
            {
                newlyDisabled = false;
                validProviders.clear();
                for (const auto* m : getActiveManifests())
                {
                    for (const auto& provision : m->providedContracts)
                    {
                        validProviders[{ provision.contract, provision.version.major }] = { m->id, provision.version };
                    }
                }

                for (const auto* m : getActiveManifests())
                {
                    for (const auto& req : m->requiredContracts)
                    {
                        auto it = validProviders.find({ req.contract, req.version.major });
                        if (it == validProviders.end() || disabled.find(it->second.first) != disabled.end())
                        {
                            disabled.insert(m->id);
                            errors.push_back(
                                { m->id, "Disabled due to unavailable dependency for contract: " + req.contract });
                            newlyDisabled = true;
                            break;
                        }
                    }
                }
            }
        }

        // Step 6: Deterministic Topological Sort (Kahn's algorithm)
        const auto finalActive = getActiveManifests();
        std::set<std::string> finalActiveSet;
        for (const auto* m : finalActive)
            finalActiveSet.insert(m->id);

        validProviders.clear();
        for (const auto* m : finalActive)
        {
            for (const auto& provision : m->providedContracts)
            {
                validProviders[{ provision.contract, provision.version.major }] = { m->id, provision.version };
            }
        }

        std::map<std::string, std::set<std::string>> finalAdj;
        std::map<std::string, int> inDegree;
        for (const auto* m : finalActive)
        {
            finalAdj[m->id];
            inDegree[m->id] = 0;
        }

        for (const auto* m : finalActive)
        {
            // Required contracts
            for (const auto& req : m->requiredContracts)
            {
                auto it = validProviders.find({ req.contract, req.version.major });
                if (it != validProviders.end() && it->second.first != m->id)
                {
                    if (finalAdj[it->second.first].insert(m->id).second)
                    {
                        inDegree[m->id]++;
                    }
                }
            }

            // Optional contracts
            for (const auto& opt : m->optionalContracts)
            {
                auto it = validProviders.find({ opt.contract, opt.version.major });
                if (it != validProviders.end() && it->second.first != m->id
                    && openeden::sdk::isCompatible(opt.version, it->second.second))
                {
                    if (finalAdj[it->second.first].insert(m->id).second)
                    {
                        inDegree[m->id]++;
                    }
                }
            }

            // loadAfter hints
            for (const auto& target : m->loadAfter)
            {
                if (finalActiveSet.find(target) != finalActiveSet.end() && target != m->id)
                {
                    if (finalAdj[target].insert(m->id).second)
                    {
                        inDegree[m->id]++;
                    }
                }
            }
        }

        // Ready set kept sorted lexicographically for deterministic tie-breaking
        std::set<std::string> ready;
        for (const auto& [id, deg] : inDegree)
        {
            if (deg == 0)
                ready.insert(id);
        }

        std::vector<std::string> loadOrder;
        while (!ready.empty())
        {
            auto it = ready.begin();
            std::string u = *it;
            ready.erase(it);

            loadOrder.push_back(u);

            for (const auto& v : finalAdj[u])
            {
                inDegree[v]--;
                if (inDegree[v] == 0)
                {
                    ready.insert(v);
                }
            }
        }

        result.loadOrder = std::move(loadOrder);
        result.disabledExtensions = std::vector<std::string>(disabled.begin(), disabled.end());
        result.errors = std::move(errors);
        result.success = result.disabledExtensions.empty() && result.errors.empty();

        return result;
    }
} // namespace openeden::extension
