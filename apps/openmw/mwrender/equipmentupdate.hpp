#ifndef OPENMW_MWRENDER_EQUIPMENTUPDATE_H
#define OPENMW_MWRENDER_EQUIPMENTUPDATE_H

namespace MWRender
{
    // Task 0074d (review of 0074b, finding 1): an actor's equipment change or rebuild (Npc::updateEquipment,
    // Npc::runRebuild, WieldingCreature::updateEquipment, through Npc::runCompiledUpdate and
    // WieldingCreature::runCompiledUpdate: task 0074e, finding 5) runs `update`, which attaches nodes, then `compile`,
    // also when `update` throws: the nodes it attached before the throw are in the scene and an uncompiled node must
    // not be drawn. The exception is then passed on unchanged. A part's model read can throw: vsgUtil::readNode reads
    // through vsgAdapters::vfs, whose VFS::Manager::get opens the file outside the readers' own catches (an Eden
    // Archive entry that fails its check or is too large, an I/O error of a BSA or a loose file); a missing file, or
    // one no reader accepts, gets Resource::Fallback's node instead.
    //
    // Since task 0074e (finding 7) a std::exception from a single part's model read is caught where the part is read
    // (Npc::insertBoundedPart, WieldingCreature::addItemPart), as OpenMW 0.51 does, and never gets here. What still
    // does: the ammunition's, holstered items' and quiver's reads, and a non-std::exception. The callers differ in
    // what happens to such an exception: a Lua delayed action is caught and logged (LuaManager::DelayedAction::apply);
    // mwscript puts the script's target in mInactive, so its script stays off for the session (ScriptManager::run); the
    // inventory, barter and container windows lose the rest of the frame's input (Engine::frame's InputManager::update
    // catch); and a mechanics-driven change (torch auto-equip at dusk, AI combat's ActionEquip, spell effects, race
    // changes, werewolf and vampire rebuilds) has no catch before Game::run's (game.hpp: "Emergency Save", then
    // Engine::frame pops the Game state): the session ends.
    template <class Update, class Compile>
    void runEquipmentUpdate(Update&& update, Compile&& compile)
    {
        try
        {
            update();
        }
        catch (...)
        {
            compile();
            throw;
        }
        compile();
    }
}

#endif
