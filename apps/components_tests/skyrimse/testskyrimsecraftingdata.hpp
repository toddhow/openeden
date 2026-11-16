#ifndef OPENEDEN_TEST_SKYRIMSE_CRAFTING_DATA_H
#define OPENEDEN_TEST_SKYRIMSE_CRAFTING_DATA_H

// Task 0199: a generated test.esp with ingredients, items, recipes, enchantments, soul gems, keywords and benches.
// No game data. Shared by the crafting tests.

#include <game_skyrimse/crafting.hpp>

#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace CraftingTestData
{
    using namespace ContentTests;
    using game_skyrimse::FormId;

    constexpr FormId kSelf = 0x01000000;
    // effects (MGEF)
    constexpr FormId kEffHeal = kSelf | 0x500;
    constexpr FormId kEffFortify = kSelf | 0x501;
    constexpr FormId kEffDamage = kSelf | 0x502; // hostile
    constexpr FormId kEffSlow = kSelf | 0x503;
    constexpr FormId kEffInvis = kSelf | 0x504;
    constexpr FormId kEffBreath = kSelf | 0x505;
    constexpr FormId kEffFire = kSelf | 0x506; // the fire damage of the enchantments
    // ingredients
    constexpr FormId kIngA = kSelf | 0x600; // Heal 5/0, Fortify 10/60, Damage 2/0, Slow 1/10
    constexpr FormId kIngB = kSelf | 0x601; // Heal 4/0, Fortify 12/30, Invis 0/30, Breath 0/60
    constexpr FormId kIngC = kSelf | 0x602; // Damage 3/0, Slow 2/5
    constexpr FormId kIngD = kSelf | 0x603; // Invis 0/20 (shares only with B)
    // items
    constexpr FormId kSword = kSelf | 0x700;
    constexpr FormId kEnchSword = kSelf | 0x701; // carries kEnchFire03 from its record
    constexpr FormId kArmor = kSelf | 0x702;
    constexpr FormId kIngot = kSelf | 0x703;
    constexpr FormId kLeather = kSelf | 0x704;
    constexpr FormId kGemFull = kSelf | 0x710; // capacity 5, soul 5
    constexpr FormId kGemEmpty = kSelf | 0x711; // capacity 5, soul 0
    constexpr FormId kGemPetty = kSelf | 0x712; // capacity 1, soul 1
    // enchantments
    constexpr FormId kEnchFireBase = kSelf | 0x800;
    constexpr FormId kEnchFire03 = kSelf | 0x801;
    // recipes (COBJ)
    constexpr FormId kRecForge = kSelf | 0x900; // sword at the forge: 2 ingots, 1 leather
    constexpr FormId kRecCond = kSelf | 0x901; // the same with a condition
    constexpr FormId kRecTemperSword = kSelf | 0x902; // sharpening wheel: 1 ingot
    constexpr FormId kRecTemperArmor = kSelf | 0x903; // armour table: 1 leather
    constexpr FormId kRecSky = kSelf | 0x904; // skyforge: the armour, 3 ingots
    constexpr FormId kRecBad = kSelf | 0x905; // forge: result is not an item
    // keywords
    constexpr FormId kKwForge = kSelf | 0xA00;
    constexpr FormId kKwSky = kSelf | 0xA01;
    constexpr FormId kKwWheel = kSelf | 0xA02;
    constexpr FormId kKwArmorTable = kSelf | 0xA03;
    constexpr FormId kKwAlchemy = kSelf | 0xA04;
    constexpr FormId kKwEnchanting = kSelf | 0xA05;
    constexpr FormId kKwOther = kSelf | 0xA06;
    // furniture
    constexpr FormId kFurnForge = kSelf | 0xB00;
    constexpr FormId kFurnSky = kSelf | 0xB01; // forge and skyforge
    constexpr FormId kFurnWheel = kSelf | 0xB02;
    constexpr FormId kFurnArmor = kSelf | 0xB03;
    constexpr FormId kFurnAlchemy = kSelf | 0xB04;
    constexpr FormId kFurnEnchant = kSelf | 0xB05;
    constexpr FormId kFurnChair = kSelf | 0xB06; // no crafting keyword

    inline game_skyrimse::LoadOrder order()
    {
        game_skyrimse::LoadOrder o;
        std::string err;
        o.add("Skyrim.esm", false, err);
        o.add("test.esp", false, err);
        return o;
    }

    inline Bytes edid(const char* name)
    {
        return sub4("EDID", Bytes().zstring(name));
    }

    inline Bytes effect(FormId id, float magnitude, std::uint32_t duration)
    {
        Bytes b = sub4("EFID", Bytes().u32(id));
        b.append(sub4("EFIT", Bytes().f32(magnitude).u32(0).u32(duration)));
        return b;
    }

    inline Bytes kwda(std::vector<FormId> ids)
    {
        Bytes b;
        b.append(sub4("KSIZ", Bytes().u32(static_cast<std::uint32_t>(ids.size()))));
        Bytes list;
        for (const FormId id : ids)
            list.u32(id);
        b.append(sub4("KWDA", list));
        return b;
    }

    inline Bytes grp(const char (&type)[5], const Bytes& records)
    {
        return group4(Content::fourCC(type), 0, records);
    }

    inline Bytes cobj(FormId id, FormId result, std::uint16_t count, FormId bench,
        std::vector<std::pair<FormId, int>> items, int conditions = 0)
    {
        Bytes d;
        d.append(sub4("COCT", Bytes().u32(static_cast<std::uint32_t>(items.size()))));
        for (const auto& [item, n] : items)
            d.append(sub4("CNTO", Bytes().u32(item).u32(static_cast<std::uint32_t>(n))));
        for (int i = 0; i < conditions; ++i)
            d.append(sub4("CTDA", Bytes().u32(0).u32(0).u32(0).u32(0).u32(0).u32(0).u32(0).u32(0)));
        d.append(sub4("CNAM", Bytes().u32(result)));
        d.append(sub4("BNAM", Bytes().u32(bench)));
        d.append(sub4("NAM1", Bytes().u16(count)));
        return record4("COBJ", 0, id, d);
    }

    inline Bytes ingredient(FormId id, std::vector<std::tuple<FormId, float, std::uint32_t>> effects)
    {
        Bytes d;
        d.append(sub4("DATA", Bytes().u32(4).f32(0.1f)));
        d.append(sub4("ENIT", Bytes().u32(4).u32(0)));
        for (const auto& [e, m, dur] : effects)
            d.append(effect(e, m, dur));
        return record4("INGR", 0, id, d);
    }

    inline Bytes enchRecord(FormId id, const char* name, FormId base, float magnitude, std::uint32_t enchantType = 6)
    {
        Bytes d;
        d.append(edid(name));
        d.append(sub4("ENIT", Bytes().u32(11).u32(0).u32(1).u32(11).u32(1).u32(enchantType).f32(0.f).u32(base).u32(0)));
        d.append(effect(kEffFire, magnitude, 0));
        return record4("ENCH", 0, id, d);
    }

    inline Bytes furn(FormId id, const char* name, std::vector<FormId> keywords)
    {
        Bytes d;
        d.append(edid(name));
        if (!keywords.empty())
            d.append(kwda(std::move(keywords)));
        return record4("FURN", 0, id, d);
    }

    inline Bytes testPlugin()
    {
        Bytes all = tes4Header(1.7f, 0, "A", "B", 40, { { "Skyrim.esm", 0 } }, 24, 44);
        const auto mgef = [](FormId id, const char* name) { return record4("MGEF", 0, id, edid(name)); };
        Bytes mg;
        mg.append(mgef(kEffHeal, "AlchRestoreHealth"));
        mg.append(mgef(kEffFortify, "AlchFortifyHealth"));
        mg.append(mgef(kEffDamage, "AlchDamageHealth"));
        mg.append(mgef(kEffSlow, "AlchSlow"));
        mg.append(mgef(kEffInvis, "AlchInvisibility"));
        mg.append(mgef(kEffBreath, "AlchWaterbreathing"));
        mg.append(mgef(kEffFire, "FireDamageFFContact"));
        all.append(grp("MGEF", mg));
        Bytes in;
        in.append(ingredient(
            kIngA, { { kEffHeal, 5.f, 0 }, { kEffFortify, 10.f, 60 }, { kEffDamage, 2.f, 0 }, { kEffSlow, 1.f, 10 } }));
        in.append(ingredient(kIngB,
            { { kEffHeal, 4.f, 0 }, { kEffFortify, 12.f, 30 }, { kEffInvis, 0.f, 30 }, { kEffBreath, 0.f, 60 } }));
        in.append(ingredient(kIngC, { { kEffDamage, 3.f, 0 }, { kEffSlow, 2.f, 5 } }));
        in.append(ingredient(kIngD, { { kEffInvis, 0.f, 20 } }));
        all.append(grp("INGR", in));
        Bytes weapons;
        weapons.append(record4("WEAP", 0, kSword, sub4("DATA", Bytes().u32(25).f32(9.f).u16(7))));
        Bytes enchanted = sub4("DATA", Bytes().u32(60).f32(9.f).u16(7));
        enchanted.append(sub4("EITM", Bytes().u32(kEnchFire03)));
        weapons.append(record4("WEAP", 0, kEnchSword, enchanted));
        all.append(grp("WEAP", weapons));
        all.append(grp("ARMO", record4("ARMO", 0, kArmor, sub4("DATA", Bytes().u32(100).f32(10.f)))));
        Bytes misc;
        misc.append(record4("MISC", 0, kIngot, sub4("DATA", Bytes().u32(5).f32(1.f))));
        misc.append(record4("MISC", 0, kLeather, sub4("DATA", Bytes().u32(3).f32(0.5f))));
        misc.append(record4("MISC", 0, 0x0000000F, sub4("DATA", Bytes().u32(1).f32(0.f))));
        all.append(grp("MISC", misc));
        Bytes gems;
        gems.append(record4("SLGM", 0, kGemFull,
            sub4("DATA", Bytes().u32(500).f32(0.5f))
                .append(sub4("SLCP", Bytes().u8(5)))
                .append(sub4("SOUL", Bytes().u8(5)))));
        gems.append(record4("SLGM", 0, kGemEmpty,
            sub4("DATA", Bytes().u32(200).f32(0.5f))
                .append(sub4("SLCP", Bytes().u8(5)))
                .append(sub4("SOUL", Bytes().u8(0)))));
        gems.append(record4("SLGM", 0, kGemPetty,
            sub4("DATA", Bytes().u32(40).f32(0.1f))
                .append(sub4("SLCP", Bytes().u8(1)))
                .append(sub4("SOUL", Bytes().u8(1)))));
        all.append(grp("SLGM", gems));
        Bytes en;
        en.append(enchRecord(kEnchFireBase, "EnchWeaponFireDamageBase", 0, 10.f));
        en.append(enchRecord(kEnchFire03, "EnchWeaponFireDamage03", kEnchFireBase, 15.f));
        all.append(grp("ENCH", en));
        Bytes kw;
        kw.append(record4("KYWD", 0, kKwForge, edid("CraftingSmithingForge")));
        kw.append(record4("KYWD", 0, kKwSky, edid("CraftingSmithingSkyforge")));
        kw.append(record4("KYWD", 0, kKwWheel, edid("CraftingSmithingSharpeningWheel")));
        kw.append(record4("KYWD", 0, kKwArmorTable, edid("CraftingSmithingArmorTable")));
        kw.append(record4("KYWD", 0, kKwAlchemy, edid("WICraftingAlchemy")));
        kw.append(record4("KYWD", 0, kKwEnchanting, edid("WICraftingEnchanting")));
        kw.append(record4("KYWD", 0, kKwOther, edid("VendorItemFood")));
        all.append(grp("KYWD", kw));
        Bytes fu;
        fu.append(furn(kFurnForge, "TestForge", { kKwForge, kKwOther }));
        fu.append(furn(kFurnSky, "TestSkyforge", { kKwForge, kKwSky }));
        fu.append(furn(kFurnWheel, "TestWheel", { kKwWheel }));
        fu.append(furn(kFurnArmor, "TestArmorTable", { kKwArmorTable }));
        fu.append(furn(kFurnAlchemy, "TestAlchemy", { kKwAlchemy }));
        fu.append(furn(kFurnEnchant, "TestEnchanter", { kKwEnchanting }));
        fu.append(furn(kFurnChair, "TestChair", { kKwOther }));
        all.append(grp("FURN", fu));
        Bytes co;
        co.append(cobj(kRecForge, kSword, 1, kKwForge, { { kIngot, 2 }, { kLeather, 1 } }));
        co.append(cobj(kRecCond, kSword, 1, kKwForge, { { kIngot, 2 } }, 2));
        co.append(cobj(kRecTemperSword, kSword, 1, kKwWheel, { { kIngot, 1 } }));
        co.append(
            cobj(kRecTemperArmor, kArmor, 1, kKwArmorTable, { { kLeather, 1 } }, 2)); // like the game's: two conditions
        co.append(cobj(kRecSky, kArmor, 1, kKwSky, { { kIngot, 3 } }));
        co.append(cobj(kRecBad, kSelf | 0xFFF, 1, kKwForge, { { kIngot, 1 } }));
        all.append(grp("COBJ", co));
        return all;
    }

    inline std::unique_ptr<Content::Plugin> openPlugin(const Bytes& bytes)
    {
        Content::Error error;
        return Content::Plugin::open(
            std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
    }
}

#endif
