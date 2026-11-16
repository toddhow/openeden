#ifndef OPENEDEN_GAME_SKYRIMSE_COMBAT_IMPACT_H
#define OPENEDEN_GAME_SKYRIMSE_COMBAT_IMPACT_H

// Skyrim SE combat sounds, the records and the pure rules (task 0193, milestone M-S5 stage S5-10 "impacts and sounds";
// research note 0056 sections 5 and 8). Pure: no engine, renderer, Jolt, Qt or OSG type, no clock, no random numbers.
//
// What the data says (the five official masters, a separate Python survey of 2026-11-15, aggregates only):
//  - IPCT (663 impacts): SNAM (4 bytes, 650 records) is the first sound, NAM1 (4 bytes, 298) a second one; each is a
//  SNDR
//    (592 and 273) or a SOUN (58 and 25). DATA is 24 bytes and is not read (effect timing and placement of the decal).
//  - IPDS (313 impact data sets): repeated PNAM of 8 bytes, 18,184 in all: a MATT (material) form id and an IPCT form
//  id.
//    A set holds up to 77 materials. A MATT is only a name here (79 records: MaterialSkin, MaterialStone, ...).
//  - WEAP: INAM (3,164 of 3,262) names the IPDS used when the weapon hits something, BIDS (3,208) the IPDS of a bash,
//    BAMT (3,238) the material the weapon counts as when it BLOCKS (MaterialBlockBlade1Hand, ...), TNAM (3,178) a SNDR
//    named WPNSwing... (the swing), NAM9 and NAM8 (3,241 each) the draw and sheathe SNDRs (WPNBlade1HandDrawSD,
//    WPNBlade1HandSheatheSD), SNAM (49, all crossbows and dart traps) the attack sound (WPNCrossbowFire).
//  - RACE: NAM4 (160 of 161) is the body's MATT (NordRace: MaterialSkin, WolfRace: MaterialSkinSmall, DragonRace:
//    MaterialDragon) and NAM5 (151) the IPDS the race's bare hands use (WPNzUnarmedImpactSet).
//  - PROJ names no sound at all, and no record names the bow's release sound: the bow table below uses SNDR editor ids
//    that exist in the data (WPNBowFireSD, WPNBowNockSD), a stand-in for the behaviour graph that plays them in the
//    game.
// Which of these a hit uses is the rule in `impactSoundFor` below: the attacker's INAM (or the unarmed race's NAM5),
// the
// victim's material, the impact for that pair and its SNAM (and NAM1 when the victim is the player: RECALLED-VERIFY,
// the
// data names the second sound of the arrow flesh impact WPNImpactArrowVsFleshPLAYER).
//
// Hostile input: sizes are checked against the bytes that remain; a subrecord of another size than the layout allows is
// counted in DecodeNotes::mOddBy and decodes to nothing; nothing throws.

#include "game_skyrimse/land.hpp"
#include "game_skyrimse/weather.hpp"
#include "game_skyrimse/weather_source.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace game_skyrimse
{
    /// An IPCT: the sounds an impact plays. DATA is not read.
    struct ImpactRecord
    {
        RecordCommon mCommon;
        FormId mSound1 = kNullFormId; ///< SNAM, a SNDR or SOUN
        FormId mSound2 = kNullFormId; ///< NAM1
    };

    /// One entry of an IPDS: "when it hits this material, use this impact".
    struct ImpactPair
    {
        FormId mMaterial = kNullFormId;
        FormId mImpact = kNullFormId;
    };

    /// An IPDS (impact data set), the PNAM entries in file order. At most kMaxImpactPairs are kept.
    struct ImpactSetRecord
    {
        RecordCommon mCommon;
        std::vector<ImpactPair> mPairs;
    };

    /// A MATT: only its name matters here.
    struct MaterialRecord
    {
        RecordCommon mCommon;
    };

    /// A SOUN (the older sound record). In the five masters EVERY SOUN has an SDSC (4 bytes) naming the SNDR that
    /// really
    /// plays (1,165 of 1,165; 83 of the 83 IPCT sounds that name a SOUN), and its FNAM is a folder at best, so a SOUN
    /// is
    /// only a pointer here.
    struct SoundRedirectRecord
    {
        RecordCommon mCommon;
        FormId mDescriptor = kNullFormId; ///< SDSC
    };

    /// The most pairs one impact data set keeps (the data's largest has 77).
    inline constexpr std::size_t kMaxImpactPairs = 512;

    [[nodiscard]] bool decodeImpact(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, ImpactRecord& out, std::string& why);
    [[nodiscard]] bool decodeImpactSet(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, ImpactSetRecord& out, std::string& why);
    [[nodiscard]] bool decodeMaterial(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, MaterialRecord& out, std::string& why);

    [[nodiscard]] bool decodeSoundRedirect(std::span<const std::byte> bytes, const Content::RawRecord& record,
        const DecodeContext& context, SoundRedirectRecord& out, std::string& why);

    /// What one plugin's walk found of the sound records (the weather module's SNDR and SOUN decoders are reused).
    struct SoundRecords
    {
        std::vector<ImpactRecord> mImpacts;
        std::vector<ImpactSetRecord> mImpactSets;
        std::vector<MaterialRecord> mMaterials;
        std::vector<SoundDescriptorRecord> mSounds; ///< SNDR
        std::vector<SoundRedirectRecord> mRedirects; ///< SOUN
    };

    /// The sound records of a load order by form id (later plugins override whole records, a deleted one goes).
    class SoundLibrary
    {
    public:
        void add(const SoundRecords& records);
        [[nodiscard]] const ImpactRecord* impact(FormId id) const;
        [[nodiscard]] const ImpactSetRecord* impactSet(FormId id) const;
        /// The SNDR for a sound form: a SNDR is itself, a SOUN is the SNDR its SDSC names (one hop), anything else
        /// null.
        [[nodiscard]] const SoundDescriptorRecord* sound(FormId id) const;
        /// The MATT or SNDR with this editor id (case-insensitive), or 0.
        [[nodiscard]] FormId materialByEditorId(std::string_view editorId) const;
        [[nodiscard]] FormId soundByEditorId(std::string_view editorId) const;
        [[nodiscard]] FormId impactSetByEditorId(std::string_view editorId) const;
        /// The impact the set gives for the material (the first matching pair), or 0.
        [[nodiscard]] FormId impactFor(FormId impactSet, FormId material) const;
        [[nodiscard]] std::size_t impactCount() const { return mImpacts.size(); }
        [[nodiscard]] std::size_t impactSetCount() const { return mSets.size(); }
        [[nodiscard]] std::size_t materialCount() const { return mMaterials.size(); }
        [[nodiscard]] std::size_t soundCount() const { return mSounds.size(); }

    private:
        std::unordered_map<FormId, ImpactRecord> mImpacts;
        std::unordered_map<FormId, ImpactSetRecord> mSets;
        std::unordered_map<FormId, MaterialRecord> mMaterials;
        std::unordered_map<FormId, SoundDescriptorRecord> mSounds;
        std::unordered_map<FormId, FormId> mRedirects;
        std::unordered_map<std::string, FormId> mMaterialNames; ///< lower case editor id
        std::unordered_map<std::string, FormId> mSoundNames;
        std::unordered_map<std::string, FormId> mSetNames;
    };

    /// Tells whether a VFS path exists (the session's VFS, through the view). Empty: every file is taken to exist.
    using SoundFileProbe = std::function<bool(const std::string&)>;

    /// A sound ready to play: its descriptor's editor id, the VFS path of the file chosen and the gain.
    struct ResolvedSound
    {
        std::string mName;
        std::string mFile;
        float mVolume = 1.f;
    };

    /// Every usable file of a descriptor (the ones the probe finds) with its name and gain, for a cache.
    struct ResolvedSoundSet
    {
        std::string mName;
        std::vector<std::string> mFiles; ///< VFS paths, in the record's order
        float mVolume = 1.f;
    };

    [[nodiscard]] bool resolveSoundSet(
        const SoundLibrary& library, FormId sound, const SoundFileProbe& exists, ResolvedSoundSet& out);

    /// Picks a file of the descriptor (the files that exist, taken in turn by `counter`: no random number) and its gain
    /// (soundGain of the BNAM attenuation). False for an unknown descriptor or one with no usable file.
    [[nodiscard]] bool resolveSound(const SoundLibrary& library, FormId sound, std::uint32_t counter,
        const SoundFileProbe& exists, ResolvedSound& out);

    /// The material an armoured target counts as: the body piece's armour type (ArmourPiece::mType: 0 light, 1 heavy;
    /// the
    /// data has MaterialArmorLight and MaterialArmorHeavy in the weapons' impact sets) when it wears one, else the
    /// body's own material. RECALLED-VERIFY: that the engine picks the armour's material for a worn cuirass.
    /// `bodyArmourType` is -1 when no body piece is worn.
    [[nodiscard]] FormId targetMaterial(
        const SoundLibrary& library, FormId bodyMaterial, int bodyArmourType, bool armourMaterials);

    /// The sounds a hit plays: the impact set's entry for the material, SNAM first and, when `second` is set, NAM1.
    struct ImpactSounds
    {
        FormId mImpact = kNullFormId;
        FormId mFirst = kNullFormId;
        FormId mSecond = kNullFormId;
    };

    [[nodiscard]] ImpactSounds impactSoundFor(
        const SoundLibrary& library, FormId impactSet, FormId material, bool includeSecond);

    /// Editor ids of the stand-ins: the SNDR a bow plays as the string is drawn and as the arrow leaves (no record
    /// names
    /// them), the bare hands' swing, the material a world surface counts as (an arrow that ends in a wall).
    inline constexpr std::string_view kBowNockSound = "WPNBowNockSD";
    inline constexpr std::string_view kBowFireSound = "WPNBowFireSD";
    inline constexpr std::string_view kUnarmedSwingSound = "WPNSwingUnarmed";
    inline constexpr std::string_view kWorldHitMaterial = "MaterialStone";
    /// The player has no race record: its body and bare hands are a Nord's (NordRace NAM4 and NAM5 in the data).
    inline constexpr std::string_view kPlayerBodyMaterial = "MaterialSkin";
    inline constexpr std::string_view kPlayerUnarmedImpactSet = "WPNzUnarmedImpactSet";
    inline constexpr std::string_view kArmourMaterialLight = "MaterialArmorLight";
    inline constexpr std::string_view kArmourMaterialHeavy = "MaterialArmorHeavy";
}

#endif
