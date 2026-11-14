// tools/face_check (task 0071): writes a TEST PATTERN curves file for one voice line, in the packed binary form. Not a
// lip-sync generator: the jaw follows a triangle wave so that a frame sequence shows the mouth against a known curve.
//
//   mkcurves <out.facecurves> <audio name> <duration seconds> <audio sha256> [json]
//
// jawopen: 0 until 0.3 s, then a triangle wave of period 0.4 s (0 -> 1 -> 0) until 1.9 s, then 0.
// eyeblinkleft: 1.0 from 1.0 s to 1.2 s (a ramp of one frame each side), else 0. 30 frames per second.
// Build (MSVC): cl /std:c++20 /EHsc /I <repo> mkcurves.cpp components/face/facecurves.cpp
// components/face/faceimport.cpp
#include <components/face/faceimport.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 5)
    {
        std::fprintf(stderr, "usage: mkcurves <out> <audio name> <duration> <sha256> [json]\n");
        return 2;
    }
    const double duration = std::atof(argv[3]);
    const unsigned rate = 30;
    const unsigned frames = static_cast<unsigned>(std::ceil(duration * rate));
    std::ostringstream csv;
    csv << "jawopen,eyeblinkleft\n";
    for (unsigned i = 0; i < frames; ++i)
    {
        const double t = i / static_cast<double>(rate);
        double jaw = 0.0;
        if (t >= 0.3 && t < 1.9)
        {
            const double phase = std::fmod(t - 0.3, 0.4) / 0.4;
            jaw = phase < 0.5 ? phase * 2.0 : (1.0 - phase) * 2.0;
        }
        double blink = 0.0;
        if (t >= 1.0 && t < 1.2)
            blink = 1.0;
        csv << jaw << ',' << blink << '\n';
    }
    Face::ImportMeta meta;
    meta.mAudioName = argv[2];
    meta.mAudioDurationSeconds = duration;
    meta.mAudioSha256 = argv[4];
    meta.mGeneratorName = "mkcurves-test-pattern";
    meta.mGeneratorVersion = "1";
    meta.mFrameRate = rate;
    Face::LoadError error;
    const auto curves = Face::importCsv(csv.str(), "pattern.csv", meta, {}, error);
    if (!curves)
    {
        std::fprintf(stderr, "%s\n", error.format().c_str());
        return 1;
    }
    std::ofstream out(argv[1], std::ios::binary);
    if (argc > 5)
    {
        const auto text = Face::writeJson(*curves);
        out << *text;
    }
    else
    {
        const auto bytes = Face::writeBinary(*curves);
        out.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
    }
    std::printf("wrote %u frames\n", frames);
    return 0;
}
