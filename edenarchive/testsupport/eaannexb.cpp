// Eden Archive test support (task 0052). Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
//
// Annex B of the specification: the worked valid pack. The hexadecimal dump below is copied from the specification
// text (docs/spec/eden-archive-format-v0.1.md, section B.1) and parsed back into bytes at run time, so that the
// independent builder can be checked against it byte for byte.
#include <cctype>
#include <string>

#include "eapackmodel.hpp"

namespace EdenTest
{
    namespace
    {
        // Copied from the specification. A line with a star means that all rows between the previous printed row and
        // the next printed row repeat the previous printed row.
        constexpr const char* annexBDump = R"DUMP(
00000000  45 44 45 4e 50 41 4b 01  00 00 01 00 00 02 00 00  |EDENPAK.........|
00000010  00 00 00 00 00 00 00 00  c0 42 00 00 00 00 00 00  |.........B......|
00000020  00 00 02 00 70 00 00 00  38 00 00 00 0b 29 00 00  |....p...8....)..|
00000030  03 00 00 00 00 00 00 00  03 00 00 00 00 00 00 00  |................|
00000040  02 00 00 00 00 00 00 00  01 00 00 00 00 00 00 00  |................|
00000050  00 10 00 00 00 00 00 00  04 20 00 00 00 00 00 00  |......... ......|
00000060  00 40 00 00 00 00 00 00  40 02 00 00 00 00 00 00  |.@......@.......|
00000070  00 40 00 00 00 00 00 00  18 00 00 00 00 00 00 00  |.@..............|
00000080  18 40 00 00 00 00 00 00  68 41 00 00 00 00 00 00  |.@......hA......|
00000090  88 41 00 00 00 00 00 00  98 41 00 00 00 00 00 00  |.A.......A......|
000000a0  a8 00 00 00 00 00 00 00  40 42 00 00 00 00 00 00  |........@B......|
000000b0  00 00 00 00 00 00 00 00  40 42 00 00 00 00 00 00  |........@B......|
000000c0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
000000f0  62 50 f4 96 c3 63 9c 22  e6 a6 a9 a9 e4 b2 39 6c  |bP...c."......9l|
00000100  ab 02 cf 56 51 3f 70 22  49 5d 5e d0 bc 73 03 8c  |...VQ?p"I]^..s..|
00000110  a5 fd 5a cb 7a 03 5f 38  79 23 5b 0b 8f dd d2 05  |..Z.z._8y#[.....|
00000120  e2 1b c1 44 09 a7 c9 6a  98 83 3c 0e 5b 6b 97 29  |...D...j..<.[k.)|
00000130  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
000001e0  e4 70 c0 2b 69 7d 9a 5c  11 54 28 e3 27 a6 5a 4c  |.p.+i}.\.T(.'.ZL|
000001f0  c8 7d 6e 4a 8e 40 e9 66  42 a7 6d 0b 11 08 a7 aa  |.}nJ.@.fB.m.....|
00000200  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00001000  28 b5 2f fd 20 40 03 02  00 41 00 00 00 00 00 00  |(./. @...A......|
00001010  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00002000  48 65 6c 6c 6f 2c 20 45  64 65 6e 21 0a 00 00 00  |Hello, Eden!....|
00002010  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00003000  54 49 4c 45 00 00 00 00  00 00 00 00 00 00 00 00  |TILE............|
00003010  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00004000  64 61 74 61 2f 62 6c 6f  62 2e 62 69 6e 72 65 61  |data/blob.binrea|
00004010  64 6d 65 2e 74 78 74 00  00 00 00 00 00 00 00 00  |dme.txt.........|
00004020  00 00 00 00 00 00 00 00  0d 00 00 00 00 00 00 00  |................|
00004030  01 00 00 00 01 00 00 00  40 00 00 00 00 00 00 00  |........@.......|
00004040  98 41 00 00 00 00 00 00  e0 28 42 4e 46 20 5e 56  |.A.......(BNF ^V|
00004050  b2 ed 1c e1 bf 70 87 05  40 72 e6 c4 e4 1f 84 3b  |.....p..@r.....;|
00004060  ed 1e 74 9d b6 35 79 2c  00 00 00 00 00 00 00 00  |..t..5y,........|
00004070  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00004090  0d 00 00 00 00 00 00 00  0a 00 00 00 00 00 00 00  |................|
000040a0  01 00 00 00 01 00 00 00  0d 00 00 00 00 00 00 00  |................|
000040b0  d0 41 00 00 00 00 00 00  e7 61 99 c0 df 40 59 c1  |.A.......a...@Y.|
000040c0  c8 32 a5 cf f8 ce b7 dc  e5 bb 05 52 79 b0 df 69  |.2.........Ry..i|
000040d0  04 b1 9a 44 a3 0d 2a cd  00 00 00 00 00 00 00 00  |...D..*.........|
000040e0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
000040f0  00 00 00 00 00 00 00 00  01 00 00 00 00 00 00 10  |................|
00004100  00 00 00 00 00 00 00 00  00 00 00 00 10 00 00 00  |................|
00004110  02 00 01 00 01 00 00 00  04 00 00 00 00 00 00 00  |................|
00004120  08 42 00 00 00 00 00 00  ce 30 1e 4f 3c 6f 79 65  |.B.......0.O<oye|
00004130  e5 52 74 99 1c 0e 43 25  7e dc d6 1b 51 3d 60 a2  |.Rt...C%~...Q=`.|
00004140  b3 01 bb d0 25 7d 41 ac  00 00 00 00 00 00 00 00  |....%}A.........|
00004150  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
00004160  00 00 00 00 00 00 00 00  28 6e 99 97 e3 a1 1b 68  |........(n.....h|
00004170  00 00 00 00 00 00 00 00  8a 95 6b ea db 69 99 b9  |..........k..i..|
00004180  01 00 00 00 00 00 00 00  01 00 00 00 00 00 00 10  |................|
00004190  02 00 00 00 00 00 00 00  00 10 00 00 00 00 00 00  |................|
000041a0  0a 00 00 00 40 00 00 00  01 00 01 00 00 00 00 00  |....@...........|
000041b0  e0 28 42 4e 46 20 5e 56  b2 ed 1c e1 bf 70 87 05  |.(BNF ^V.....p..|
000041c0  40 72 e6 c4 e4 1f 84 3b  ed 1e 74 9d b6 35 79 2c  |@r.....;..t..5y,|
000041d0  00 20 00 00 00 00 00 00  0d 00 00 00 0d 00 00 00  |. ..............|
000041e0  00 00 01 00 00 00 00 00  e7 61 99 c0 df 40 59 c1  |.........a...@Y.|
000041f0  c8 32 a5 cf f8 ce b7 dc  e5 bb 05 52 79 b0 df 69  |.2.........Ry..i|
00004200  04 b1 9a 44 a3 0d 2a cd  00 30 00 00 00 00 00 00  |...D..*..0......|
00004210  04 00 00 00 04 00 00 00  00 00 01 00 00 00 00 00  |................|
00004220  ce 30 1e 4f 3c 6f 79 65  e5 52 74 99 1c 0e 43 25  |.0.O<oye.Rt...C%|
00004230  7e dc d6 1b 51 3d 60 a2  b3 01 bb d0 25 7d 41 ac  |~...Q=`.....%}A.|
00004240  45 44 45 4e 45 4e 44 01  00 00 01 00 80 00 00 00  |EDENEND.........|
00004250  c0 42 00 00 00 00 00 00  00 40 00 00 00 00 00 00  |.B.......@......|
00004260  40 02 00 00 00 00 00 00  a5 fd 5a cb 7a 03 5f 38  |@.........Z.z._8|
00004270  79 23 5b 0b 8f dd d2 05  e2 1b c1 44 09 a7 c9 6a  |y#[........D...j|
00004280  98 83 3c 0e 5b 6b 97 29  e4 70 c0 2b 69 7d 9a 5c  |..<.[k.).p.+i}.\|
00004290  11 54 28 e3 27 a6 5a 4c  c8 7d 6e 4a 8e 40 e9 66  |.T(.'.ZL.}nJ.@.f|
000042a0  42 a7 6d 0b 11 08 a7 aa  00 00 00 00 00 00 00 00  |B.m.............|
000042b0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
)DUMP";
    }

    Bytes annexBGolden()
    {
        Bytes out;
        Bytes previousRow;
        bool repeatPending = false;
        const std::string text = annexBDump;
        std::size_t at = 0;
        while (at < text.size())
        {
            std::size_t end = text.find('\n', at);
            if (end == std::string::npos)
                end = text.size();
            const std::string line = text.substr(at, end - at);
            at = end + 1;
            if (line.empty())
                continue;
            if (line[0] == '*')
            {
                repeatPending = true;
                continue;
            }
            // "00000000  45 44 ...  |ASCII|": the offset, then hexadecimal pairs up to the bar.
            const std::uint64_t offset = std::stoull(line.substr(0, 8), nullptr, 16);
            Bytes row;
            const std::size_t bar = line.find('|');
            for (std::size_t i = 10; i + 1 < bar; ++i)
            {
                if (std::isxdigit(static_cast<unsigned char>(line[i])) && std::isxdigit(static_cast<unsigned char>(line[i + 1])))
                {
                    row.push_back(static_cast<std::uint8_t>(std::stoul(line.substr(i, 2), nullptr, 16)));
                    ++i;
                }
            }
            if (repeatPending)
            {
                while (out.size() + 16 <= offset)
                    appendBytes(out, previousRow);
                repeatPending = false;
            }
            appendBytes(out, row);
            previousRow = row;
        }
        return out;
    }

    PackSpec annexBSpec()
    {
        PackSpec spec;
        spec.mChunkSize = 131072;
        spec.mCompressorVersion = 10507; // zstd 1.5.7, as Annex B says
        spec.mCompressorLevel = 3;

        EntrySpec blob;
        blob.mPath = "data/blob.bin";
        blob.mContent = Bytes(64, 0x41);
        blob.mCodec = CodecChoice::HandRle; // the hand-built frame of Annex B
        spec.mEntries.push_back(blob);

        EntrySpec readme;
        readme.mPath = "readme.txt";
        readme.mContent = bytesOf("Hello, Eden!\n");
        spec.mEntries.push_back(readme);

        EntrySpec tile;
        tile.mKey = 0x1000000000000001ull;
        tile.mType = 0x0010;
        tile.mExtraFlags = Fmt::entryPinned;
        tile.mContent = bytesOf("TILE");
        spec.mEntries.push_back(tile);
        return spec;
    }

    Bytes annexBPack()
    {
        return buildPack(annexBSpec());
    }
}
