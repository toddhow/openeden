#include <gtest/gtest.h>

#include <components/contentsource/decoderregistry.hpp>
#include <components/contentsource/plugin.hpp>

#include <string>
#include <vector>

// Task 0083, layer 2: the registry that maps a record type to its decoder.
namespace
{
    struct Context
    {
        std::vector<std::string> mLog;
    };

    using Registry = Content::DecoderRegistry<Context>;

    TEST(ContentDecoderRegistry, an_empty_registry_finds_nothing)
    {
        const Registry registry;
        EXPECT_EQ(registry.size(), 0u);
        EXPECT_EQ(registry.find(Content::fourCC("STAT")), nullptr);
    }

    TEST(ContentDecoderRegistry, a_registered_decoder_is_found_and_called_with_the_context)
    {
        Registry registry;
        EXPECT_TRUE(registry.add(Content::fourCC("STAT"), [](Context& c) {
            c.mLog.push_back("stat");
            return true;
        }));
        EXPECT_EQ(registry.size(), 1u);
        const Registry::Decoder* decoder = registry.find(Content::fourCC("STAT"));
        ASSERT_NE(decoder, nullptr);
        Context context;
        EXPECT_TRUE((*decoder)(context));
        EXPECT_EQ(context.mLog, std::vector<std::string>{ "stat" });
        EXPECT_EQ(registry.find(Content::fourCC("NPC_")), nullptr);
    }

    TEST(ContentDecoderRegistry, the_result_of_a_decoder_is_passed_back)
    {
        Registry registry;
        registry.add(Content::fourCC("DELE"), [](Context&) { return false; });
        Context context;
        EXPECT_FALSE((*registry.find(Content::fourCC("DELE")))(context));
    }

    TEST(ContentDecoderRegistry, a_second_registration_of_a_type_is_refused_and_the_first_stays)
    {
        Registry registry;
        EXPECT_TRUE(registry.add(Content::fourCC("STAT"), [](Context& c) {
            c.mLog.push_back("first");
            return true;
        }));
        EXPECT_FALSE(registry.add(Content::fourCC("STAT"), [](Context& c) {
            c.mLog.push_back("second");
            return true;
        }));
        EXPECT_EQ(registry.size(), 1u);
        Context context;
        (*registry.find(Content::fourCC("STAT")))(context);
        EXPECT_EQ(context.mLog, std::vector<std::string>{ "first" });
    }

    TEST(ContentDecoderRegistry, an_empty_decoder_is_refused)
    {
        Registry registry;
        EXPECT_FALSE(registry.add(Content::fourCC("STAT"), Registry::Decoder()));
        EXPECT_EQ(registry.find(Content::fourCC("STAT")), nullptr);
    }

    TEST(ContentDecoderRegistry, decoders_are_listed_in_type_order_whatever_the_registration_order)
    {
        Registry a;
        Registry b;
        const char* names[] = { "ZZZZ", "AAAA", "MMMM" };
        for (const char* name : names)
        {
            a.add(Content::fourCC("AAAA") + static_cast<std::uint32_t>(name[0]), [](Context&) { return true; });
        }
        for (int i = 2; i >= 0; --i)
        {
            b.add(Content::fourCC("AAAA") + static_cast<std::uint32_t>(names[i][0]), [](Context&) { return true; });
        }
        std::vector<std::uint32_t> keysA;
        std::vector<std::uint32_t> keysB;
        for (const auto& [type, decoder] : a.decoders())
        {
            keysA.push_back(type);
        }
        for (const auto& [type, decoder] : b.decoders())
        {
            keysB.push_back(type);
        }
        EXPECT_EQ(keysA, keysB);
        EXPECT_EQ(keysA.size(), 3u);
        EXPECT_TRUE(keysA[0] < keysA[1] && keysA[1] < keysA[2]);
    }

    TEST(ContentDecoderRegistry, records_without_a_decoder_are_counted_by_type)
    {
        Registry registry;
        registry.countSkipped(Content::fourCC("ZZZZ"));
        registry.countSkipped(Content::fourCC("ZZZZ"));
        registry.countSkipped(Content::fourCC("YYYY"));
        ASSERT_EQ(registry.skipped().size(), 2u);
        EXPECT_EQ(registry.skipped().at(Content::fourCC("ZZZZ")), 2u);
        EXPECT_EQ(registry.skipped().at(Content::fourCC("YYYY")), 1u);
    }
}
