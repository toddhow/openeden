#ifndef OPENEDEN_COMPONENTS_UI_LUA_BACKENDPACKAGE_H
#define OPENEDEN_COMPONENTS_UI_LUA_BACKENDPACKAGE_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

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
    ///   setUpdater(fn)   (task 0164: the engine calls fn once a frame, see UpdateRegistry; call it once per session)
    ///
    /// Ids are numbers. A bad argument raises a Lua error naming the function. `poll` with no events and `commit` with
    /// nothing written allocate nothing. The session lives as long as its table (it is released by Lua's collector).
    ///
    /// `opener` is called by `open()` on the Lua thread; it returns the session's backend (a null pointer makes
    /// `open()` raise an error). Owner of the result: Lua.
    using BackendOpener = std::function<std::shared_ptr<Ui::Backend>()>;

    /// Task 0164: the engine-side per-frame hook. A session's `setUpdater(fn)` (the library registers its `ui.update`
    /// when it is required) puts `fn` here, and the engine calls `runAll` once a frame on the Lua thread, so a script
    /// need not call `ui.update()` itself (a manual call stays harmless: a second flush in one frame has nothing to do).
    /// Updaters run in registration order. A failing one is reported through `onError` the first time and again only
    /// when its message changes; it never stops the others. `clear` forgets every updater (the engine calls it when
    /// scripts are unloaded: an updater holds its session's Lua function). Main (Lua) thread only.
    class UpdateRegistry
    {
    public:
        UpdateRegistry();
        ~UpdateRegistry();
        UpdateRegistry(const UpdateRegistry&) = delete;
        UpdateRegistry& operator=(const UpdateRegistry&) = delete;

        void runAll(const std::function<void(const std::string&)>& onError);
        void clear();
        std::size_t size() const;

        /// For the session table: adds an updater (a Lua function, held as a protected function).
        void add(sol::object function);

    private:
        struct Entry;
        std::vector<Entry> mEntries;
    };

    /// With a registry, sessions of this package register their updaters in it; without one `setUpdater` is accepted
    /// and forgotten.
    sol::table makeBackendPackage(
        sol::state_view lua, BackendOpener opener, std::shared_ptr<UpdateRegistry> registry = nullptr);

    /// The session table for one backend (what `open()` returns); exposed for tests.
    sol::table makeSessionTable(
        sol::state_view lua, std::shared_ptr<Ui::Backend> backend, std::shared_ptr<UpdateRegistry> registry = nullptr);
}

#endif
