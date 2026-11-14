// Task 0144 stage a: the gated real-data survey of the bhk collision blocks
// (docs/research/0144-havok-collision-provenance.md). Not run by default. With OPENEDEN_SKYRIMSE_DATA set to the game's
// Data folder it reads every .nif of the five masters' BSAs and prints AGGREGATE numbers only (lines starting
// BHK-SURVEY): shape tree signatures, filter layers, motion types, and the VERTEX CORRESPONDENCE check that settles the
// format questions: for every file with bhk data and drawn geometry, the collision vertices are decoded under each
// candidate reading (quaternion order of chunk transforms and bodies, the 4x4 layout of transform shapes, whether a
// plain rigid body places its shape, the unit scale) and the share that land within half a game unit of a visual vertex
// is counted; the reading that scores far above the others is the fact. No path or name is printed and nothing is
// written. OPENEDEN_BHK_SURVEY_MAX stops after that many files; OPENEDEN_BHK_SURVEY_STRIDE (default 1) checks every
// n-th file with collision for the correspondence.
#include <components/bsa/compressedbsafile.hpp>
#include <components/nif/niffile.hpp>
#include <components/nif/physics.hpp>
#include <components/nifscene/build.hpp>
#include <components/nifscene/collision.hpp>
#include <components/nifscene/flatten.hpp>
#include <components/nifscene/havok.hpp>
#include <components/vfs/pathutil.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace NifScene::Havok;

    std::string lower(std::string s)
    {
        std::transform(
            s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    void line(const std::string& text)
    {
        std::cout << "BHK-SURVEY " << text << std::endl;
    }

    std::string signature(const Collision& c, std::uint32_t shape, int depth = 0)
    {
        if (shape == sNone || depth > 8)
            return "?";
        const Shape& s = c.mShapes[shape];
        std::string out = s.mTypeName;
        if (!s.mChildren.empty())
        {
            out += "(";
            for (std::size_t i = 0; i < s.mChildren.size() && i < 3; ++i)
                out += (i ? "," : "") + signature(c, s.mChildren[i], depth + 1);
            if (s.mChildren.size() > 3)
                out += ",...";
            out += ")";
        }
        return out;
    }

    // ---- poses -------------------------------------------------------------------------------------------------

    using V3 = std::array<double, 3>;

    struct Pose
    {
        std::array<double, 9> r{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
        V3 t{ 0, 0, 0 };
        double s = 1.0;
    };

    V3 applyPose(const Pose& p, const V3& v)
    {
        V3 out{};
        for (int row = 0; row < 3; ++row)
            out[row] = p.s * (p.r[row * 3] * v[0] + p.r[row * 3 + 1] * v[1] + p.r[row * 3 + 2] * v[2]) + p.t[row];
        return out;
    }

    Pose compose(const Pose& parent, const Pose& child)
    {
        Pose out;
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column)
            {
                double sum = 0.0;
                for (int k = 0; k < 3; ++k)
                    sum += parent.r[row * 3 + k] * child.r[k * 3 + column];
                out.r[row * 3 + column] = sum;
            }
        out.s = parent.s * child.s;
        out.t = applyPose(parent, child.t);
        return out;
    }

    Pose nodeWorld(const NifScene::Scene& scene, std::uint32_t node)
    {
        std::vector<std::uint32_t> chain;
        for (std::uint32_t at = node; at < scene.mNodes.size() && chain.size() <= scene.mNodes.size();
             at = scene.mNodes[at].mParent)
            chain.push_back(at);
        Pose world;
        for (std::size_t i = chain.size(); i-- > 0;)
        {
            const NifScene::Transform& t = scene.mNodes[chain[i]].mTransform;
            Pose local;
            for (int k = 0; k < 9; ++k)
                local.r[k] = t.mRotation[k];
            for (int k = 0; k < 3; ++k)
                local.t[k] = t.mTranslation[k];
            local.s = t.mScale;
            world = compose(world, local);
        }
        return world;
    }

    bool isIdentity(const Pose& p)
    {
        const double e = 1e-4;
        for (int i = 0; i < 9; ++i)
            if (std::abs(p.r[i] - ((i % 4 == 0) ? 1.0 : 0.0)) > e)
                return false;
        return std::abs(p.s - 1.0) < e && std::abs(p.t[0]) < e && std::abs(p.t[1]) < e && std::abs(p.t[2]) < e;
    }

    Pose fromQuat(const Vec4& q)
    {
        Pose p;
        const double x = q[0], y = q[1], z = q[2], w = q[3];
        const double n = std::sqrt(x * x + y * y + z * z + w * w);
        if (!(n > 1e-6))
            return p;
        const double X = x / n, Y = y / n, Z = z / n, W = w / n;
        p.r = { 1 - 2 * (Y * Y + Z * Z), 2 * (X * Y - Z * W), 2 * (X * Z + Y * W), 2 * (X * Y + Z * W),
            1 - 2 * (X * X + Z * Z), 2 * (Y * Z - X * W), 2 * (X * Z - Y * W), 2 * (Y * Z + X * W),
            1 - 2 * (X * X + Y * Y) };
        return p;
    }

    // ---- candidate readings --------------------------------------------------------------------------------------

    struct Reading
    {
        bool mChunkXyzw, mBodyXyzw;
        int mMatrixLayout; // 0: v' = v * M, translation in elements 12..14; 1: v' = M * v, translation in 3, 7, 11
        bool mPlainBodyPlaces; // a bhkRigidBody (not T) places its shape with its translation and rotation
        double mScale;
        std::string name() const
        {
            return std::string("chunkquat=") + (mChunkXyzw ? "xyzw" : "wxyz")
                + " bodyquat=" + (mBodyXyzw ? "xyzw" : "wxyz")
                + " matrix=" + (mMatrixLayout == 0 ? "rowvec-last-row" : "colvec-last-column")
                + " plainbody=" + (mPlainBodyPlaces ? "places" : "ignored") + " scale=" + std::to_string(mScale);
        }
    };

    struct Triangle
    {
        V3 a, b, c;
    };

    // Havok-space chain applied to the points of a shape tree: Transform shapes and the body. Appends game-unit points
    // (before the node's world pose) and triangles.
    void gather(const Collision& c, std::uint32_t shape, const Pose& chain, int layout, std::vector<V3>& points,
        std::vector<Triangle>& triangles, int depth = 0)
    {
        if (shape == sNone || depth > 16)
            return;
        const Shape& s = c.mShapes[shape];
        auto place = [&](const Vec3& v) { return applyPose(chain, V3{ v[0], v[1], v[2] }); };
        switch (s.mKind)
        {
            case ShapeKind::ConvexHull:
                for (const Vec3& p : s.mPoints)
                    points.push_back(place(p));
                break;
            case ShapeKind::Box:
                for (int i = 0; i < 8; ++i)
                    points.push_back(place({ (i & 1 ? 1.f : -1.f) * s.mHalfExtents[0],
                        (i & 2 ? 1.f : -1.f) * s.mHalfExtents[1], (i & 4 ? 1.f : -1.f) * s.mHalfExtents[2] }));
                break;
            case ShapeKind::Capsule:
                points.push_back(place(s.mPoint1));
                points.push_back(place(s.mPoint2));
                break;
            case ShapeKind::TriMesh:
            {
                std::vector<V3> placed;
                placed.reserve(s.mVertices.size());
                for (const Vec3& p : s.mVertices)
                    placed.push_back(place(p));
                for (const V3& p : placed)
                    points.push_back(p);
                for (const auto& t : s.mTriangles)
                    triangles.push_back({ placed[t[0]], placed[t[1]], placed[t[2]] });
                break;
            }
            case ShapeKind::Transform:
            {
                Pose m;
                if (layout == 0)
                {
                    for (int i = 0; i < 3; ++i)
                        for (int j = 0; j < 3; ++j)
                            m.r[j * 3 + i] = s.mTransform[i * 4 + j]; // v'_j = sum_i v_i M[i][j]
                    m.t = { s.mTransform[12], s.mTransform[13], s.mTransform[14] };
                }
                else
                {
                    for (int i = 0; i < 3; ++i)
                        for (int j = 0; j < 3; ++j)
                            m.r[i * 3 + j] = s.mTransform[i * 4 + j];
                    m.t = { s.mTransform[3], s.mTransform[7], s.mTransform[11] };
                }
                for (std::uint32_t child : s.mChildren)
                    gather(c, child, compose(chain, m), layout, points, triangles, depth + 1);
                return;
            }
            default:
                break;
        }
        for (std::uint32_t child : s.mChildren)
            gather(c, child, chain, layout, points, triangles, depth + 1);
    }

    // ---- the visual geometry ------------------------------------------------------------------------------------

    struct Visual
    {
        struct Vertex
        {
            std::uint32_t mShape, mIndex;
        };
        std::unordered_map<std::int64_t, std::vector<Vertex>> mGrid;
        std::vector<std::vector<std::array<float, 3>>> mPositions; // per shape
        std::map<std::array<std::uint32_t, 4>, int> mTriangles; // {shape, sorted indices} -> parity of the stored order
        static std::int64_t key(int x, int y, int z)
        {
            return (static_cast<std::int64_t>(x & 0xFFFFF) << 40) | (static_cast<std::int64_t>(y & 0xFFFFF) << 20)
                | static_cast<std::int64_t>(z & 0xFFFFF);
        }
        void build(const NifScene::Flat::FlatModel& flat)
        {
            std::uint32_t id = 0;
            for (const auto& shape : flat.mShapes)
            {
                mPositions.push_back(shape.mPositions);
                for (std::uint32_t i = 0; i < shape.mPositions.size(); ++i)
                {
                    const auto& p = shape.mPositions[i];
                    mGrid[key(static_cast<int>(std::floor(p[0])), static_cast<int>(std::floor(p[1])),
                              static_cast<int>(std::floor(p[2])))]
                        .push_back({ id, i });
                }
                for (std::size_t t = 0; t + 2 < shape.mIndices.size(); t += 3)
                {
                    std::array<std::uint32_t, 3> v{ shape.mIndices[t], shape.mIndices[t + 1], shape.mIndices[t + 2] };
                    std::array<std::uint32_t, 3> sorted = v;
                    std::sort(sorted.begin(), sorted.end());
                    const int parity
                        = (v == sorted || v == std::array<std::uint32_t, 3>{ sorted[1], sorted[2], sorted[0] }
                              || v == std::array<std::uint32_t, 3>{ sorted[2], sorted[0], sorted[1] })
                        ? 0
                        : 1;
                    mTriangles[{ id, sorted[0], sorted[1], sorted[2] }] = parity;
                }
                ++id;
            }
        }
        // Visual vertices within half a unit of p.
        std::vector<Vertex> near(const V3& p) const
        {
            std::vector<Vertex> out;
            const int cx = static_cast<int>(std::floor(p[0])), cy = static_cast<int>(std::floor(p[1])),
                      cz = static_cast<int>(std::floor(p[2]));
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dz = -1; dz <= 1; ++dz)
                    {
                        const auto found = mGrid.find(key(cx + dx, cy + dy, cz + dz));
                        if (found == mGrid.end())
                            continue;
                        for (const Vertex& v : found->second)
                        {
                            const auto& q = mPositions[v.mShape][v.mIndex];
                            if (std::abs(q[0] - p[0]) <= 0.5 && std::abs(q[1] - p[1]) <= 0.5
                                && std::abs(q[2] - p[2]) <= 0.5)
                                out.push_back(v);
                        }
                    }
            return out;
        }
    };

    struct Score
    {
        std::uint64_t mPoints = 0, mMatched = 0, mFiles = 0, mGoodFiles = 0;
        std::uint64_t mTriangles = 0, mSameWinding = 0, mReversedWinding = 0;
    };

    TEST(BhkReal, survey_of_the_users_meshes_when_asked)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == 0)
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data(env);
        std::uint64_t limit = ~0ull, stride = 1;
        if (const char* m = std::getenv("OPENEDEN_BHK_SURVEY_MAX"); m != nullptr && *m)
            limit = std::strtoull(m, nullptr, 10);
        if (const char* m = std::getenv("OPENEDEN_BHK_SURVEY_STRIDE"); m != nullptr && *m)
            stride = std::max<std::uint64_t>(1, std::strtoull(m, nullptr, 10));

        std::vector<fs::path> archives;
        std::error_code ec;
        for (fs::directory_iterator it(data, ec), end; !ec && it != end; it.increment(ec))
        {
            if (!it->is_regular_file(ec) || lower(it->path().extension().string()) != ".bsa")
                continue;
            const std::string stem = lower(it->path().filename().string());
            for (const char* prefix : { "skyrim - ", "update", "dawnguard", "hearthfires", "dragonborn" })
                if (stem.rfind(prefix, 0) == 0)
                {
                    archives.push_back(it->path());
                    break;
                }
        }
        std::sort(archives.begin(), archives.end());

        NifScene::SceneOptions options;
        options.mReadHavok = true;

        std::uint64_t files = 0, failed = 0, withColl = 0, bodies = 0, bodiesT = 0, checked = 0;
        std::map<std::string, std::uint64_t> sigBodies, sigFiles, motion, quality, response, layer, kinds,
            unsupportedTypes;
        std::map<std::string, std::uint64_t> flagsByLayer, layerClass;
        std::uint64_t layerMismatch = 0, noShape = 0, overCap = 0, droppedTris = 0, notFinite = 0, otherObjects = 0;
        std::uint64_t multiBody = 0, nonRootBody = 0, plainBodyMoved = 0, plainBodyRotated = 0, tBodyMoved = 0;
        // 0140 set: models whose drawn triangles give no collision mesh, by what the bhk data says.
        std::map<std::string, std::uint64_t> visualEmptyModels;
        // Transform shape matrices.
        std::uint64_t xformIdentityRotation = 0;
        std::map<std::string, std::uint64_t> xformElement15;
        std::uint64_t xformShapes = 0, xformLastRow = 0, xformLastColumn = 0, xformCorner1 = 0, xformBothZero = 0;
        std::map<std::string, Score> scores;
        std::map<std::string, std::pair<std::uint64_t, std::uint64_t>>
            inBounds; // reading + body kind -> points, inside the drawn bounds plus 10 units
        std::map<std::string, std::pair<std::uint64_t, std::uint64_t>>
            byCategory; // body signature -> points, matched (the best reading)
        const char* probeEnv = std::getenv("OPENEDEN_BHK_PROBE");
        const std::string probe = probeEnv ? lower(probeEnv) : std::string();
        // Compressed mesh decoding checks against the data's own bounds.
        std::uint64_t cmsData = 0, cmsChunks = 0, cmsWithTransform = 0, cmsIdxBad = 0, cmsScaleNot1 = 0,
                      cmsRadiusSet = 0;
        std::map<std::string, std::uint64_t> variantInside, variantTotal, transformIdx;

        std::vector<Reading> readings;
        for (bool chunk : { true, false })
            for (bool body : { true, false })
                for (int layout : { 0, 1 })
                    for (bool plain : { false, true })
                        for (double scale : { 69.99125, 70.0, 7.0, 1.0 })
                            readings.push_back({ chunk, body, layout, plain, scale });

        for (const fs::path& path : archives)
        {
            Bsa::CompressedBSAFile archive;
            try
            {
                archive.open(path);
            }
            catch (const std::exception&)
            {
                continue;
            }
            for (const auto& entry : archive.getList())
            {
                const std::string name = lower(std::string(entry.name()));
                if (name.size() < 4 || name.compare(name.size() - 4, 4, ".nif") != 0)
                    continue;
                if (files >= limit)
                    break;
                ++files;
                std::string bytes;
                {
                    auto stream = archive.getFile(&entry);
                    bytes.assign((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
                }
                try
                {
                    Files::IStreamPtr stream(new std::istringstream(bytes, std::ios::binary));
                    Nif::NIFFile file(VFS::Path::Normalized("survey.nif"));
                    Nif::Reader reader(file);
                    reader.parse(std::move(stream), options.mRead);

                    const Collision coll = readCollision(file);
                    if (coll.mReport.mCollisionObjects == 0)
                    {
                        // Models with no bhk data at all: do their drawn triangles give a mesh? (counted below)
                        continue;
                    }
                    ++withColl;
                    if (!probe.empty() && name.find(probe) != std::string::npos)
                    {
                        for (const Body& b : coll.mBodies)
                            line("probe body layer " + std::to_string(b.mLayer) + " flags "
                                + std::to_string(b.mFilterFlags) + " motion " + std::to_string(b.mMotionType)
                                + " response " + std::to_string(b.mResponseType) + " T "
                                + std::to_string(b.mHasTransform) + " signature " + signature(coll, b.mShape)
                                + " classified " + (bodyBlocksActors(b) ? "solid" : "not solid"));
                    }
                    otherObjects += coll.mReport.mObjectsOfOtherKinds;
                    droppedTris += coll.mReport.mTrianglesDropped;
                    notFinite += coll.mReport.mShapesNotFinite;
                    if (coll.mReport.mOverCap)
                        ++overCap;
                    if (coll.mBodies.size() > 1)
                        ++multiBody;
                    std::set<std::string> sigsInFile;
                    bool anySolid = false;
                    for (const Body& b : coll.mBodies)
                    {
                        ++bodies;
                        if (b.mHasTransform)
                            ++bodiesT;
                        ++motion[std::to_string(b.mMotionType)];
                        ++quality[std::to_string(b.mQuality)];
                        ++response[std::to_string(b.mResponseType)];
                        ++layer[std::to_string(b.mLayer)];
                        ++flagsByLayer["layer " + std::to_string(b.mLayer) + " flags "
                            + std::to_string(b.mFilterFlags)];
                        ++layerClass["layer " + std::to_string(b.mLayer)
                            + (bodyBlocksActors(b) ? " solid" : " not solid")];
                        anySolid = anySolid || bodyBlocksActors(b);
                        if (b.mLayer != b.mWorldObjectLayer)
                            ++layerMismatch;
                        const bool moved
                            = std::abs(b.mTranslation[0]) + std::abs(b.mTranslation[1]) + std::abs(b.mTranslation[2])
                            > 1e-6f;
                        const bool rotated
                            = std::abs(b.mRotation[0]) + std::abs(b.mRotation[1]) + std::abs(b.mRotation[2]) > 1e-6f;
                        if (!b.mHasTransform)
                        {
                            plainBodyMoved += moved;
                            plainBodyRotated += rotated;
                        }
                        else
                            tBodyMoved += moved;
                        if (b.mShape == sNone)
                        {
                            ++noShape;
                            continue;
                        }
                        const std::string sig = std::string(b.mHasTransform ? "T:" : "") + signature(coll, b.mShape);
                        ++sigBodies[sig];
                        sigsInFile.insert(sig);
                    }
                    for (const std::string& s : sigsInFile)
                        ++sigFiles[s];
                    for (const Shape& s : coll.mShapes)
                    {
                        ++kinds[s.mTypeName];
                        if (s.mKind == ShapeKind::Unsupported)
                            ++unsupportedTypes[s.mTypeName];
                        if (s.mKind == ShapeKind::Transform)
                        {
                            ++xformShapes;
                            const bool lastRow
                                = s.mTransform[12] != 0.f || s.mTransform[13] != 0.f || s.mTransform[14] != 0.f;
                            const bool lastCol
                                = s.mTransform[3] != 0.f || s.mTransform[7] != 0.f || s.mTransform[11] != 0.f;
                            xformLastRow += lastRow;
                            xformLastColumn += lastCol;
                            xformBothZero += !lastRow && !lastCol;
                            xformCorner1 += s.mTransform[15] == 1.f;
                            bool identity = true;
                            for (int r = 0; r < 3; ++r)
                                for (int cc = 0; cc < 3; ++cc)
                                    identity = identity
                                        && std::abs(s.mTransform[r * 4 + cc] - (r == cc ? 1.f : 0.f)) < 1e-4f;
                            xformIdentityRotation += identity;
                            xformElement15[std::to_string(s.mTransform[15])]++;
                        }
                    }

                    // The scene, the drawn geometry and the correspondence check.
                    const NifScene::Scene scene = NifScene::buildScene(file, options);
                    const auto flat = NifScene::Flat::flatten(scene);
                    {
                        NifScene::Collision::Report visualReport;
                        const auto mesh = NifScene::Collision::buildMesh(flat, {}, &visualReport);
                        if (mesh.empty() && !visualReport.mOverCap)
                            ++visualEmptyModels[anySolid ? "bhk has a solid body" : "bhk bodies all not solid"];
                    }
                    std::unordered_map<std::uint32_t, std::uint32_t> nodeOfRecord;
                    for (std::uint32_t i = 0; i < scene.mNodes.size(); ++i)
                        nodeOfRecord.emplace(scene.mNodes[i].mRecordIndex, i);
                    for (const Body& b : coll.mBodies)
                    {
                        const auto n = nodeOfRecord.find(b.mNodeRecord);
                        if (n != nodeOfRecord.end() && !isIdentity(nodeWorld(scene, n->second)))
                            ++nonRootBody;
                    }

                    if (withColl % stride == 0 && !flat.mShapes.empty())
                    {
                        ++checked;
                        Visual visual;
                        visual.build(flat);
                        for (bool chunkXyzw : { true, false })
                            for (bool bodyXyzw : { true, false })
                            {
                                Limits limits;
                                limits.mChunkQuatXyzw = chunkXyzw;
                                limits.mRigidBodyQuatXyzw = bodyXyzw;
                                const Collision variant = readCollision(file, limits);
                                for (const Reading& reading : readings)
                                {
                                    if (reading.mChunkXyzw != chunkXyzw || reading.mBodyXyzw != bodyXyzw)
                                        continue;
                                    std::uint64_t filePoints = 0, fileMatched = 0;
                                    Score& score = scores[reading.name()];
                                    for (const Body& b : variant.mBodies)
                                    {
                                        const auto n = nodeOfRecord.find(b.mNodeRecord);
                                        if (n == nodeOfRecord.end() || b.mShape == sNone)
                                            continue;
                                        Pose bodyPose;
                                        if (b.mHasTransform || reading.mPlainBodyPlaces)
                                        {
                                            bodyPose = fromQuat(b.mRotation);
                                            bodyPose.t = { b.mTranslation[0], b.mTranslation[1], b.mTranslation[2] };
                                        }
                                        std::vector<V3> points;
                                        std::vector<Triangle> triangles;
                                        gather(variant, b.mShape, bodyPose, reading.mMatrixLayout, points, triangles);
                                        Pose world = nodeWorld(scene, n->second);
                                        auto toModel = [&](const V3& p) {
                                            return applyPose(world,
                                                V3{ p[0] * reading.mScale, p[1] * reading.mScale,
                                                    p[2] * reading.mScale });
                                        };
                                        const std::size_t step = std::max<std::size_t>(1, points.size() / 3000);
                                        const bool best = reading.mChunkXyzw && reading.mBodyXyzw
                                            && reading.mMatrixLayout == 0 && !reading.mPlainBodyPlaces
                                            && reading.mScale == 69.99125;
                                        const std::string category = std::string(b.mHasTransform ? "T:" : "")
                                            + signature(variant, b.mShape).substr(0, 60);
                                        for (std::size_t i = 0; i < points.size(); i += step)
                                        {
                                            ++filePoints;
                                            const V3 modelPoint = toModel(points[i]);
                                            const bool hit = !visual.near(modelPoint).empty();
                                            {
                                                bool in = true;
                                                for (int axis = 0; axis < 3; ++axis)
                                                    in = in && modelPoint[axis] >= flat.mBoundsMin[axis] - 10.0
                                                        && modelPoint[axis] <= flat.mBoundsMax[axis] + 10.0;
                                                auto& counter = inBounds[reading.name()
                                                    + (b.mHasTransform ? " | T body" : " | plain body")
                                                    + (signature(variant, b.mShape).find("Transform")
                                                                != std::string::npos
                                                            ? " | with transform shapes"
                                                            : "")];
                                                ++counter.first;
                                                counter.second += in;
                                            }
                                            fileMatched += hit;
                                            if (best)
                                            {
                                                ++byCategory[category].first;
                                                byCategory[category].second += hit;
                                            }
                                        }
                                        if (reading.mScale == 69.99125 && reading.mMatrixLayout == 0
                                            && !reading.mPlainBodyPlaces)
                                        {
                                            const std::size_t tstep = std::max<std::size_t>(1, triangles.size() / 2000);
                                            for (std::size_t i = 0; i < triangles.size(); i += tstep)
                                            {
                                                const auto ca = visual.near(toModel(triangles[i].a));
                                                const auto cb = visual.near(toModel(triangles[i].b));
                                                const auto cc = visual.near(toModel(triangles[i].c));
                                                bool done = false;
                                                for (const auto& a : ca)
                                                    for (const auto& bb : cb)
                                                        for (const auto& c : cc)
                                                        {
                                                            if (done || a.mShape != bb.mShape || a.mShape != c.mShape)
                                                                continue;
                                                            std::array<std::uint32_t, 3> v{ a.mIndex, bb.mIndex,
                                                                c.mIndex };
                                                            std::array<std::uint32_t, 3> sorted = v;
                                                            std::sort(sorted.begin(), sorted.end());
                                                            const auto found = visual.mTriangles.find(
                                                                { a.mShape, sorted[0], sorted[1], sorted[2] });
                                                            if (found == visual.mTriangles.end())
                                                                continue;
                                                            const int parity
                                                                = (v == sorted
                                                                      || v
                                                                          == std::array<std::uint32_t, 3>{ sorted[1],
                                                                              sorted[2], sorted[0] }
                                                                      || v
                                                                          == std::array<std::uint32_t, 3>{ sorted[2],
                                                                              sorted[0], sorted[1] })
                                                                ? 0
                                                                : 1;
                                                            ++score.mTriangles;
                                                            (parity == found->second ? score.mSameWinding
                                                                                     : score.mReversedWinding)++;
                                                            done = true;
                                                        }
                                            }
                                        }
                                    }
                                    if (filePoints > 0)
                                    {
                                        score.mPoints += filePoints;
                                        score.mMatched += fileMatched;
                                        ++score.mFiles;
                                        score.mGoodFiles += (fileMatched * 10 >= filePoints * 9);
                                    }
                                }
                            }
                    }

                    // Compressed mesh data checks, straight on the records.
                    for (const auto& record : file.mRecords)
                    {
                        if (const auto* shape = dynamic_cast<const Nif::bhkCompressedMeshShape*>(record.get()))
                        {
                            if (shape->mScale.x != 1.f || shape->mScale.y != 1.f || shape->mScale.z != 1.f)
                                ++cmsScaleNot1;
                            if (shape->mRadius != 0.f)
                                ++cmsRadiusSet;
                        }
                        const auto* cms = dynamic_cast<const Nif::bhkCompressedMeshShapeData*>(record.get());
                        if (cms == nullptr)
                            continue;
                        ++cmsData;
                        for (const Nif::bhkCMSChunk& stored : cms->mChunks)
                        {
                            ++cmsChunks;
                            ++transformIdx[stored.mTransformIndex < cms->mChunkTransforms.size() ? "valid index"
                                                                                                 : "no/invalid index"];
                            Chunk chunk;
                            chunk.mTranslation
                                = { stored.mTranslation.x, stored.mTranslation.y, stored.mTranslation.z, 0.f };
                            chunk.mVertices = stored.mVertices;
                            chunk.mIndices = stored.mIndices;
                            chunk.mStrips = stored.mStrips;
                            std::vector<Vec3> v;
                            std::vector<std::array<std::uint32_t, 3>> t;
                            cmsIdxBad += decodeChunk(chunk, nullptr, v, t);
                            if (stored.mTransformIndex < cms->mChunkTransforms.size())
                                ++cmsWithTransform;
                            const float eps = 0.05f;
                            bool inside = true;
                            for (const Vec3& p : v)
                                if (p[0] < cms->mAabbMin.x - eps || p[1] < cms->mAabbMin.y - eps
                                    || p[2] < cms->mAabbMin.z - eps || p[0] > cms->mAabbMax.x + eps
                                    || p[1] > cms->mAabbMax.y + eps || p[2] > cms->mAabbMax.z + eps)
                                    inside = false;
                            ++variantTotal["step 0.001 untransformed"];
                            variantInside["step 0.001 untransformed"] += inside;
                        }
                    }
                }
                catch (const std::exception&)
                {
                    ++failed;
                }
            }
        }

        line("files " + std::to_string(files) + " failed " + std::to_string(failed) + " with collision objects "
            + std::to_string(withColl) + " bodies " + std::to_string(bodies) + " rigidBodyT " + std::to_string(bodiesT)
            + " multi-body files " + std::to_string(multiBody) + " other collision objects "
            + std::to_string(otherObjects) + " bodies without shape " + std::to_string(noShape) + " over cap files "
            + std::to_string(overCap) + " triangles dropped " + std::to_string(droppedTris) + " shapes not finite "
            + std::to_string(notFinite) + " body/worldobject layer mismatch " + std::to_string(layerMismatch));
        line("bodies on a node whose world pose is not the identity " + std::to_string(nonRootBody)
            + "; plain (non-T) bodies with a translation " + std::to_string(plainBodyMoved) + " and a rotation "
            + std::to_string(plainBodyRotated) + "; T bodies with a translation " + std::to_string(tBodyMoved));
        line("transform shapes " + std::to_string(xformShapes) + ": last-row translation nonzero "
            + std::to_string(xformLastRow) + ", last-column translation nonzero " + std::to_string(xformLastColumn)
            + ", both zero " + std::to_string(xformBothZero) + ", element 15 equals 1 " + std::to_string(xformCorner1));
        line("transform shapes with an identity 3x3 " + std::to_string(xformIdentityRotation) + " of "
            + std::to_string(xformShapes));
        for (const auto& [k, n] : xformElement15)
            line("transform element 15 = " + k + ": " + std::to_string(n));
        auto dump = [](const std::string& label, const std::map<std::string, std::uint64_t>& m) {
            for (const auto& [k, n] : m)
                line(label + " " + k + ": " + std::to_string(n));
        };
        for (const auto& [k, n] : sigBodies)
            line("signature " + k + ": bodies " + std::to_string(n) + " files " + std::to_string(sigFiles[k]));
        dump("motion", motion);
        dump("quality", quality);
        dump("response", response);
        dump("layer", layer);
        dump("layer classified", layerClass);
        dump("filter", flagsByLayer);
        dump("shape block", kinds);
        dump("unsupported block", unsupportedTypes);
        dump("model with no collidable visual triangles:", visualEmptyModels);
        for (const auto& [k, v] : byCategory)
            line("best reading, points by body signature [" + k + "]: matched " + std::to_string(v.second) + " of "
                + std::to_string(v.first));
        for (const auto& [k, v] : inBounds)
            line("inside drawn bounds [" + k + "]: " + std::to_string(v.second) + " of " + std::to_string(v.first));
        line("correspondence checked on " + std::to_string(checked) + " files");
        for (const auto& [k, s] : scores)
            line("reading [" + k + "]: points matched " + std::to_string(s.mMatched) + " of "
                + std::to_string(s.mPoints) + ", files with 90 percent matched " + std::to_string(s.mGoodFiles) + " of "
                + std::to_string(s.mFiles)
                + (s.mTriangles
                        ? ", triangles matched to a visual triangle " + std::to_string(s.mTriangles) + " same winding "
                            + std::to_string(s.mSameWinding) + " reversed " + std::to_string(s.mReversedWinding)
                        : std::string()));
        line("compressed mesh data " + std::to_string(cmsData) + " chunks " + std::to_string(cmsChunks)
            + " with transform " + std::to_string(cmsWithTransform) + " bad indices " + std::to_string(cmsIdxBad)
            + " shape scale not 1 " + std::to_string(cmsScaleNot1) + " shape radius set "
            + std::to_string(cmsRadiusSet));
        dump("chunk transform index", transformIdx);
        for (const auto& [k, n] : variantTotal)
            line("chunk inside data bounds [" + k + "]: " + std::to_string(variantInside[k]) + " of "
                + std::to_string(n));
        SUCCEED();
    }
}
