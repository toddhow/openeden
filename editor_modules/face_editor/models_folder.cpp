#include "models_folder.hpp"

#include <components/face/sha256.hpp>

#include <fstream>
#include <sstream>
#include <system_error>

namespace face_editor
{
    namespace fs = std::filesystem;

    namespace
    {
        // File names, sizes and SHA-256 recorded from toddhow's downloads of 2026-11-12 (docs/tasks/0071-...md, "Stage
        // a results"). Only network.onnx is hashed: it is the file that decides the output and the one that is big
        // enough to be corrupted by a failed download; the others are small and checked by size.
        std::vector<VariantSpec> makeVariants()
        {
            std::vector<VariantSpec> v;

            VariantSpec v30;
            v30.mId = "v3.0";
            v30.mLabel = "Audio2Face-3D v3.0 (high quality)";
            v30.mSource
                = "huggingface.co/nvidia, repository Audio2Face-3D-v3.0 (name from the download; the page itself was "
                  "not checked)";
            v30.mLicence = "NVIDIA Open Model License";
            v30.mFiles = {
                { "network.onnx", 724844664, "db47c2701ca849de443c9e9f25657210f829a74fc458ee6fed603a8a501253a8" },
                { "network_info.json", 1233, "" },
                { "model.json", 1313, "" },
                { "model_config_Mark.json", 911, "" },
                { "model_data_Mark.npz", 972958, "" },
                { "bs_skin_Mark.npz", 15297809, "" },
                { "bs_skin_config_Mark.json", 4947, "" },
                { "bs_tongue_Mark.npz", 1147927, "" },
                { "bs_tongue_config_Mark.json", 1870, "" },
            };
            v.push_back(std::move(v30));

            VariantSpec v23;
            v23.mId = "v2.3-mark";
            v23.mLabel = "Audio2Face-3D v2.3 Mark (portable)";
            v23.mSource = "https://huggingface.co/nvidia/Audio2Face-3D-v2.3-Mark";
            v23.mLicence = "NVIDIA Open Model License";
            v23.mFiles = {
                { "network.onnx", 75751144, "dcd16d3b1affb090d4da1ba88791faa6bcd755f97c853b3b667abad9be15cb4b" },
                { "network_info.json", 1015, "" },
                { "model.json", 484, "" },
                { "model_config.json", 984, "" },
                { "model_data.npz", 203797682, "" },
                { "bs_skin.npz", 39182291, "" },
                { "bs_skin_config.json", 4944, "" },
                { "bs_tongue.npz", 1147669, "" },
                { "bs_tongue_config.json", 1870, "" },
                { "implicit_emo_db.npz", 1319768, "" },
                { "a2f_ms_config.json", 7413, "" },
            };
            v.push_back(std::move(v23));

            VariantSpec emo;
            emo.mId = "audio2emotion";
            emo.mLabel = "Audio2Emotion (optional emotion model)";
            emo.mSource = "huggingface.co/nvidia, the Audio2Emotion repository (gated: accept its own licence there)";
            emo.mLicence
                = "its own licence: accept it on its own page; use only together with Audio2Face; not for "
                  "emotion recognition";
            emo.mOptional = true;
            emo.mFiles = {
                { "network.onnx", 1271318264, "92eab06c234b78ee3d5efe4762d0ac198cf39e085f942fe8e1065120e3d411b7" },
                { "network_info.json", 133, "" },
                { "model.json", 125, "" },
                { "model_config.json", 626, "" },
            };
            v.push_back(std::move(emo));
            return v;
        }

        constexpr std::size_t kReadChunk = 1u << 20;
    }

    const std::vector<VariantSpec>& knownVariants()
    {
        static const std::vector<VariantSpec> sVariants = makeVariants();
        return sVariants;
    }

    fs::path modelsRoot(const fs::path& userData)
    {
        return userData / "models" / "audio2face";
    }

    std::string stateText(VariantState state)
    {
        switch (state)
        {
            case VariantState::Missing:
                return "Missing";
            case VariantState::Incomplete:
                return "Incomplete";
            case VariantState::WrongSize:
                return "Wrong size";
            case VariantState::Present:
                return "Present (hashes not verified)";
            case VariantState::Verified:
                return "Verified";
            case VariantState::HashMismatch:
                return "Hash mismatch";
        }
        return "Unknown";
    }

    std::vector<VariantStatus> scanModels(const fs::path& root)
    {
        std::vector<VariantStatus> result;
        for (const VariantSpec& spec : knownVariants())
        {
            VariantStatus status;
            status.mId = spec.mId;
            status.mLabel = spec.mLabel;
            std::error_code ec;
            const fs::path folder = root / spec.mId;
            if (!fs::is_directory(folder, ec))
            {
                status.mState = VariantState::Missing;
                status.mDetail = "put the files in " + folder.generic_string();
                result.push_back(std::move(status));
                continue;
            }
            status.mState = VariantState::Present;
            for (const ExpectedFile& file : spec.mFiles)
            {
                const fs::path path = folder / file.mName;
                ec.clear();
                if (!fs::is_regular_file(path, ec))
                {
                    status.mState = VariantState::Incomplete;
                    status.mDetail = "missing " + file.mName;
                    break;
                }
                const auto size = fs::file_size(path, ec);
                if (ec || size != file.mSize)
                {
                    status.mState = VariantState::WrongSize;
                    status.mDetail = file.mName + " has "
                        + (ec ? std::string("an unreadable size") : std::to_string(size)) + " bytes, expected "
                        + std::to_string(file.mSize);
                    break;
                }
            }
            result.push_back(std::move(status));
        }
        return result;
    }

    VariantStatus verifyVariant(
        const fs::path& root, const VariantSpec& spec, const std::function<bool(double)>& progress)
    {
        VariantStatus status;
        status.mId = spec.mId;
        status.mLabel = spec.mLabel;
        std::uint64_t total = 0;
        for (const ExpectedFile& file : spec.mFiles)
            if (!file.mSha256.empty())
                total += file.mSize;
        std::uint64_t done = 0;
        status.mState = VariantState::Verified;
        std::vector<char> buffer(kReadChunk);
        for (const ExpectedFile& file : spec.mFiles)
        {
            if (file.mSha256.empty())
                continue;
            const fs::path path = root / spec.mId / file.mName;
            std::ifstream in(path, std::ios::binary);
            if (!in)
            {
                status.mState = VariantState::Incomplete;
                status.mDetail = "cannot read " + file.mName;
                return status;
            }
            Face::Sha256 hash;
            while (in)
            {
                in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const auto got = in.gcount();
                if (got <= 0)
                    break;
                hash.update(buffer.data(), static_cast<std::size_t>(got));
                done += static_cast<std::uint64_t>(got);
                if (progress && !progress(total != 0 ? std::min(1.0, static_cast<double>(done) / total) : 1.0))
                {
                    status.mState = VariantState::Present;
                    status.mDetail = "verification cancelled";
                    return status;
                }
            }
            if (hash.finishHex() != file.mSha256)
            {
                status.mState = VariantState::HashMismatch;
                status.mDetail = file.mName + " does not match the recorded SHA-256 (a damaged or different download)";
                return status;
            }
        }
        if (progress)
            progress(1.0);
        return status;
    }

    std::string readmeText()
    {
        std::ostringstream t;
        t << "Eden Studio: Audio2Face models folder\n"
             "=====================================\n\n"
             "Eden Studio generates facial animation curves for voice lines with NVIDIA's Audio2Face-3D networks. The\n"
             "networks are NOT part of OpenEden, are not bundled and are not hosted by us: you download them "
             "yourself,\n"
             "under NVIDIA's licence, and put them in this folder. Studio looks here, lists what it finds and checks "
             "the\n"
             "files against the sizes and SHA-256 hashes below (Face > Verify Models). Nothing is ever uploaded.\n\n"
             "Licence. The Audio2Face models are published under the NVIDIA Open Model License. By downloading them "
             "you\n"
             "accept it; read it on the model's page. Audio2Emotion has its OWN licence, which you accept separately "
             "on\n"
             "its page: it may be used only together with Audio2Face and not for emotion recognition. Curves you "
             "generate\n"
             "are yours, but the audio they were generated from may not be: do not ship curves made from audio you "
             "have\n"
             "no right to.\n\n"
             "Layout: one folder per variant, named exactly as below, with the files directly inside it.\n\n";
        for (const VariantSpec& spec : knownVariants())
        {
            t << spec.mId << "/   " << spec.mLabel << (spec.mOptional ? " (optional)" : "") << "\n";
            t << "  download: " << spec.mSource << "\n";
            t << "  licence:  " << spec.mLicence << "\n";
            for (const ExpectedFile& file : spec.mFiles)
            {
                t << "  " << file.mName << "  " << file.mSize << " bytes";
                if (!file.mSha256.empty())
                    t << "  SHA-256 " << file.mSha256;
                t << "\n";
            }
            t << "\n";
        }
        t << "The v3.0 download holds three identities (Claire, James, Mark); Studio's first version uses Mark, so "
             "only\n"
             "the Mark files are required. If a page asks you to sign in or accept terms, that is NVIDIA's page: Eden\n"
             "Studio never does it for you.\n";
        return t.str();
    }

    bool ensureModelsFolder(const fs::path& root, std::string& error)
    {
        std::error_code ec;
        fs::create_directories(root, ec);
        if (ec || !fs::is_directory(root, ec))
        {
            error = "cannot create " + root.generic_string() + (ec ? ": " + ec.message() : std::string());
            return false;
        }
        const fs::path readme = root / "README.txt";
        if (fs::exists(readme, ec))
            return true;
        std::ofstream out(readme, std::ios::binary);
        out << readmeText();
        out.flush();
        if (!out)
        {
            error = "cannot write " + readme.generic_string();
            return false;
        }
        return true;
    }
}
