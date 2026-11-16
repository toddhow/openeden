#include "game_skyrimse/location_conditions.hpp"

#include "game_skyrimse/story.hpp"

namespace game_skyrimse
{
    namespace
    {
        // 563 LocationHasRefType(location reference type): the candidate location lists a special reference of the type
        float locationHasRefType(const survey::Ctda& c, conditions::Subject subject, const conditions::ConditionWorld& world)
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
        float getIsEditorLocation(const survey::Ctda& c, conditions::Subject subject, const conditions::ConditionWorld& world)
        {
            const LocationSubjectSource* source = dynamic_cast<const LocationSubjectSource*>(&world);
            if (source == nullptr || subject == 0)
                return 0.f;
            const FormId location = source->locationSubject();
            if (location == kNullFormId)
                return 0.f;
            return source->locationWithin(location, source->resolveLocationForm(c.mParam1)) ? 1.f : 0.f;
        }
    }

    const std::vector<conditions::ExtraFunction>& locationExtraFunctions()
    {
        static const std::vector<conditions::ExtraFunction> table = { { 563, &locationHasRefType },
            { 565, &getIsEditorLocation } };
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
