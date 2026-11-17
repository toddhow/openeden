#include "game_skyrimse/location_conditions.hpp"

#include "game_skyrimse/story.hpp"

namespace game_skyrimse
{
    namespace
    {
        // 563 LocationHasRefType(location reference type): the candidate location lists a special reference of the type
        float locationHasRefType(
            const survey::Ctda& c, conditions::Subject subject, const conditions::ConditionWorld& world)
        {
            const LocationSubjectSource* source = dynamic_cast<const LocationSubjectSource*>(&world);
            if (source == nullptr || subject == 0)
                return 0.f;
            const FormId location = source->locationSubject();
            if (location == kNullFormId)
                return 0.f;
            return source->locationHasRefType(location, source->resolveLocationForm(c.mParam1)) ? 1.f : 0.f;
        }

        // 565 GetIsEditorLocation(location): the candidate is that location or inside it
        float getIsEditorLocation(
            const survey::Ctda& c, conditions::Subject subject, const conditions::ConditionWorld& world)
        {
            const LocationSubjectSource* source = dynamic_cast<const LocationSubjectSource*>(&world);
            if (source == nullptr || subject == 0)
                return 0.f;
            const FormId location = source->locationSubject();
            if (location == kNullFormId)
                return 0.f;
            return source->locationWithin(location, source->resolveLocationForm(c.mParam1)) ? 1.f : 0.f;
        }

        // 181 HasSameEditorLocAsRefAlias(alias, keyword): the candidate location and the editor location of the alias's
        // reference have the same nearest ancestor-or-self carrying the keyword
        float hasSameEditorLocAsRefAlias(
            const survey::Ctda& c, conditions::Subject subject, const conditions::ConditionWorld& world)
        {
            const LocationSubjectSource* source = dynamic_cast<const LocationSubjectSource*>(&world);
            if (source == nullptr || subject == 0)
                return 0.f;
            const FormId location = source->locationSubject();
            if (location == kNullFormId)
                return 0.f;
            const FormId keyword = source->resolveLocationForm(c.mParam2);
            const FormId reference = source->aliasReference(c.mParam1);
            if (keyword == kNullFormId || reference == kNullFormId)
                return 0.f;
            const FormId referenceLocation = source->editorLocationOfReference(reference);
            if (referenceLocation == kNullFormId)
                return 0.f;
            const FormId mine = source->locationAncestorWithKeyword(location, keyword);
            return mine != kNullFormId && mine == source->locationAncestorWithKeyword(referenceLocation, keyword) ? 1.f
                                                                                                                  : 0.f;
        }

        // 567 GetIsEditorLocAlias(alias): the editor location of the subject is the location the alias holds or lies
        // inside it (task 0230). Data: 416 uses, all `== 1`, run on the subject, in quest aliases (414) and packages
        // (2); the Jarl, Steward and Innkeeper aliases of the bounty quests pair it with a faction test and the Hold or
        // Location alias, and the actors' own XLCN is the city, not the hold, so "inside" is what the data needs. The
        // meaning is RECALLED-VERIFY (the Creation Kit wiki is unreachable; xEdit names the function and its alias).
        float getIsEditorLocAlias(
            const survey::Ctda& c, conditions::Subject subject, const conditions::ConditionWorld& world)
        {
            const LocationSubjectSource* source = dynamic_cast<const LocationSubjectSource*>(&world);
            if (source == nullptr || subject == 0 || source->locationSubject() != kNullFormId)
                return 0.f; // not an actor's condition (a candidate location is not an actor)
            const FormId aliasLocation = source->aliasReference(c.mParam1);
            if (aliasLocation == kNullFormId)
                return 0.f;
            const FormId mine = source->editorLocationOfSubject(subject);
            return mine != kNullFormId && source->locationWithin(mine, aliasLocation) ? 1.f : 0.f;
        }
    }

    const std::vector<conditions::ExtraFunction>& locationExtraFunctions()
    {
        static const std::vector<conditions::ExtraFunction> table
            = { { 563, &locationHasRefType }, { 565, &getIsEditorLocation } };
        return table;
    }

    const std::vector<conditions::ExtraFunction>& aliasLocationExtraFunctions()
    {
        static const std::vector<conditions::ExtraFunction> table
            = { { 181, &hasSameEditorLocAsRefAlias }, { 567, &getIsEditorLocAlias } };
        return table;
    }

    const std::vector<conditions::ExtraFunction>& storyAndLocationExtraFunctions()
    {
        static const std::vector<conditions::ExtraFunction> table = [] {
            std::vector<conditions::ExtraFunction> all = storyExtraFunctions();
            all.insert(all.end(), locationExtraFunctions().begin(), locationExtraFunctions().end());
            return all;
        }();
        return table;
    }
}
