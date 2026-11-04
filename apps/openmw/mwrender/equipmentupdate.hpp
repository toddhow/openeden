#ifndef OPENMW_MWRENDER_EQUIPMENTUPDATE_H
#define OPENMW_MWRENDER_EQUIPMENTUPDATE_H

namespace MWRender
{
    // Task 0074d (review of 0074b, finding 1): an actor's equipment change or rebuild (Npc::updateEquipment,
    // Npc::rebuild, WieldingCreature::updateEquipment) runs `update`, which attaches nodes, then `compile`, also when
    // `update` throws: the nodes it attached before the throw are in the scene and an uncompiled node must not be
    // drawn. The exception is then passed on unchanged. A part's model read can throw: vsgUtil::readNode reads through
    // vsgAdapters::vfs, whose VFS::Manager::get opens the file outside the readers' own catches (an Eden Archive entry
    // that fails its check or is too large, an I/O error of a BSA or a loose file); a missing file, or one no reader
    // accepts, gets Resource::Fallback's node instead.
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
