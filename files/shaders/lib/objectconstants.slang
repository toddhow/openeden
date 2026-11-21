// Task 0363 (stage d, plan M): the object shader's own specialization constants, beside lib/modeconstants.slang's (the
// ones it shares with the terrain: the sky field, wetness, cover, the ambient cube, the point light shadow, the debug
// views and the parallax step counts). Imported by default/default.frag.slang and default/default.vert.slang when
// MODE_CONSTANTS is defined (the permutation vsgUtil::constantDefinesFor names for `[Rendering] shader constants`);
// without it this module declares nothing. The ids are Pipeline::ObjectConstantId (the values come from
// Pipeline::objectFragmentConstants and objectVertexConstants). A shader branches on each constant alone, never on an
// expression over them (files/shaders/CLAUDE.md, OpSpecConstantOp): the derived ones (per pixel lighting, the lit
// surfaces of the material model, wetness and cover on them) are worked out by the CPU. Change this file,
// components/pipeline/objectconstants.hpp and the table there together.
#ifndef LIB_OBJECTCONSTANTS_SLANG
#define LIB_OBJECTCONSTANTS_SLANG

#ifdef MODE_CONSTANTS
[vk::constant_id(2020)] const bool cGi = false;
[vk::constant_id(2021)] const bool cPerPixel = false;
[vk::constant_id(2022)] const bool cPbrLit = false;
[vk::constant_id(2023)] const bool cPbrPhysical = false;
[vk::constant_id(2024)] const bool cWetLit = false;
[vk::constant_id(2025)] const bool cCoverLit = false;
[vk::constant_id(2026)] const bool cLodFade = false;
[vk::constant_id(2027)] const bool cAdjustCoverage = false;
[vk::constant_id(2028)] const bool cLodSway = false;
#endif

#endif
