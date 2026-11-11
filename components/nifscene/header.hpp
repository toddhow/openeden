#ifndef OPENEDEN_COMPONENTS_NIFSCENE_HEADER_HPP
#define OPENEDEN_COMPONENTS_NIFSCENE_HEADER_HPP

#include <cstddef>
#include <string_view>

namespace NifScene
{
    /// The longest first line of a NIF file that is looked at.
    constexpr std::size_t sMaxHeaderLine = 80;

    /// Whether the first bytes of a file are a NIF header line of version 20.2.0.7 (Skyrim SE: "Gamebryo File Format,
    /// Version 20.2.0.7"). Looks at nothing past the first line (at most sMaxHeaderLine characters), accepts only
    /// printable text, and never throws. Task 0110: the renderer's NIF reader uses it to hand such a file to the scene
    /// path; Eden Studio's CSMMesh::chooseNifReader makes the same decision.
    bool isSkyrimSeHeader(std::string_view fileStart);
}

#endif
