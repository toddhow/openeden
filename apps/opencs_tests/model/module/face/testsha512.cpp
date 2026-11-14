// Task 0147 slice 3: SHA-512 against the FIPS 180-4 examples and Python's hashlib.
#include "editor_modules/face_editor/sha512.hpp"

#include <gtest/gtest.h>

namespace
{
    using face_editor::Sha512;
    using face_editor::sha512Hex;

    TEST(Sha512, emptyMessage)
    {
        EXPECT_EQ(sha512Hex(""),
            "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd4"
            "7417a81a538327af927da3e");
    }

    TEST(Sha512, abc)
    {
        EXPECT_EQ(sha512Hex("abc"),
            "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d442364"
            "3"
            "ce80e2a9ac94fa54ca49f");
    }

    TEST(Sha512, theTwoBlockExample)
    {
        EXPECT_EQ(sha512Hex("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlm"
                            "nopqrsmnopqrstnopqrstu"),
            "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018501d289e4900f7e4331b99dec4b5433ac7d329eeb6"
            "d"
            "d26545e96e55b874be909");
    }

    TEST(Sha512, aMillionAs)
    {
        EXPECT_EQ(sha512Hex(std::string(1000000, 'a')),
            "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973ebde0ff244877ea60a4cb0432ce577c31beb009c5c2c"
            "4"
            "9aa2e4eadb217ad8cc09b");
    }

    TEST(Sha512, streamingInOddChunksEqualsOneShot)
    {
        std::string data(100000, '\0');
        for (std::size_t i = 0; i < data.size(); ++i)
            data[i] = static_cast<char>((i * 7) % 251);
        const char* expected
            = "9029cfe7fbc63001366ae9d2d34114346c645122c01a20c83aab2b2c71b2fc956bb4ee48120885facb9dbd9ade46b39ddc3cf38e"
              "b33"
              "3be9ac74bafaba6149af1";
        EXPECT_EQ(sha512Hex(data), expected);
        for (std::size_t chunk : { std::size_t{ 1 }, std::size_t{ 3 }, std::size_t{ 127 }, std::size_t{ 128 },
                 std::size_t{ 129 }, std::size_t{ 4097 } })
        {
            Sha512 h;
            for (std::size_t at = 0; at < data.size(); at += chunk)
                h.update(data.data() + at, std::min(chunk, data.size() - at));
            EXPECT_EQ(h.finishHex(), expected) << "chunk " << chunk;
        }
    }
}
