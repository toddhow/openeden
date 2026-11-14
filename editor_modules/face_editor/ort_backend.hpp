#pragma once

// The ONNX Runtime backend of the Audio2Face generator (task 0147 slice 2): runs a network.onnx from the models folder
// for the generator in a2f_generate. Compiled only when the ONNX Runtime dependency is configured
// (OPENEDEN_FACE_GENERATOR, extern/CMakeLists.txt). Used only by the face editor module, only from a background task.
//
// The runtime is delay loaded: Eden Studio and this module start without onnxruntime.dll, and runtimeAvailable() says
// why generation cannot run. DirectML is optional (task 0147 slice 3): it is used only when the caller passes the full
// path of a DirectML.dll the author installed (directml_install.hpp), which is loaded BY THAT PATH before the session
// is made; the CPU provider is the fallback, with the reason returned to the caller. The input and output layouts below
// are the ones the feasibility gate inferred from the networks (task 0071 "Stage a results"; RECALLED-VERIFY).

#include "a2f_generate.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace face_editor
{
    enum class OrtProvider
    {
        DirectMl,
        Cpu
    };

    /// True when onnxruntime.dll (the pinned one beside the editor) can be loaded. \a why says what is missing.
    bool ortRuntimeAvailable(std::string& why);

    /// Loads \a dll (the full path of an installed DirectML.dll) into the process, so that onnxruntime's delay load of
    /// "DirectML.dll" binds to it and to no other copy (Windows ships an older one in System32). False with \a why set
    /// when it does not load or another DirectML.dll is the one in the process.
    bool ortLoadDirectMl(const std::filesystem::path& dll, std::string& why);

    /// The pinned runtime's version string ("1.24.4"), empty when it cannot be loaded.
    std::string ortRuntimeVersion();

    class OrtBackend final : public A2FBackend
    {
    public:
        /// Loads \a network. With a non-empty \a directMlDll the DirectML provider is tried (after ortLoadDirectMl);
        /// a failure of it falls back to the CPU provider and sets \a fallbackReason; a CPU failure is an error. An
        /// empty \a directMlDll is the CPU provider with no reason given. Null with \a error set when the model cannot
        /// be loaded.
        static std::unique_ptr<OrtBackend> create(const std::filesystem::path& network, A2FVariant variant,
            const std::filesystem::path& directMlDll, std::string& fallbackReason, std::string& error);

        ~OrtBackend() override;
        OrtProvider provider() const;
        static const char* providerName(OrtProvider provider);

        bool runV3(const float* window, const float* identity, const float* emotion, const float* latentsIn,
            const float* noise, float* prediction, float* latentsOut, std::string& error) override;
        bool runV23(
            const float* windows, std::size_t count, const float* emotion, float* results, std::string& error) override;

    private:
        class Impl;
        explicit OrtBackend(std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> mImpl;
    };
}
