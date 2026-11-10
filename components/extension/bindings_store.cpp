#include "bindings_store.hpp"

#include <chrono>

namespace openeden::extension
{
    using openeden::sdk::BindingResult;
    using openeden::sdk::BindingStatus;
    using openeden::sdk::BindingType;
    using openeden::sdk::DeclareResult;

    bool isBindingIdentifier(std::string_view text) noexcept
    {
        if (text.empty() || text.front() < 'a' || text.front() > 'z')
            return false;
        for (const char c : text)
        {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
            if (!ok)
                return false;
        }
        return true;
    }

    namespace
    {
        DeclareResult refuse(std::string_view packId, std::string_view name, std::string_view why)
        {
            DeclareResult result;
            result.ok = false;
            result.message = "Game pack '" + std::string(packId) + "' declared script bindings namespace '"
                + std::string(name) + "': " + std::string(why);
            return result;
        }

        bool validType(BindingType type) noexcept
        {
            return type == BindingType::Bool || type == BindingType::Int32 || type == BindingType::Double
                || type == BindingType::String;
        }
    }

    std::size_t BindingsStore::functionCount() const noexcept
    {
        std::size_t count = 0;
        for (const auto& ns : mNamespaces)
            count += ns->functions.size();
        return count;
    }

    DeclareResult BindingsStore::declare(
        std::string_view packId, std::string_view gameId, const openeden::sdk::NamespaceDescriptor& descriptor)
    {
        const std::string_view name = descriptor.name;

        // "<game id>.<area>"
        const std::size_t dot = name.find('.');
        if (dot == std::string_view::npos)
            return refuse(packId, name, "the name must be '<game id>.<area>'");
        const std::string_view game = name.substr(0, dot);
        const std::string_view area = name.substr(dot + 1);
        if (!isBindingIdentifier(game) || !isBindingIdentifier(area))
            return refuse(packId, name, "each part of the name must match [a-z][a-z0-9_]*");
        if (game != gameId)
            return refuse(packId, name, "the name must start with the session's game id '" + std::string(gameId) + "'");
        if (descriptor.version.major < 1)
            return refuse(packId, name, "the major version must be at least 1");
        for (const auto& existing : mNamespaces)
        {
            if (existing->name == name)
                return refuse(packId, name, "a namespace of this name is already declared");
        }
        if (mNamespaces.size() >= openeden::sdk::kMaxBindingNamespaces)
            return refuse(packId, name, "more than 64 namespaces");
        if (descriptor.functions.size() > openeden::sdk::kMaxBindingFunctions)
            return refuse(packId, name, "more than 256 functions");

        auto bound = std::make_unique<BoundNamespace>();
        bound->name = std::string(name);
        bound->packId = std::string(packId);
        bound->version = descriptor.version;
        bound->functions.reserve(descriptor.functions.size());

        for (const openeden::sdk::FunctionDescriptor& fn : descriptor.functions)
        {
            const std::string where = "function '" + std::string(fn.name) + "'";
            if (!isBindingIdentifier(fn.name))
                return refuse(packId, name, where + ": the name must match [a-z][a-z0-9_]*");
            for (const BoundFunction& other : bound->functions)
            {
                if (other.name == fn.name)
                    return refuse(packId, name, where + " is declared twice");
            }
            if (fn.handler == nullptr)
                return refuse(packId, name, where + " has no handler");
            if (fn.params.size() > openeden::sdk::kMaxBindingParameters)
                return refuse(packId, name, where + " has more than 8 parameters");
            for (const BindingType type : fn.params)
            {
                if (!validType(type))
                    return refuse(packId, name, where + " has a parameter of an unknown type");
            }
            if (fn.hasResult && !validType(fn.result))
                return refuse(packId, name, where + " has a result of an unknown type");
            if (fn.access != openeden::sdk::BindingAccess::Read)
                return refuse(packId, name, where + " has an access this version does not support");

            BoundFunction entry;
            entry.name = std::string(fn.name);
            entry.qualifiedName = bound->name + "." + entry.name;
            entry.params.assign(fn.params.begin(), fn.params.end());
            entry.hasResult = fn.hasResult;
            entry.result = fn.result;
            entry.access = fn.access;
            entry.handler = fn.handler;
            entry.maxCostMicroseconds = fn.maxCostMicroseconds;
            bound->functions.push_back(std::move(entry));
        }

        mNamespaces.push_back(std::move(bound));
        DeclareResult ok;
        ok.ok = true;
        return ok;
    }

    void BindingsStore::retirePack(std::string_view packId) noexcept
    {
        for (const auto& ns : mNamespaces)
        {
            if (ns->packId != packId)
                continue;
            for (BoundFunction& fn : ns->functions)
                fn.handler = nullptr;
        }
    }

    void BindingsStore::retireAll() noexcept
    {
        for (const auto& ns : mNamespaces)
            for (BoundFunction& fn : ns->functions)
                fn.handler = nullptr;
    }

    Invoked BindingsStore::invoke(BoundFunction& function, std::span<const openeden::sdk::BindingValue> args)
    {
        Invoked invoked;
        if (function.handler == nullptr)
        {
            invoked.result.status = BindingStatus::Refused;
            invoked.result.message = "the game pack that declared this function is no longer loaded";
            return invoked;
        }

        const bool timed = function.maxCostMicroseconds != 0;
        const auto begin = timed ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        try
        {
            invoked.result = function.handler->call(args);
        }
        catch (...)
        {
            invoked.result = BindingResult{};
            invoked.result.status = BindingStatus::Internal;
            invoked.result.message = "an exception escaped the game pack's handler";
            invoked.firstEscapedException = function.escapedExceptions++ == 0;
            return invoked;
        }
        if (timed)
        {
            const auto micros
                = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - begin);
            if (micros.count() > static_cast<std::int64_t>(function.maxCostMicroseconds))
                invoked.firstOverCost = function.overCostCalls++ == 0;
        }

        if (invoked.result.status == BindingStatus::Ok && function.hasResult)
        {
            const bool wrongType = invoked.result.value.type != function.result;
            const bool tooLong = function.result == BindingType::String
                && invoked.result.value.text.size() > openeden::sdk::kMaxBindingStringBytes;
            if (wrongType || tooLong)
            {
                invoked.result = BindingResult{};
                invoked.result.status = BindingStatus::Internal;
                invoked.result.message = wrongType ? "the handler returned a value of the wrong type"
                                                   : "the handler returned a string over 64 KiB";
            }
        }
        return invoked;
    }
}
