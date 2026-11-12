// Task 0147: SHA-256 against the FIPS 180-4 / NIST example vectors.
#include <components/face/sha256.hpp>

#include <gtest/gtest.h>

#include <string>

TEST(FaceSha256, emptyString)
{
    EXPECT_EQ(Face::sha256Hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(FaceSha256, abc)
{
    EXPECT_EQ(Face::sha256Hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(FaceSha256, twoBlockMessage)
{
    EXPECT_EQ(Face::sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(FaceSha256, aMillionAs)
{
    Face::Sha256 hash;
    const std::string chunk(1000, 'a');
    for (int i = 0; i < 1000; ++i)
        hash.update(chunk);
    EXPECT_EQ(hash.finishHex(), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(FaceSha256, splitUpdatesGiveTheSameDigestAsOne)
{
    const std::string text(1000, 'x');
    for (std::size_t cut : { std::size_t{ 0 }, std::size_t{ 1 }, std::size_t{ 55 }, std::size_t{ 56 },
             std::size_t{ 63 }, std::size_t{ 64 }, std::size_t{ 65 }, std::size_t{ 999 } })
    {
        Face::Sha256 hash;
        hash.update(text.data(), cut);
        hash.update(text.data() + cut, text.size() - cut);
        EXPECT_EQ(hash.finishHex(), Face::sha256Hex(text)) << "cut " << cut;
    }
}

TEST(FaceSha256, theLengthPaddingBoundaries)
{
    EXPECT_EQ(
        Face::sha256Hex(std::string(55, 'a')), "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    EXPECT_EQ(
        Face::sha256Hex(std::string(56, 'a')), "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    EXPECT_EQ(
        Face::sha256Hex(std::string(64, 'a')), "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
}
