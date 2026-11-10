#ifndef GAME_MWCLASS_CLASSES_H
#define GAME_MWCLASS_CLASSES_H

namespace MWClass
{
    /// Registers the classes a game pack provides (task 0098 move 8). The Morrowind pack supplies the world-object
    /// classes (door, container, activator, static, body part) and the item classes (armor, weapon, clothing, book,
    /// ingredient, potion, miscellaneous, light, lockpick, probe, repair, apparatus) and the actor classes (NPC,
    /// creature, the creature and item levelled lists); the executable installs this function through
    /// MWBase::RulesRegistry before the engine runs, so openmw-lib never names the pack.
    using ClassRegistrar = void (*)();

    /// Main thread, before registerClasses(). A second call replaces the first.
    void installClassRegistrar(ClassRegistrar registrar);

    void registerClasses();
    ///< register all known classes
}

#endif
