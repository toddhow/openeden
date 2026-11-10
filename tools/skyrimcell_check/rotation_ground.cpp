// Task 0086, rotation convention of Skyrim SE placements (stage log, "Rotation convention"). Not built by CMake: an
// analysis tool that was compiled by hand against the pack's library (game_skyrimse_world) and componentsrd.
//
// Question: in which order, and with which signs, do the Euler angles of a REFR combine? Placements that follow a slope
// (rocks, tilted statics on hills) are aligned with the terrain normal by the level designers, so the right convention
// brings the object's up axis (R * +Z) closest to the normal. The tool reads the LAND heights (VHGT) of the first
// plugin directly, takes the 48 candidates (6 orders x 8 sign choices), and prints each one's mean angle to the normal.
// Usage: rotation_ground <plugin list file, one path per line> <Data folder> [min tilt rad] [max tilt rad]
// Prints counts and angles only; no record, cell or mod name. Z is invisible to this test (it does not move the up
// axis).
#include <algorithm>
#include <array>
#include <cmath>
#include <components/contentsource/plugin.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <game_skyrimse/world_view.hpp>
#include <map>
#include <string>
#include <vector>
#include <zlib.h>
namespace fs = std::filesystem;
namespace sdkgame = openeden::sdk::game;
using namespace game_skyrimse;
using V3 = std::array<double, 3>;
using M3 = std::array<double, 9>;
static M3 rot(int axis, double a)
{
    double c = std::cos(a), s = std::sin(a);
    if (axis == 0)
        return { 1, 0, 0, 0, c, -s, 0, s, c };
    if (axis == 1)
        return { c, 0, s, 0, 1, 0, -s, 0, c };
    return { c, -s, 0, s, c, 0, 0, 0, 1 };
}
static M3 mul(const M3& a, const M3& b)
{
    M3 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                r[i * 3 + j] += a[i * 3 + k] * b[k * 3 + j];
    return r;
}
struct Variant
{
    int order[3];
    double sign[3];
    std::string name;
};
static M3 build(const Variant& v, const V3& e)
{
    M3 r = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    for (int i = 0; i < 3; ++i)
    {
        int ax = v.order[i];
        r = mul(rot(ax, v.sign[ax] * e[ax]), r);
    }
    return r;
}
struct Land
{
    std::array<float, 33 * 33> h{};
};
static std::vector<std::uint8_t> payload(const Content::RawRecord& r)
{
    std::vector<std::uint8_t> out;
    const std::uint8_t* d = reinterpret_cast<const std::uint8_t*>(r.mData.data());
    if (!r.isCompressed())
    {
        out.assign(d, d + r.mData.size());
        return out;
    }
    if (r.mData.size() < 4)
        return out;
    std::uint32_t n;
    std::memcpy(&n, d, 4);
    if (n > 64u * 1024 * 1024)
        return out;
    out.resize(n);
    uLongf len = n;
    if (uncompress(out.data(), &len, d + 4, static_cast<uLong>(r.mData.size() - 4)) != Z_OK)
        out.clear();
    return out;
}
class LandWalker : public Content::Visitor
{
public:
    std::map<std::pair<int, int>, Land> lands;
    std::uint32_t worldId = 0;
    std::uint32_t curWorld = 0;
    bool haveCell = false;
    int cx = 0, cy = 0;
    bool onGroupBegin(const Content::GroupInfo& g) override
    {
        if (g.mType == 1)
        {
            curWorld = g.mLabel;
            haveCell = false;
        }
        return true;
    }
    bool onRecord(const Content::RawRecord& r) override
    {
        if (r.mType == Content::fourCC("CELL"))
        {
            haveCell = false;
            auto p = payload(r);
            for (std::size_t i = 0; i + 6 <= p.size();)
            {
                std::uint32_t t;
                std::uint16_t sz;
                std::memcpy(&t, &p[i], 4);
                std::memcpy(&sz, &p[i + 4], 2);
                if (t == Content::fourCC("XCLC") && sz >= 8 && i + 6 + 8 <= p.size())
                {
                    std::int32_t xy[2];
                    std::memcpy(xy, &p[i + 6], 8);
                    cx = xy[0];
                    cy = xy[1];
                    haveCell = true;
                }
                i += 6 + sz;
            }
        }
        else if (r.mType == Content::fourCC("LAND") && haveCell && curWorld == worldId)
        {
            auto p = payload(r);
            for (std::size_t i = 0; i + 6 <= p.size();)
            {
                std::uint32_t t;
                std::uint16_t sz;
                std::memcpy(&t, &p[i], 4);
                std::memcpy(&sz, &p[i + 4], 2);
                if (t == Content::fourCC("VHGT") && sz >= 1096 && i + 6 + 1096 <= p.size())
                {
                    float off;
                    std::memcpy(&off, &p[i + 6], 4);
                    Land L;
                    float rowStart = off;
                    for (int rr = 0; rr < 33; ++rr)
                    {
                        float h = 0;
                        for (int c = 0; c < 33; ++c)
                        {
                            std::int8_t g = static_cast<std::int8_t>(p[i + 6 + 4 + rr * 33 + c]);
                            if (c == 0)
                            {
                                rowStart += g;
                                h = rowStart;
                            }
                            else
                                h += g;
                            L.h[rr * 33 + c] = h * 8.0f;
                        }
                    }
                    lands[{ cx, cy }] = L;
                }
                i += 6 + sz;
            }
        }
        return true;
    }
};
static bool heightAt(const std::map<std::pair<int, int>, Land>& lands, double x, double y, double& h)
{
    int gx = (int)std::floor(x / 4096.0), gy = (int)std::floor(y / 4096.0);
    auto it = lands.find({ gx, gy });
    if (it == lands.end())
        return false;
    double u = (x - gx * 4096.0) / 128.0, v = (y - gy * 4096.0) / 128.0;
    int i0 = std::min(31, (int)u), j0 = std::min(31, (int)v);
    double fu = u - i0, fv = v - j0;
    // rows run along +Y, columns along +X (row-major r = y index, c = x index)
    auto H = [&](int c, int r) { return (double)it->second.h[r * 33 + c]; };
    h = H(i0, j0) * (1 - fu) * (1 - fv) + H(i0 + 1, j0) * fu * (1 - fv) + H(i0, j0 + 1) * (1 - fu) * fv
        + H(i0 + 1, j0 + 1) * fu * fv;
    return true;
}
int main(int argc, char** argv)
{
    std::vector<fs::path> paths;
    {
        std::ifstream f(argv[1]);
        std::string l;
        while (std::getline(f, l))
            if (!l.empty())
                paths.push_back(l);
    }
    fs::path data = argv[2];
    const double minTilt = argc > 3 ? std::atof(argv[3]) : 0.05;
    const double maxTilt = argc > 4 ? std::atof(argv[4]) : 0.9;
    DataFolderFiles files(data);
    SkyrimWorldView view({}, &files);
    std::vector<std::string> problems;
    view.load(paths, problems);
    auto world = view.findWorldByEditorId("Tamriel");
    if (!world)
    {
        std::puts("no Tamriel");
        return 1;
    }
    LandWalker w;
    w.worldId = 0x3C;
    Content::Error err;
    auto plugin = Content::Plugin::openFile(paths[0], err);
    plugin->walk(w);
    std::printf("land cells %zu (world form 0x3C), view world id %u\n", w.lands.size(), world->id);

    std::vector<Variant> vars;
    int perms[6][3] = { { 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 }, { 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 } };
    for (auto& p : perms)
        for (int m = 0; m < 8; ++m)
        {
            Variant v;
            for (int i = 0; i < 3; ++i)
                v.order[i] = p[i];
            v.sign[0] = (m & 1) ? 1 : -1;
            v.sign[1] = (m & 2) ? 1 : -1;
            v.sign[2] = (m & 4) ? 1 : -1;
            char b[64];
            std::snprintf(b, sizeof b, "apply %c%c%c sx%+d sy%+d sz%+d", "XYZ"[p[0]], "XYZ"[p[1]], "XYZ"[p[2]],
                (int)v.sign[0], (int)v.sign[1], (int)v.sign[2]);
            v.name = b;
            vars.push_back(v);
        }
    std::vector<double> dotSum(vars.size(), 0), angSum(vars.size(), 0);
    std::vector<long> better(vars.size(), 0);
    double baseDot = 0, baseAng = 0;
    long n = 0;
    std::vector<sdkgame::WorldCellInfo> cells;
    view.listCells(0, view.cellCount(), cells);
    std::map<int, long> kinds;
    for (auto& c : cells)
    {
        if (c.interior || c.world != world->id || !c.hasGrid)
            continue;
        sdkgame::WorldCellContent cont;
        std::string e;
        if (!view.loadCell(c.id, cont, e))
            continue;
        for (auto& o : cont.objects)
        {
            if (o.initiallyDisabled)
                continue;
            auto k = o.baseKind;
            if (!(k == sdkgame::BaseKind::Static || k == sdkgame::BaseKind::MovableStatic
                    || k == sdkgame::BaseKind::Tree || k == sdkgame::BaseKind::Flora))
                continue;
            double tilt = std::max(
                std::abs(std::remainder(o.rotation.x, 6.2831853)), std::abs(std::remainder(o.rotation.y, 6.2831853)));
            if (tilt < minTilt || tilt > maxTilt)
                continue;
            // terrain normal from central differences over 128 units
            double h1, h2, h3, h4, h0;
            if (!heightAt(w.lands, o.position.x + 64, o.position.y, h1)
                || !heightAt(w.lands, o.position.x - 64, o.position.y, h2)
                || !heightAt(w.lands, o.position.x, o.position.y + 64, h3)
                || !heightAt(w.lands, o.position.x, o.position.y - 64, h4)
                || !heightAt(w.lands, o.position.x, o.position.y, h0))
                continue;
            // only objects standing on the ground (within 150 units of terrain height)
            if (std::abs(o.position.z - h0) > 150)
                continue;
            V3 nrm = { -(h1 - h2) / 128.0, -(h3 - h4) / 128.0, 1.0 };
            double nl = std::sqrt(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]);
            for (double& q : nrm)
                q /= nl;
            if (nrm[2] > 0.995)
                continue; // flat ground says nothing
            ++n;
            ++kinds[(int)k];
            baseDot += nrm[2];
            baseAng += std::acos(std::min(1.0, nrm[2]));
            for (std::size_t vi = 0; vi < vars.size(); ++vi)
            {
                M3 r = build(vars[vi], { o.rotation.x, o.rotation.y, o.rotation.z });
                V3 up = { r[2], r[5], r[8] };
                double d = up[0] * nrm[0] + up[1] * nrm[1] + up[2] * nrm[2];
                dotSum[vi] += d;
                angSum[vi] += std::acos(std::max(-1.0, std::min(1.0, d)));
                if (d > nrm[2] + 1e-9)
                    ++better[vi];
            }
        }
    }
    std::printf(
        "sloped-ground tilted statics: %ld  (tilt %.2f..%.2f rad); untilted baseline: mean angle to terrain normal "
        "%.4f rad\n",
        n, minTilt, maxTilt, baseAng / n);
    std::vector<int> idx(vars.size());
    for (std::size_t i = 0; i < idx.size(); ++i)
        idx[i] = (int)i;
    std::sort(idx.begin(), idx.end(), [&](int a, int b) { return angSum[a] < angSum[b]; });
    for (int i : idx)
        std::printf("%-34s mean angle %.4f rad, closer than untilted in %5.1f%%\n", vars[i].name.c_str(), angSum[i] / n,
            100.0 * better[i] / n);
}
