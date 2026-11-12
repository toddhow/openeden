#include "ort_backend.hpp"

#include <onnxruntime_cxx_api.h>

#include <dml_provider_factory.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <array>
#include <cstring>

namespace face_editor
{
    namespace fs = std::filesystem;

    bool ortRuntimeAvailable(std::string& why)
    {
#ifdef _WIN32
        // The application directory only, so that an older onnxruntime.dll in the system folder (Windows ships one for
        // WinML) can never be bound by the delay load helper in place of the pinned one.
        // (The same flags also decide where its own dependencies are searched, so the system folder is added.)
        HMODULE h = LoadLibraryExW(
            L"onnxruntime.dll", nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (h == nullptr)
        {
            why = "The ONNX runtime (onnxruntime.dll) was not found beside the editor (error "
                + std::to_string(GetLastError()) + ")";
            return false;
        }
        return true;
#else
        why = "ONNX Runtime is not part of this platform's build";
        return false;
#endif
    }

    std::string ortRuntimeVersion()
    {
        std::string why;
        if (!ortRuntimeAvailable(why))
            return {};
        try
        {
            const OrtApiBase* base = OrtGetApiBase();
            return base != nullptr ? base->GetVersionString() : std::string();
        }
        catch (...)
        {
            return {};
        }
    }

    class OrtBackend::Impl
    {
    public:
        Impl()
            : mEnv(ORT_LOGGING_LEVEL_WARNING, "eden-face")
            , mMemory(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault))
        {
        }

        bool open(const fs::path& network, bool directMl, std::string& error)
        {
            try
            {
                Ort::SessionOptions options;
                options.SetLogSeverityLevel(3);
                if (directMl)
                {
                    // The settings the feasibility gate ran DirectML with.
                    options.DisableMemPattern();
                    options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
                    const OrtDmlApi* dml = nullptr;
                    Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi(
                        "DML", ORT_API_VERSION, reinterpret_cast<const void**>(&dml)));
                    Ort::ThrowOnError(dml->SessionOptionsAppendExecutionProvider_DML(options, 0));
                }
                mSession = std::make_unique<Ort::Session>(mEnv, network.c_str(), options);
                mProvider = directMl ? OrtProvider::DirectMl : OrtProvider::Cpu;
                Ort::AllocatorWithDefaultOptions allocator;
                mOutput0 = mSession->GetOutputNameAllocated(0, allocator).get();
                if (mSession->GetOutputCount() > 1)
                    mOutput1 = mSession->GetOutputNameAllocated(1, allocator).get();
                return true;
            }
            catch (const std::exception& e)
            {
                error = e.what();
                mSession.reset();
                return false;
            }
        }

        Ort::Env mEnv;
        Ort::MemoryInfo mMemory;
        std::unique_ptr<Ort::Session> mSession;
        OrtProvider mProvider = OrtProvider::Cpu;
        std::string mOutput0, mOutput1;
    };

    OrtBackend::OrtBackend(std::unique_ptr<Impl> impl)
        : mImpl(std::move(impl))
    {
    }

    OrtBackend::~OrtBackend() = default;

    OrtProvider OrtBackend::provider() const
    {
        return mImpl->mProvider;
    }

    const char* OrtBackend::providerName(OrtProvider provider)
    {
        return provider == OrtProvider::DirectMl ? "DirectML" : "CPU";
    }

    std::unique_ptr<OrtBackend> OrtBackend::create(const fs::path& network, A2FVariant, bool allowDirectMl,
        std::string& fallbackReason, std::string& error)
    {
        fallbackReason.clear();
        if (!ortRuntimeAvailable(error))
            return nullptr;
        try
        {
            if (allowDirectMl)
            {
                auto dml = std::make_unique<Impl>();
                std::string why;
                if (dml->open(network, true, why))
                    return std::unique_ptr<OrtBackend>(new OrtBackend(std::move(dml)));
                fallbackReason = "DirectML could not be used (" + why + ")";
            }
            auto cpu = std::make_unique<Impl>();
            if (!cpu->open(network, false, error))
                return nullptr;
            return std::unique_ptr<OrtBackend>(new OrtBackend(std::move(cpu)));
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return nullptr;
        }
    }

    namespace
    {
        template <std::size_t N>
        Ort::Value tensor(const Ort::MemoryInfo& memory, const float* data, std::size_t count, const std::array<std::int64_t, N>& shape)
        {
            return Ort::Value::CreateTensor<float>(
                memory, const_cast<float*>(data), count, shape.data(), shape.size());
        }
    }

    bool OrtBackend::runV3(const float* window, const float* identity, const float* emotion, const float* latentsIn,
        const float* noise, float* prediction, float* latentsOut, std::string& error)
    {
        try
        {
            const Ort::MemoryInfo& mem = mImpl->mMemory;
            std::array<Ort::Value, 5> inputs{ tensor(mem, window, A2FDims::sV3Window, std::array<std::int64_t, 2>{ 1, 16000 }),
                tensor(mem, identity, 3, std::array<std::int64_t, 2>{ 1, 3 }),
                tensor(mem, emotion, A2FDims::sV3EmotionFloats, std::array<std::int64_t, 3>{ 1, 30, 10 }),
                tensor(mem, latentsIn, A2FDims::sV3LatentFloats, std::array<std::int64_t, 4>{ 2, 2, 1, 256 }),
                tensor(mem, noise, A2FDims::sV3NoiseFloats, std::array<std::int64_t, 4>{ 1, 3, 60, 88831 }) };
            const char* inNames[5] = { "window", "identity", "emotion", "input_latents", "noise" };
            const char* outNames[2] = { mImpl->mOutput0.c_str(), mImpl->mOutput1.c_str() };
            auto outputs = mImpl->mSession->Run(Ort::RunOptions{ nullptr }, inNames, inputs.data(), inputs.size(), outNames, 2);
            const std::size_t predCount = outputs[0].GetTensorTypeAndShapeInfo().GetElementCount();
            const std::size_t latCount = outputs[1].GetTensorTypeAndShapeInfo().GetElementCount();
            if (predCount != A2FDims::sV3Frames * A2FDims::sV3Outputs || latCount != A2FDims::sV3LatentFloats)
            {
                error = "the v3.0 network returned " + std::to_string(predCount) + " and " + std::to_string(latCount)
                    + " values, not the expected layout";
                return false;
            }
            std::memcpy(prediction, outputs[0].GetTensorData<float>(), predCount * sizeof(float));
            std::memcpy(latentsOut, outputs[1].GetTensorData<float>(), latCount * sizeof(float));
            return true;
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return false;
        }
    }

    bool OrtBackend::runV23(const float* windows, std::size_t count, const float* emotion, float* results,
        std::string& error)
    {
        try
        {
            const Ort::MemoryInfo& mem = mImpl->mMemory;
            std::vector<float> emotions(count * A2FDims::sV23EmotionFloats);
            for (std::size_t i = 0; i < count; ++i)
                std::memcpy(emotions.data() + i * A2FDims::sV23EmotionFloats, emotion,
                    A2FDims::sV23EmotionFloats * sizeof(float));
            const auto n = static_cast<std::int64_t>(count);
            std::array<Ort::Value, 2> inputs{ tensor(mem, windows, count * A2FDims::sV23Window,
                                                  std::array<std::int64_t, 3>{ n, 1, 8320 }),
                tensor(mem, emotions.data(), emotions.size(), std::array<std::int64_t, 3>{ n, 1, 26 }) };
            const char* inNames[2] = { "input", "emotion" };
            const char* outNames[1] = { mImpl->mOutput0.c_str() };
            auto outputs = mImpl->mSession->Run(Ort::RunOptions{ nullptr }, inNames, inputs.data(), inputs.size(), outNames, 1);
            const std::size_t got = outputs[0].GetTensorTypeAndShapeInfo().GetElementCount();
            if (got != count * A2FDims::sV23Outputs)
            {
                error = "the v2.3 network returned " + std::to_string(got) + " values for " + std::to_string(count)
                    + " windows, not " + std::to_string(count * A2FDims::sV23Outputs);
                return false;
            }
            std::memcpy(results, outputs[0].GetTensorData<float>(), got * sizeof(float));
            return true;
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return false;
        }
    }
}
