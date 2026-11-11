#ifndef OPENEDEN_COMPONENTS_UI_RML_OVERLAY_H
#define OPENEDEN_COMPONENTS_UI_RML_OVERLAY_H

#include <cstdint>
#include <memory>
#include <string>

#include <vsg/core/ref_ptr.h>

#include <components/toutf8/toutf8.hpp>

namespace vsg
{
    class Node;
    class Options;
}

namespace vsgUtil
{
    class CompileContext;
}

namespace VFS
{
    class Manager;
}

// OpenEden's new UI system, first slice (decision 0015, task 0104): RmlUi drawn over the game by the GUI render graph.
// This header names no RmlUi type, so the game's code does not need RmlUi's headers.
namespace UiRml
{
    struct OverlayConfig
    {
        /// The Morrowind bitmap font to use, as the file name without folder and extension ("Magic_Cards"); the game's
        /// `Fonts_Font_0`. If it cannot be loaded the overlay falls back to the DejaVu TrueType font of the engine.
        std::string mBitmapFont;
        /// The data files' encoding (the font code page follows it).
        ToUTF8::FromType mEncoding = ToUTF8::WINDOWS_1252;
    };

    /// One RmlUi context drawn over the game with the debug panel of this slice in it.
    ///
    /// RmlUi keeps global state, so there is at most one Overlay in a process (the constructor throws if another
    /// exists). Owner: the render manager, which creates it only with `[GUI] rmlui` on; with the setting off nothing of
    /// this exists. The VFS manager and the compile context must outlive it. No input reaches it yet.
    class Overlay
    {
    public:
        Overlay(vsg::ref_ptr<vsgUtil::CompileContext> compile, vsg::ref_ptr<const vsg::Options> shaderOptions,
            const VFS::Manager& vfs, const OverlayConfig& config, std::uint32_t width, std::uint32_t height);
        ~Overlay();
        Overlay(const Overlay&) = delete;
        Overlay& operator=(const Overlay&) = delete;

        /// The node to put last in the GUI render graph.
        vsg::ref_ptr<vsg::Node> node();

        /// The size of the window the overlay draws into (main thread, between frames).
        void setExtent(std::uint32_t width, std::uint32_t height);

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}

#endif
