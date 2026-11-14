// Task 0117, stage E0: the aggregate check of the Skyrim SE exterior decoding on the player's own masters. It prints
// counts only (lines starting SKYRIMSE-LAND), never a record name, id or mod path, and writes nothing. Skipped when the
// environment is not set.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required)
//
// It settles the RECALLED-VERIFY items of docs/research/0053: the row/column orientation of LAND heights (shared edges
// of neighbouring cells), the quadrant numbering and the VTXT position layout (the same, for textures), the PNAM bits,
// the XCLW sentinels, the null ATXT textures and why Solstheim has 34,383 LAND records. The conclusions are asserted at
// the end so that a regression in the decoder shows.
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/worldspace.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace game_skyrimse;
    using Grid = std::pair<std::int32_t, std::int32_t>;

    const char* const kMasters[] = { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" };

    struct World
    {
        WorldSpaceRecord mRecord;
        std::map<Grid, LandRecord> mLands;
        std::map<Grid, ExteriorCellInfo> mCells;
        std::vector<FormId> mCellsWithoutGrid;
    };

    struct Merged
    {
        std::map<FormId, World> mWorlds;
        std::map<FormId, GroundTextureRecord> mLtex;
        std::map<FormId, TextureSetRecord> mTxst;
        std::map<FormId, WaterRecord> mWatr;
        std::uint64_t mErrors = 0, mWalkErrors = 0, mOdd = 0;
        std::map<std::string, std::uint64_t> mOddBy;
        std::uint64_t mLandSeen = 0, mLandParsed = 0, mLandWithoutCell = 0;
    };

    bool loadAll(const fs::path& data, Merged& merged)
    {
        LoadOrder order;
        std::string err;
        for (const char* name : kMasters)
            if (!order.add(name, false, err))
                return false;
        for (const char* name : kMasters)
        {
            ExteriorData d;
            Content::Error error;
            if (!parseExteriorFile(data / name, order, ExteriorLimits(), d, error))
            {
                std::cout << "SKYRIMSE-LAND plugin failed " << error.mMessage << "\n";
                ++merged.mWalkErrors;
                continue;
            }
            merged.mErrors += d.mErrors.size() + d.mErrorsDropped;
            merged.mOdd += d.mNotes.mOddSubrecords;
            for (const auto& [k, v] : d.mNotes.mOddBy)
                merged.mOddBy[k] += v;
            const auto count = [&](const char(&t)[5]) -> TypeCount& { return d.mCounts[Content::fourCC(t)]; };
            merged.mLandSeen += count("LAND").mSeen;
            merged.mLandParsed += count("LAND").mParsed;
            for (WorldSpaceRecord& w : d.mWorlds)
                merged.mWorlds[w.mCommon.mId].mRecord = w;
            for (ExteriorCellInfo& c : d.mCells)
            {
                if (c.mWorld == kNullFormId)
                    continue;
                if (c.mGrid)
                    merged.mWorlds[c.mWorld].mCells[{ c.mGrid->x, c.mGrid->y }] = c;
                else
                    merged.mWorlds[c.mWorld].mCellsWithoutGrid.push_back(c.mCommon.mId);
            }
            for (LandRecord& l : d.mLands)
            {
                if (!l.mGrid)
                {
                    ++merged.mLandWithoutCell;
                    continue;
                }
                merged.mWorlds[l.mWorld].mLands[{ l.mGrid->x, l.mGrid->y }] = l;
            }
            for (GroundTextureRecord& t : d.mGroundTextures)
                merged.mLtex[t.mCommon.mId] = t;
            for (TextureSetRecord& t : d.mTextureSets)
                merged.mTxst[t.mCommon.mId] = t;
            for (WaterRecord& t : d.mWaters)
                merged.mWatr[t.mCommon.mId] = t;
        }
        return true;
    }

    // The 33 values of one edge of the height grid, in file order.
    //   0: column 0 (index = row), 1: column 32, 2: row 0 (index = column), 3: row 32
    std::array<float, 33> edge(const LandRecord& l, int which)
    {
        std::array<float, 33> e{};
        for (int k = 0; k < 33; ++k)
        {
            switch (which)
            {
                case 0:
                    e[k] = l.mHeights[k * 33 + 0];
                    break;
                case 1:
                    e[k] = l.mHeights[k * 33 + 32];
                    break;
                case 2:
                    e[k] = l.mHeights[0 * 33 + k];
                    break;
                default:
                    e[k] = l.mHeights[32 * 33 + k];
                    break;
            }
        }
        return e;
    }

    bool agrees(const std::array<float, 33>& a, const std::array<float, 33>& b, bool reversed)
    {
        for (int k = 0; k < 33; ++k)
            if (std::abs(a[k] - b[reversed ? 32 - k : k]) > 0.01f)
                return false;
        return true;
    }

    struct EdgeCounts
    {
        std::uint64_t mPairs = 0;
        std::uint64_t mAgree[4][4][2] = {}; // [edge of A][edge of B][reversed]
    };

    EdgeCounts edgeStudy(const World& w, int dx, int dy)
    {
        EdgeCounts c;
        for (const auto& [g, a] : w.mLands)
        {
            const auto it = w.mLands.find({ g.first + dx, g.second + dy });
            if (it == w.mLands.end() || !a.mHasHeights || !it->second.mHasHeights)
                continue;
            ++c.mPairs;
            for (int i = 0; i < 4; ++i)
            {
                const auto ea = edge(a, i);
                for (int j = 0; j < 4; ++j)
                {
                    const auto eb = edge(it->second, j);
                    for (int r = 0; r < 2; ++r)
                        if (agrees(ea, eb, r != 0))
                            ++c.mAgree[i][j][r];
                }
            }
        }
        return c;
    }

    const char* const kEdgeNames[4] = { "col0", "col32", "row0", "row32" };

    std::string best(const EdgeCounts& c, std::uint64_t& bestCount, int& bi, int& bj, int& br)
    {
        bestCount = 0;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                for (int r = 0; r < 2; ++r)
                    if (c.mAgree[i][j][r] > bestCount)
                    {
                        bestCount = c.mAgree[i][j][r];
                        bi = i;
                        bj = j;
                        br = r;
                    }
        return std::string(kEdgeNames[bi]) + "->" + kEdgeNames[bj] + (br != 0 ? " reversed" : " forward");
    }

    TEST(SkyrimSeLandRealData, aggregates)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        Merged m;
        ASSERT_TRUE(loadAll(env, m));
        std::cout << "SKYRIMSE-LAND plugins " << std::size(kMasters) << " walk errors " << m.mWalkErrors
                  << " record errors " << m.mErrors << " odd subrecords " << m.mOdd << "\n";
        for (const auto& [k, v] : m.mOddBy)
            std::cout << "SKYRIMSE-LAND odd " << k << " " << v << "\n";
        std::cout << "SKYRIMSE-LAND land seen " << m.mLandSeen << " parsed " << m.mLandParsed << " without cell grid "
                  << m.mLandWithoutCell << "\n";
        std::cout << "SKYRIMSE-LAND worlds " << m.mWorlds.size() << " ltex " << m.mLtex.size() << " txst "
                  << m.mTxst.size() << " watr " << m.mWatr.size() << "\n";

        const World* tamriel = nullptr;
        const World* solstheim = nullptr;
        for (const auto& [id, w] : m.mWorlds)
        {
            if (w.mRecord.mCommon.mEditorId == "Tamriel")
                tamriel = &w;
            if (w.mRecord.mCommon.mEditorId == "DLC2SolstheimWorld")
                solstheim = &w;
        }
        ASSERT_NE(tamriel, nullptr);
        std::cout << "SKYRIMSE-LAND tamriel lands " << tamriel->mLands.size() << " cells " << tamriel->mCells.size()
                  << "\n";

        // ---- A. height orientation by shared edges -------------------------------------------------------------
        for (int dir = 0; dir < 2; ++dir)
        {
            const EdgeCounts c = edgeStudy(*tamriel, dir == 0 ? 1 : 0, dir == 0 ? 0 : 1);
            std::uint64_t n = 0;
            int bi = 0, bj = 0, br = 0;
            const std::string name = best(c, n, bi, bj, br);
            std::cout << "SKYRIMSE-LAND edges " << (dir == 0 ? "east" : "north") << " pairs " << c.mPairs << " best "
                      << name << " agree " << n << "\n";
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j)
                    for (int r = 0; r < 2; ++r)
                        if (c.mAgree[i][j][r] * 20 > c.mPairs)
                            std::cout << "SKYRIMSE-LAND   edge " << kEdgeNames[i] << "->" << kEdgeNames[j]
                                      << (r != 0 ? " reversed " : " forward ") << c.mAgree[i][j][r] << "\n";
            if (dir == 0)
            {
                EXPECT_EQ(std::string(name), "col32->col0 forward") << "east neighbours share column 32 and column 0";
                EXPECT_GE(n * 1000, c.mPairs * 990);
            }
            else
            {
                EXPECT_EQ(std::string(name), "row32->row0 forward") << "north neighbours share row 32 and row 0";
                EXPECT_GE(n * 1000, c.mPairs * 990);
            }
        }

        // ---- B. quadrant numbering by base-texture adjacency ---------------------------------------------------
        for (int dir = 0; dir < 3; ++dir)
        {
            std::uint64_t eq[4][4] = {}, tot[4][4] = {};
            for (const auto& [g, a] : tamriel->mLands)
            {
                if (dir == 2)
                {
                    for (int qa = 0; qa < 4; ++qa)
                        for (int qb = qa + 1; qb < 4; ++qb)
                        {
                            const FormId x = a.mQuadrants[qa].mBase, y = a.mQuadrants[qb].mBase;
                            if (x == 0 || y == 0)
                                continue;
                            ++tot[qa][qb];
                            if (x == y)
                                ++eq[qa][qb];
                        }
                    continue;
                }
                const auto it = tamriel->mLands.find({ g.first + (dir == 0 ? 1 : 0), g.second + (dir == 1 ? 1 : 0) });
                if (it == tamriel->mLands.end())
                    continue;
                for (int qa = 0; qa < 4; ++qa)
                    for (int qb = 0; qb < 4; ++qb)
                    {
                        const FormId x = a.mQuadrants[qa].mBase, y = it->second.mQuadrants[qb].mBase;
                        if (x == 0 || y == 0)
                            continue;
                        ++tot[qa][qb];
                        if (x == y)
                            ++eq[qa][qb];
                    }
            }
            if (dir < 2)
            {
                // The two pairs that touch (east: A's quadrants 1 and 3 against B's 0 and 2; north: A's 2 and 3
                // against B's 0 and 1) must be the two most alike of the sixteen.
                std::vector<std::pair<double, int>> ranked;
                for (int qa = 0; qa < 4; ++qa)
                    for (int qb = 0; qb < 4; ++qb)
                        ranked.push_back(
                            { static_cast<double>(eq[qa][qb]) / static_cast<double>(tot[qa][qb]), qa * 4 + qb });
                std::sort(ranked.rbegin(), ranked.rend());
                const std::set<int> top = { ranked[0].second, ranked[1].second };
                const std::set<int> expected
                    = dir == 0 ? std::set<int>{ 1 * 4 + 0, 3 * 4 + 2 } : std::set<int>{ 2 * 4 + 0, 3 * 4 + 1 };
                EXPECT_EQ(top, expected) << (dir == 0 ? "east" : "north");
            }
            for (int qa = 0; qa < 4; ++qa)
                for (int qb = 0; qb < 4; ++qb)
                    if (tot[qa][qb] != 0)
                        std::cout << "SKYRIMSE-LAND base "
                                  << (dir == 0          ? "east"
                                             : dir == 1 ? "north"
                                                        : "same-cell")
                                  << " A.q" << qa << " B.q" << qb << " equal " << eq[qa][qb] << " of " << tot[qa][qb]
                                  << "\n";
        }

        // ---- C. VTXT position layout by the vertices two quadrants share ---------------------------------------
        {
            struct Pair
            {
                int a, b;
            };
            const Pair pairs[] = { { 0, 1 }, { 2, 3 }, { 0, 2 }, { 1, 3 } };
            for (const Pair& pr : pairs)
                for (int variant = 0; variant < 2; ++variant)
                {
                    std::uint64_t matched = 0, total = 0, totalB = 0;
                    for (const auto& [g, l] : tamriel->mLands)
                    {
                        const auto lineOf = [&](std::uint16_t pos) { return variant == 0 ? pos / 17 : pos % 17; };
                        const auto edgeOf = [&](std::uint16_t pos) { return variant == 0 ? pos % 17 : pos / 17; };
                        std::map<std::pair<FormId, int>, float> b;
                        for (const LandLayer& layer : l.mQuadrants[pr.b].mLayers)
                            for (const LandOpacity& o : layer.mOpacity)
                                if (edgeOf(o.mPosition) == 0 && layer.mTexture != 0)
                                {
                                    b[{ layer.mTexture, lineOf(o.mPosition) }] = o.mOpacity;
                                    ++totalB;
                                }
                        for (const LandLayer& layer : l.mQuadrants[pr.a].mLayers)
                            for (const LandOpacity& o : layer.mOpacity)
                                if (edgeOf(o.mPosition) == 16 && layer.mTexture != 0)
                                {
                                    ++total;
                                    const auto it = b.find({ layer.mTexture, lineOf(o.mPosition) });
                                    if (it != b.end() && std::abs(it->second - o.mOpacity) < 0.01f)
                                        ++matched;
                                }
                    }
                    std::cout << "SKYRIMSE-LAND vtxt q" << pr.a << "|q" << pr.b << " variant "
                              << (variant == 0 ? "pos=row*17+col(edge col)" : "pos=col*17+row(edge row)") << " matched "
                              << matched << " of " << total << " (B edge entries " << totalB << ")\n";
                    // pos = row * 17 + col, quadrant 1 east of 0 (shares col 16 / col 0), quadrant 2 north of 0.
                    if (variant == (pr.b == pr.a + 1 ? 0 : 1))
                        EXPECT_GE(matched * 100, total * 60) << "quadrants " << pr.a << " and " << pr.b;
                    else
                        EXPECT_LE(matched * 100, total * 5) << "quadrants " << pr.a << " and " << pr.b;
                }
        }

        // ---- D. PNAM bits --------------------------------------------------------------------------------------
        {
            std::map<std::uint16_t, int> hist;
            struct Group
            {
                int n = 0, climate = 0, water = 0, lod = 0, map = 0, dnam = 0, fullyLand = 0, someLand = 0;
            };
            Group set[8], clear[8];
            for (const auto& [id, w] : m.mWorlds)
            {
                const WorldSpaceRecord& r = w.mRecord;
                if (r.mParent == 0 || !r.mParentUsage)
                    continue;
                ++hist[*r.mParentUsage];
                for (int bit = 0; bit < 8; ++bit)
                {
                    Group& g = ((*r.mParentUsage >> bit) & 1) != 0 ? set[bit] : clear[bit];
                    ++g.n;
                    g.climate += r.mClimate != 0;
                    g.water += r.mWater != 0;
                    g.lod += (r.mLodWater != 0 || r.mLodWaterHeight);
                    g.map += r.mMap.has_value();
                    g.dnam += r.mDefaultWaterHeight.has_value();
                    g.fullyLand += (!w.mCells.empty() && w.mLands.size() * 10 >= w.mCells.size() * 9);
                    g.someLand += !w.mLands.empty();
                }
            }
            // A child that takes a kind of data from its parent has no record of its own for it: 0x04 map data (no
            // MNAM), 0x08 water (no NAM2), 0x10 climate (no CNAM). Bits 0x01 and 0x02 are never apart in the installed
            // data.
            EXPECT_EQ(set[2].map, 0);
            EXPECT_EQ(set[3].water, 0);
            EXPECT_EQ(set[4].climate, 0);
            EXPECT_EQ(clear[3].water, clear[3].n);
            EXPECT_EQ(set[0].n, set[1].n);
            for (const auto& [v, n] : hist)
                EXPECT_EQ((v & 1) != 0, (v & 2) != 0) << "bits 0x01 and 0x02 differ in PNAM " << v;
            for (const auto& [v, n] : hist)
                std::cout << "SKYRIMSE-LAND pnam 0x" << std::hex << v << std::dec << " worlds " << n << "\n";
            for (int bit = 0; bit < 7; ++bit)
                for (int isSet = 1; isSet >= 0; --isSet)
                {
                    const Group& g = isSet != 0 ? set[bit] : clear[bit];
                    std::cout << "SKYRIMSE-LAND pnam bit 0x" << std::hex << (1 << bit) << std::dec
                              << (isSet != 0 ? " SET   " : " CLEAR ") << "worlds " << g.n << " with CNAM " << g.climate
                              << " NAM2 " << g.water << " NAM3/NAM4 " << g.lod << " MNAM " << g.map << " DNAM "
                              << g.dnam << " own LAND >=90% of cells " << g.fullyLand << " any own LAND " << g.someLand
                              << "\n";
                }
        }

        // ---- E. XCLW ---------------------------------------------------------------------------------------------
        {
            std::map<std::string, std::uint64_t> cross;
            std::map<float, std::uint64_t> rawNoWater;
            std::uint64_t heightCells = 0, belowDefault = 0, defaultCells = 0, defaultWithFlag = 0;
            float minH = 1e30f, maxH = -1e30f;
            std::map<std::uint16_t, std::uint64_t> flagHist;
            for (const auto& [id, w] : m.mWorlds)
            {
                const float worldWater = w.mRecord.mDefaultWaterHeight ? *w.mRecord.mDefaultWaterHeight : 0.0f;
                for (const auto& [g, c] : w.mCells)
                {
                    const char* kind[] = { "absent", "default", "nowater", "height", "invalid" };
                    const bool hasLand = w.mLands.count(g) != 0;
                    ++cross[std::string(kind[static_cast<int>(c.mWater.mKind)])
                        + " flag2=" + ((c.mFlags & 2) ? "1" : "0") + " land=" + (hasLand ? "1" : "0")];
                    ++flagHist[c.mFlags];
                    if (c.mWater.mKind == CellWaterKind::NoWater || c.mWater.mKind == CellWaterKind::Invalid)
                        ++rawNoWater[c.mWater.mRaw];
                    if (c.mWater.mKind == CellWaterKind::Height)
                    {
                        ++heightCells;
                        minH = std::min(minH, c.mWater.mHeight);
                        maxH = std::max(maxH, c.mWater.mHeight);
                    }
                    if (c.mWater.mKind == CellWaterKind::UseWorldDefault && hasLand)
                    {
                        const LandRecord& l = w.mLands.at(g);
                        if (l.mHasHeights)
                        {
                            ++defaultCells;
                            defaultWithFlag += (c.mFlags & 2) != 0;
                            if (*std::min_element(l.mHeights.begin(), l.mHeights.end()) < worldWater)
                                ++belowDefault;
                        }
                    }
                }
            }
            // Every XCLW value is one of: the maximum float (the world's default), a sentinel at or beyond 2^31 in
            // magnitude (no water: -FLT_MAX, -2^31 and 2^32 in the installed data), or a plausible height.
            EXPECT_EQ(cross.count("invalid flag2=1 land=0") + cross.count("invalid flag2=1 land=1"), 0u);
            EXPECT_GT(heightCells, 0u);
            EXPECT_GT(minH, -100000.0f);
            EXPECT_LT(maxH, 100000.0f);
            for (const auto& [v, n] : rawNoWater)
                EXPECT_TRUE(v == -FLT_MAX || v == -2147483648.0f || v >= 4.29e9f) << v; // 2^32 less a few float steps

            for (const auto& [k, n] : cross)
                std::cout << "SKYRIMSE-LAND xclw " << k << " " << n << "\n";
            for (const auto& [v, n] : rawNoWater)
                std::cout << "SKYRIMSE-LAND xclw sentinel raw " << v << " cells " << n << "\n";
            std::cout << "SKYRIMSE-LAND xclw height cells " << heightCells << " range " << minH << " to " << maxH
                      << "\n";
            std::cout << "SKYRIMSE-LAND xclw default cells with land " << defaultCells << " with flag 2 "
                      << defaultWithFlag << " terrain below the world default water " << belowDefault << "\n";
            for (const auto& [f, n] : flagHist)
                std::cout << "SKYRIMSE-LAND cell flags 0x" << std::hex << f << std::dec << " " << n << "\n";
        }

        // ---- F. layers: null textures and overlap ---------------------------------------------------------------
        {
            std::uint64_t atxt = 0, nullAtxt = 0, unresolved = 0, notLtex = 0, nullWithEntries = 0, nullEntries = 0,
                          nullAlone = 0, baseNull = 0, baseTotal = 0, landsNoBase = 0, lands = 0, ltexNoSet = 0,
                          setNoDiffuse = 0, setMissing = 0;
            std::map<int, std::uint64_t> nullByIndex, overlap, layersPerQuadrant;
            std::set<FormId> notLtexIds;
            std::map<std::string, std::uint64_t> notLtexKind;
            std::uint64_t notLtexBase = 0;
            float nullMin = 2, nullMax = -1;
            std::uint64_t maxPerCell = 0, indexGaps = 0;
            for (const auto& [id, w] : m.mWorlds)
                for (const auto& [g, l] : w.mLands)
                {
                    ++lands;
                    bool anyBase = false;
                    for (const LandQuadrant& q : l.mQuadrants)
                    {
                        anyBase = anyBase || q.mBase != 0;
                        layersPerQuadrant[static_cast<int>(q.mLayers.size())]++;
                        std::array<int, 289> count{};
                        std::uint16_t expect = 0;
                        for (const LandLayer& layer : q.mLayers)
                        {
                            ++atxt;
                            indexGaps += layer.mLayerIndex != expect++;
                            if (layer.mTexture == kUnresolvedFormId)
                                ++unresolved;
                            else if (layer.mTexture == 0)
                            {
                                ++nullAtxt;
                                nullByIndex[layer.mLayerIndex]++;
                                nullAlone += q.mLayers.size() == 1;
                                if (!layer.mOpacity.empty())
                                    ++nullWithEntries;
                                nullEntries += layer.mOpacity.size();
                                for (const LandOpacity& o : layer.mOpacity)
                                {
                                    nullMin = std::min(nullMin, o.mOpacity);
                                    nullMax = std::max(nullMax, o.mOpacity);
                                }
                            }
                            else if (m.mLtex.count(layer.mTexture) == 0)
                            {
                                ++notLtex;
                                notLtexIds.insert(layer.mTexture);
                                notLtexKind[m.mTxst.count(layer.mTexture) != 0 ? "a TXST"
                                        : m.mWatr.count(layer.mTexture) != 0   ? "a WATR"
                                                                               : "no record of the five masters"]++;
                            }
                            // The null layers paint the default ground texture, so they count; the base is one more.
                            for (const LandOpacity& o : layer.mOpacity)
                                ++count[o.mPosition];
                        }
                        for (int c : count)
                            overlap[c + 1]++; // + the base texture
                        if (q.mBase != 0 && m.mLtex.count(q.mBase) == 0 && q.mBase != kUnresolvedFormId)
                            ++notLtexBase;
                        ++baseTotal;
                        baseNull += q.mBase == 0;
                    }
                    landsNoBase += !anyBase;
                    maxPerCell = std::max<std::uint64_t>(maxPerCell, l.layerCount());
                }
            for (const auto& [id, t] : m.mLtex)
            {
                ltexNoSet += t.mTextureSet == 0;
                if (t.mTextureSet != 0)
                {
                    const auto it = m.mTxst.find(t.mTextureSet);
                    if (it == m.mTxst.end())
                        ++setMissing;
                    else if (it->second.mPaths[0].empty())
                        ++setNoDiffuse;
                }
            }
            // A null LTEX in ATXT is a real layer (almost always with opacity entries): it paints the default ground
            // texture. A base texture of 0 is the same. One dangling id (no record in its plugin) is used by 101
            // layers.
            EXPECT_GT(nullAtxt, 0u);
            EXPECT_GE(nullWithEntries * 100, nullAtxt * 99);
            EXPECT_EQ(unresolved, 0u);
            EXPECT_EQ(indexGaps, 0u);
            EXPECT_EQ(notLtexIds.size(), 1u);
            std::cout << "SKYRIMSE-LAND layers lands " << lands << " atxt " << atxt << " null " << nullAtxt
                      << " unresolved " << unresolved << " non-null not an LTEX " << notLtex << " layer index gaps "
                      << indexGaps << " max layers per cell " << maxPerCell << "\n";
            std::cout << "SKYRIMSE-LAND non-null ATXT not an LTEX: distinct ids " << notLtexIds.size()
                      << " base BTXT not an LTEX " << notLtexBase << "\n";
            for (FormId id : notLtexIds)
            {
                std::cout << "SKYRIMSE-LAND   the id is 0x" << std::hex << id << std::dec;
                // What record is it? Walk the plugin its top byte names (all five masters are plugins 0 to 4).
                struct Finder final : Content::Visitor
                {
                    std::uint32_t mWanted = 0;
                    std::string mType;
                    bool onRecord(const Content::RawRecord& r) override
                    {
                        if (r.mFormId == mWanted)
                            mType = Content::fourCCToString(r.mType)
                                + ((r.mFlags & RecordFlags::Deleted) ? " deleted" : "");
                        return true;
                    }
                } finder;
                finder.mWanted = id;
                Content::Error error;
                if (const auto plugin = Content::Plugin::openFile(fs::path(env) / kMasters[(id >> 24) & 0xFF], error))
                {
                    Content::WalkOptions options;
                    options.mReadData = false;
                    plugin->walk(finder, options);
                }
                std::cout << " is " << (finder.mType.empty() ? "not a record of its plugin" : finder.mType) << "\n";
            }
            for (const auto& [k, n] : notLtexKind)
                std::cout << "SKYRIMSE-LAND   resolves to " << k << " " << n << "\n";
            std::cout << "SKYRIMSE-LAND null atxt with VTXT entries " << nullWithEntries << " entries " << nullEntries
                      << " opacity " << nullMin << " to " << nullMax << " alone in quadrant " << nullAlone << "\n";
            for (const auto& [i, n] : nullByIndex)
                std::cout << "SKYRIMSE-LAND null atxt at layer index " << i << " " << n << "\n";
            std::cout << "SKYRIMSE-LAND base quadrants " << baseTotal << " null " << baseNull
                      << " lands with no base at all " << landsNoBase << "\n";
            for (const auto& [k, n] : layersPerQuadrant)
                std::cout << "SKYRIMSE-LAND quadrants with " << k << " layers " << n << "\n";
            for (const auto& [k, n] : overlap)
                std::cout << "SKYRIMSE-LAND vertices drawn from " << k << " textures (base + layers, null included) "
                          << n << "\n";
            std::cout << "SKYRIMSE-LAND ltex " << m.mLtex.size() << " without a texture set " << ltexNoSet
                      << " set missing " << setMissing << " set without a diffuse path " << setNoDiffuse << "\n";
        }

        // ---- G. Solstheim and the worlds with the most LAND --------------------------------------------------------
        for (const auto& [id, w] : m.mWorlds)
        {
            if (w.mLands.size() < 100)
                continue;
            std::uint64_t flat = 0, withLayers = 0, withBase = 0, withNormals = 0, inMap = 0, inBounds = 0,
                          noHeights = 0, atDefault = 0;
            float lo = 1e30f, hi = -1e30f;
            std::set<float> offsets;
            const WorldSpaceRecord& r = w.mRecord;
            for (const auto& [g, l] : w.mLands)
            {
                if (!l.mHasHeights)
                {
                    ++noHeights;
                    continue;
                }
                const auto [mn, mx] = std::minmax_element(l.mHeights.begin(), l.mHeights.end());
                lo = std::min(lo, *mn);
                hi = std::max(hi, *mx);
                flat += *mn == *mx;
                offsets.insert(l.mHeightOffset);
                if (r.mDefaultLandHeight && *mn == *mx && *mn == *r.mDefaultLandHeight)
                    ++atDefault;
                withLayers += l.layerCount() != 0;
                bool b = false;
                for (const LandQuadrant& q : l.mQuadrants)
                    b = b || q.mBase != 0;
                withBase += b;
                withNormals += l.mHasNormals;
                if (r.mMap)
                    inMap += g.first >= r.mMap->mNorthWestX && g.first <= r.mMap->mSouthEastX
                        && g.second <= r.mMap->mNorthWestY && g.second >= r.mMap->mSouthEastY;
                if (r.mMinBounds && r.mMaxBounds)
                    inBounds += g.first * 4096.0f >= r.mMinBounds->x && g.first * 4096.0f < r.mMaxBounds->x
                        && g.second * 4096.0f >= r.mMinBounds->y && g.second * 4096.0f < r.mMaxBounds->y;
            }
            const bool solst = &w == solstheim;
            std::cout << "SKYRIMSE-LAND world" << (solst ? " [Solstheim]" : "") << " cells " << w.mCells.size()
                      << " lands " << w.mLands.size() << " flat " << flat << " flat at world default land height "
                      << atDefault << " with layers " << withLayers << " with a base texture " << withBase
                      << " with normals " << withNormals << " no heights " << noHeights << " inside MNAM rect " << inMap
                      << " inside NAM0/NAM9 " << inBounds << " height range " << lo << " to " << hi
                      << " distinct offsets " << offsets.size() << "\n";
        }

        // Solstheim in detail: what the LAND outside the map's usable rectangle is.
        ASSERT_NE(solstheim, nullptr);
        {
            const WorldSpaceRecord& r = solstheim->mRecord;
            ASSERT_TRUE(r.mMap.has_value());
            std::uint64_t outside = 0, outsideFlat = 0, outsideLayers = 0, outsideBase = 0, inside = 0, insideFlat = 0;
            std::map<float, std::uint64_t> flatHeights;
            std::uint64_t cellsInside = 0, cellsOutside = 0;
            const auto isInside = [&](const Grid& g) {
                return g.first >= r.mMap->mNorthWestX && g.first <= r.mMap->mSouthEastX
                    && g.second <= r.mMap->mNorthWestY && g.second >= r.mMap->mSouthEastY;
            };
            for (const auto& [g, c] : solstheim->mCells)
                (isInside(g) ? cellsInside : cellsOutside)++;
            std::int32_t minX = 9999, maxX = -9999, minY = 9999, maxY = -9999;
            for (const auto& [g, l] : solstheim->mLands)
            {
                minX = std::min(minX, g.first);
                maxX = std::max(maxX, g.first);
                minY = std::min(minY, g.second);
                maxY = std::max(maxY, g.second);
                const auto [mn, mx] = std::minmax_element(l.mHeights.begin(), l.mHeights.end());
                const bool flat = *mn == *mx;
                bool base = false;
                for (const LandQuadrant& q : l.mQuadrants)
                    base = base || q.mBase != 0;
                if (isInside(g))
                {
                    ++inside;
                    insideFlat += flat;
                }
                else
                {
                    ++outside;
                    outsideFlat += flat;
                    outsideLayers += l.layerCount() != 0;
                    outsideBase += base;
                    if (flat)
                        ++flatHeights[*mn];
                }
            }
            std::cout << "SKYRIMSE-LAND solstheim grid x " << minX << " to " << maxX << " y " << minY << " to " << maxY
                      << " MNAM rect x " << r.mMap->mNorthWestX << " to " << r.mMap->mSouthEastX << " y "
                      << r.mMap->mSouthEastY << " to " << r.mMap->mNorthWestY << " size field " << r.mMap->mSize
                      << " default land height " << (r.mDefaultLandHeight ? *r.mDefaultLandHeight : 0.0f)
                      << " default water " << (r.mDefaultWaterHeight ? *r.mDefaultWaterHeight : 0.0f) << "\n";
            std::cout << "SKYRIMSE-LAND solstheim cells inside rect " << cellsInside << " outside " << cellsOutside
                      << "; LAND inside " << inside << " (flat " << insideFlat << "), outside " << outside << " (flat "
                      << outsideFlat << ", with layers " << outsideLayers << ", with a base " << outsideBase << ")\n";
            std::vector<std::pair<std::uint64_t, float>> top;
            for (const auto& [h, n] : flatHeights)
                top.push_back({ n, h });
            std::sort(top.rbegin(), top.rend());
            for (std::size_t i = 0; i < std::min<std::size_t>(top.size(), 4); ++i)
                std::cout << "SKYRIMSE-LAND solstheim flat height " << top[i].second << " cells " << top[i].first
                          << "\n";
            std::cout << "SKYRIMSE-LAND solstheim distinct flat heights " << flatHeights.size() << "\n";
            // The bulk of Solstheim's LAND is filler outside the map's usable rectangle, and a large part of it is flat
            // at the world's default land height (to the 8 unit step of the height scale).
            EXPECT_GT(outside, inside * 100);
            ASSERT_FALSE(top.empty());
            ASSERT_TRUE(r.mDefaultLandHeight.has_value());
            EXPECT_LE(std::abs(top[0].second - *r.mDefaultLandHeight), 8.0f);
            EXPECT_GT(top[0].first * 100, outside * 30);
        }

        // The default ground texture that a null BTXT/ATXT stands for: is the file in the game's archives?
        {
            bool found = false, foundNormal = false;
            std::uint64_t archives = 0;
            for (const auto& entry : fs::directory_iterator(env))
            {
                std::string name = entry.path().filename().string();
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
                if (name.find("textures") == std::string::npos || name.rfind(".bsa") != name.size() - 4)
                    continue;
                ArchiveListing listing;
                std::string error;
                if (!readArchiveListing(entry.path(), listing, error))
                    continue;
                ++archives;
                for (std::string file : listing.mFiles)
                {
                    std::transform(
                        file.begin(), file.end(), file.begin(), [](unsigned char c) { return std::tolower(c); });
                    found = found || file == "textures\\landscape\\dirt02.dds";
                    foundNormal = foundNormal || file == "textures\\landscape\\dirt02_n.dds";
                }
            }
            EXPECT_TRUE(found);
            EXPECT_TRUE(foundNormal);
            std::cout << "SKYRIMSE-LAND default ground texture archives scanned " << archives << " dirt02.dds " << found
                      << " dirt02_n.dds " << foundNormal << "\n";
        }
    }
}
