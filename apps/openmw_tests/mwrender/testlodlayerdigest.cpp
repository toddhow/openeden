#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <vsg/io/Options.h>

#include <components/terrain/bounds.hpp>
#include <components/terrain/storage.hpp>
#include <components/vfs/manager.hpp>

#include "apps/components_tests/vfs/vfsfakes.hpp"
#include "apps/openmw/mwrender/lodterrain.hpp"

// The digest of a land texture layer goes into the staleness digest of the terrain tiles of level 2 and up (task 0060,
// review of the far eviction, finding 6). The review's guess was that it depended on how the file lookup resolves on a
// platform; the cause was elsewhere (the corner normals, see testtilebake.cpp), and a Windows and a Linux bake are now
// byte for byte equal. These tests pin that the digest follows the layer's name and the file's bytes, and not the
// spelling of the path the lookup returns.
namespace
{
    // A storage that only names layers; the digest never asks it for anything else.
    class LayerStorage final : public Terrain::Storage
    {
    public:
        LayerStorage()
            : Terrain::Storage(8192.f, 65)
        {
        }
        VertexData getVertexData(int, const Terrain::Bounds&) override { return {}; }
        vsg::ref_ptr<vsg::Data> getBlendmap(const Terrain::Bounds&) override { return {}; }
        std::vector<Terrain::LayerInfo> getLayers() override { return {}; }
        float getHeightAt(const vsg::vec3&, ESM::RefId) override { return 0.f; }
    };

    vsg::ref_ptr<const vsg::Options> optionsReturning(std::string spelling)
    {
        auto options = vsg::Options::create();
        options->findFileCallback
            = [spelling](const vsg::Path&, const vsg::Options*) -> vsg::Path { return vsg::Path(spelling); };
        return options;
    }

    struct Fixture
    {
        VFS::Manager vfs;
        LayerStorage storage;

        explicit Fixture(const std::string& content)
        {
            VFSTest::addFakeArchive(vfs, "textures").add("textures/tx_a.dds", content);
            vfs.buildIndex();
        }

        EdenArchive::Hash digest(const std::string& spelling, const std::string& layerName = "tx_a.dds")
        {
            MWRender::StorageAlbedoSource source(storage, { layerName }, optionsReturning(spelling), &vfs);
            return source.layerDigest(0);
        }
    };

    TEST(LodLayerDigest, does_not_depend_on_the_spelling_of_the_path_the_lookup_returns)
    {
        Fixture fixture("the bytes of the texture");
        const EdenArchive::Hash plain = fixture.digest("textures/tx_a.dds");
        EXPECT_EQ(plain, fixture.digest("Textures\\TX_A.DDS"));
        EXPECT_EQ(plain, fixture.digest("TEXTURES/Tx_A.dds"));
        EXPECT_NE(plain, EdenArchive::Hash{});
    }

    TEST(LodLayerDigest, follows_the_bytes_of_the_file_and_the_name_of_the_layer)
    {
        Fixture one("one");
        Fixture other("other");
        EXPECT_NE(one.digest("textures/tx_a.dds"), other.digest("textures/tx_a.dds"));
        EXPECT_NE(one.digest("textures/tx_a.dds", "tx_a.dds"), one.digest("textures/tx_a.dds", "tx_b.dds"));
    }

    TEST(LodLayerDigest, a_file_the_lookup_does_not_find_is_reported_and_is_not_the_digest_of_the_file)
    {
        Fixture fixture("the bytes of the texture");
        MWRender::StorageAlbedoSource missing(
            fixture.storage, { "tx_a.dds" }, optionsReturning("textures/nothing.dds"), &fixture.vfs);
        EXPECT_NE(missing.layerDigest(0), fixture.digest("textures/tx_a.dds"));
        EXPECT_NE(missing.describeLayer(0).find("exists no"), std::string::npos) << missing.describeLayer(0);
        MWRender::StorageAlbedoSource found(
            fixture.storage, { "tx_a.dds" }, optionsReturning("textures/tx_a.dds"), &fixture.vfs);
        EXPECT_NE(found.describeLayer(0).find("exists yes"), std::string::npos) << found.describeLayer(0);
    }
}
