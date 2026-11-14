#ifndef OPENEDEN_COMPONENTS_UI_LUA_BACKENDPACKAGE_H
#define OPENEDEN_COMPONENTS_UI_LUA_BACKENDPACKAGE_H

#include <functional>
#include <memory>

#include <sol/forward.hpp>

#include <components/ui/elementprotocol.hpp>

namespace UiLua
{
    /// The Lua face of a `Ui::Backend` (task 0162): the package `openeden.uibackend`, which only the library
    /// `openeden.ui` is meant to call. It holds one function, `open()`, that makes a new session and returns a table of
    /// plain functions over it:
    ///
    ///   createDocument(name) -> id        create(tag) -> id          destroy(id)
    ///   insertBefore(parent, child [, before])                        detach(child)
    ///   setText(id, s)  setClass(id, s)  setProperty(id, name, s)    setAttribute(id, name, s)
    ///   listen(id, kind, on)   focus(id)   commit()   poll(fn)       (fn(id, kindName, value) per event)
    ///
    /// Ids are numbers. A bad argument raises a Lua error naming the function. `poll` with no events and `commit` with
    /// nothing written allocate nothing. The session lives as long as its table (it is released by Lua's collector).
    ///
    /// `opener` is called by `open()` on the Lua thread; it returns the session's backend (a null pointer makes
    /// `open()` raise an error). Owner of the result: Lua.
    using BackendOpener = std::function<std::shared_ptr<Ui::Backend>()>;

    sol::table makeBackendPackage(sol::state_view lua, BackendOpener opener);

    /// The session table for one backend (what `open()` returns); exposed for tests.
    sol::table makeSessionTable(sol::state_view lua, std::shared_ptr<Ui::Backend> backend);
}

#endif
