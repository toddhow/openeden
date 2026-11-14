#include "backendpackage.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include <sol/sol.hpp>

namespace UiLua
{
    namespace
    {
        struct SessionState
        {
            std::shared_ptr<Ui::Backend> mBackend;
            bool mDirty = false; // something was written since the last commit
            std::vector<Ui::Event> mEvents;
        };

        [[noreturn]] void fail(const char* function, const char* what)
        {
            throw std::runtime_error(std::string("openeden.uibackend.") + function + ": " + what);
        }

        Ui::ElementId idArg(const sol::object& value, const char* function, const char* arg)
        {
            if (value.get_type() != sol::type::number)
                fail(function, (std::string(arg) + " must be an element id (a number)").c_str());
            const double number = value.as<double>();
            if (!(number >= 0.0 && number <= 4294967295.0) || std::floor(number) != number)
                fail(function, (std::string(arg) + " is not a valid element id").c_str());
            return static_cast<Ui::ElementId>(number);
        }

        bool isNil(const sol::object& value)
        {
            return value.get_type() == sol::type::lua_nil || value.get_type() == sol::type::none;
        }

        std::string_view stringArg(const sol::object& value, const char* function, const char* arg)
        {
            if (value.get_type() != sol::type::string)
                fail(function, (std::string(arg) + " must be a string").c_str());
            // The Lua string stays alive while `value` does.
            return value.as<std::string_view>();
        }
    }

    sol::table makeSessionTable(sol::state_view lua, std::shared_ptr<Ui::Backend> backend)
    {
        if (backend == nullptr)
            throw std::runtime_error("openeden.uibackend: no backend");
        auto state = std::make_shared<SessionState>();
        state->mBackend = std::move(backend);
        sol::table t(lua, sol::create);

        t["createDocument"] = [state](sol::object name) {
            const std::string_view n = stringArg(name, "createDocument", "name");
            const Ui::ElementId id = state->mBackend->allocateId();
            state->mBackend->createDocument(id, n);
            state->mDirty = true;
            return static_cast<double>(id);
        };
        t["create"] = [state](sol::object tag) {
            const std::string_view n = stringArg(tag, "create", "tag");
            const Ui::ElementId id = state->mBackend->allocateId();
            state->mBackend->create(id, n);
            state->mDirty = true;
            return static_cast<double>(id);
        };
        t["destroy"] = [state](sol::object id) {
            state->mBackend->destroy(idArg(id, "destroy", "id"));
            state->mDirty = true;
        };
        t["insertBefore"] = [state](sol::object parent, sol::object child, sol::object before) {
            const Ui::ElementId p = idArg(parent, "insertBefore", "parent");
            const Ui::ElementId c = idArg(child, "insertBefore", "child");
            const Ui::ElementId b = isNil(before) ? Ui::kNoElement : idArg(before, "insertBefore", "before");
            state->mBackend->insertBefore(p, c, b);
            state->mDirty = true;
        };
        t["detach"] = [state](sol::object child) {
            state->mBackend->detach(idArg(child, "detach", "child"));
            state->mDirty = true;
        };
        t["setText"] = [state](sol::object id, sol::object text) {
            const Ui::ElementId i = idArg(id, "setText", "id");
            state->mBackend->setText(i, stringArg(text, "setText", "text"));
            state->mDirty = true;
        };
        t["setClass"] = [state](sol::object id, sol::object classes) {
            const Ui::ElementId i = idArg(id, "setClass", "id");
            state->mBackend->setClass(i, stringArg(classes, "setClass", "classes"));
            state->mDirty = true;
        };
        t["setProperty"] = [state](sol::object id, sol::object name, sol::object value) {
            const Ui::ElementId i = idArg(id, "setProperty", "id");
            const std::string_view n = stringArg(name, "setProperty", "name");
            state->mBackend->setProperty(i, n, stringArg(value, "setProperty", "value"));
            state->mDirty = true;
        };
        t["setAttribute"] = [state](sol::object id, sol::object name, sol::object value) {
            const Ui::ElementId i = idArg(id, "setAttribute", "id");
            const std::string_view n = stringArg(name, "setAttribute", "name");
            state->mBackend->setAttribute(i, n, stringArg(value, "setAttribute", "value"));
            state->mDirty = true;
        };
        t["listen"] = [state](sol::object id, sol::object kind, sol::object on) {
            const Ui::ElementId i = idArg(id, "listen", "id");
            const auto k = Ui::eventKindFromName(stringArg(kind, "listen", "kind"));
            if (!k)
                fail("listen", "unknown event kind");
            if (on.get_type() != sol::type::boolean)
                fail("listen", "on must be a boolean");
            state->mBackend->listen(i, *k, on.as<bool>());
            state->mDirty = true;
        };
        t["focus"] = [state](sol::object id) {
            state->mBackend->focus(idArg(id, "focus", "id"));
            state->mDirty = true;
        };
        t["commit"] = [state]() {
            if (!state->mDirty)
                return;
            state->mDirty = false;
            state->mBackend->commit();
        };
        t["poll"] = [state](sol::object handler) {
            if (handler.get_type() != sol::type::function)
                fail("poll", "handler must be a function");
            std::vector<Ui::Event> events;
            events.swap(state->mEvents);
            events.clear();
            state->mBackend->takeEvents(events);
            if (events.empty())
            {
                state->mEvents.swap(events); // keep the capacity
                return;
            }
            sol::protected_function fn(handler);
            std::string error;
            for (const Ui::Event& event : events)
            {
                sol::protected_function_result result
                    = fn(static_cast<double>(event.mTarget), Ui::eventKindName(event.mKind), event.mValue);
                if (!result.valid() && error.empty())
                {
                    sol::error e = result;
                    error = e.what();
                }
            }
            events.clear();
            state->mEvents.swap(events);
            if (!error.empty())
                fail("poll", error.c_str());
        };
        return t;
    }

    sol::table makeBackendPackage(sol::state_view lua, BackendOpener opener)
    {
        sol::table package(lua, sol::create);
        package["open"] = [lua, opener = std::move(opener)]() mutable {
            std::shared_ptr<Ui::Backend> backend = opener ? opener() : nullptr;
            if (backend == nullptr)
                throw std::runtime_error("openeden.uibackend.open: no UI backend is available");
            return makeSessionTable(lua, std::move(backend));
        };
        return package;
    }
}
