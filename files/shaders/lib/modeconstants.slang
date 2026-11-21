// Task 0363 (stage c): the terrain's defines as specialization constants. Imported by the stages and includes that read
// them when MODE_CONSTANTS is defined (the permutation vsgUtil::constantDefinesFor names for `[Rendering] shader
// constants`); without it this module declares nothing. The ids are Pipeline::TerrainConstantId (the values come from
// Pipeline::terrainConstants) and, for the two debug views, Resource::settingsConstantTable (1070, 1071). A shader
// branches on each constant alone, never on an expression over them (files/shaders/CLAUDE.md, OpSpecConstantOp). Change
// this file, components/pipeline/terrain.hpp and the table together.
#ifndef LIB_MODECONSTANTS_SLANG
#define LIB_MODECONSTANTS_SLANG

#ifdef MODE_CONSTANTS
[vk::constant_id(2000)] const bool cGiSky = false;
[vk::constant_id(2001)] const bool cWetness = false;
[vk::constant_id(2002)] const bool cWetnessPuddles = false;
[vk::constant_id(2003)] const bool cWetnessDebug = false;
[vk::constant_id(2004)] const bool cCover = false;
[vk::constant_id(2005)] const bool cCoverDebug = false;
[vk::constant_id(2006)] const bool cAmbientCube = false;
[vk::constant_id(2007)] const bool cPointShadow = false;
[vk::constant_id(2008)] const bool cAoTerrain = false;
// The parallax quality's step counts (lib/material/parallax.slang); the default is the medium quality.
[vk::constant_id(2009)] const int cPomMinSteps = 10;
[vk::constant_id(2010)] const int cPomMaxSteps = 20;
[vk::constant_id(2011)] const int cPomRefineTaps = 4;
[vk::constant_id(1070)] const bool cDebugShadow = false;
[vk::constant_id(1071)] const bool cDebugChunks = false;
#endif

#endif
