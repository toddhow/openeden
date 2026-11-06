project (Components)

if(APPLE)
    set(BUNDLE_RESOURCES_DIR "${APP_BUNDLE_DIR}/Contents/Resources")
    set(OPENMW_RESOURCES_ROOT ${BUNDLE_RESOURCES_DIR})
endif(APPLE)

# Version file
set (VERSION_RESOURCE_FILE_IN "${OpenMW_SOURCE_DIR}/files/version.in")
set (VERSION_RESOURCE_FILE_RELATIVE "resources/version")
set (VERSION_CPP_FILE "components/version/version.cpp")

if (GIT_CHECKOUT)
    get_generator_is_multi_config(multi_config)
    add_custom_target (get-version
       BYPRODUCTS "${OpenMW_BINARY_DIR}/${VERSION_CPP_FILE}"
       DEPENDS "${OpenMW_SOURCE_DIR}/${VERSION_CPP_FILE}.in"
       COMMAND ${CMAKE_COMMAND}
            -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
            -DOpenMW_SOURCE_DIR=${OpenMW_SOURCE_DIR}
            -DOpenMW_BINARY_DIR=${OpenMW_BINARY_DIR}
            -DVERSION_RESOURCE_FILE_IN=${VERSION_RESOURCE_FILE_IN}
            -DVERSION_RESOURCE_FILE_RELATIVE=${VERSION_RESOURCE_FILE_RELATIVE}
            -DVERSION_CPP_FILE_IN=${OpenMW_SOURCE_DIR}/${VERSION_CPP_FILE}.in
            -DVERSION_CPP_FILE_OUT=${OpenMW_BINARY_DIR}/${VERSION_CPP_FILE}.out
            -DOPENMW_VERSION_MAJOR=${OPENMW_VERSION_MAJOR}
            -DOPENMW_VERSION_MINOR=${OPENMW_VERSION_MINOR}
            -DOPENMW_VERSION_RELEASE=${OPENMW_VERSION_RELEASE}
            -DOPENMW_LUA_API_REVISION=${OPENMW_LUA_API_REVISION}
            -DOPENMW_POSTPROCESSING_API_REVISION=${OPENMW_POSTPROCESSING_API_REVISION}
            -DOPENMW_VERSION=${OPENMW_VERSION}
            -DOPENMW_DOC_BASEURL=${OPENMW_DOC_BASEURL}
            -DMACROSFILE=${CMAKE_SOURCE_DIR}/cmake/OpenMWMacros.cmake
            "-DCMAKE_CONFIGURATION_TYPES=${CMAKE_CONFIGURATION_TYPES}"
            -Dgenerator_is_multi_config_var=${multi_config}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/GitVersion.cmake
        COMMAND ${CMAKE_COMMAND}
            -E copy_if_different ${OpenMW_BINARY_DIR}/${VERSION_CPP_FILE}.out ${OpenMW_BINARY_DIR}/${VERSION_CPP_FILE}
        VERBATIM)
else (GIT_CHECKOUT)
    configure_resource_file(${VERSION_RESOURCE_FILE_IN} ${OpenMW_BINARY_DIR} ${VERSION_RESOURCE_FILE_RELATIVE})
    configure_file("${OpenMW_SOURCE_DIR}/${VERSION_CPP_FILE}.in" "${OpenMW_BINARY_DIR}/${VERSION_CPP_FILE}")
endif (GIT_CHECKOUT)

list (APPEND COMPONENT_FILES "${OpenMW_BINARY_DIR}/${VERSION_CPP_FILE}")



# source files

add_component_dir (lua
    luastate scriptscontainer asyncpackage utilpackage serialization configuration l10n storage utf8
    shapes/box inputactions yamlloader scripttracker luastateptr
    )
copy_resource_file("lua/util.lua" "${OPENMW_RESOURCES_ROOT}" "resources/lua_libs/util.lua")

add_component_dir (l10n
    messagebundles manager
    )

#vsgopenmw-fixme(rewrite-me)
add_component_dir (settings
    categories
    categories/camera
    categories/cells
    categories/fog
    categories/game
    categories/general
    categories/groundcover
    categories/gui
    categories/hud
    categories/input
    categories/lua
    categories/map
    categories/models
    categories/navigator
    categories/physics
    categories/postprocessing
    categories/saves
    categories/shaders
    categories/shadows
    categories/sound
    categories/terrain
    categories/video
    categories/videomemory
    categories/water
    categories/windows
    categories/worldlod
    categories/bodies
    gyroscopeaxis
    hrtfmode
    navmeshrendermode
    parser
    sanitizer
    sanitizerimpl
    settings
    settingvalue
    shadermanager
    values
    windowmode
    )

add_component_dir (bgsm
    stream file
    )

# World LOD tiles (task 0055): tile keys, selection by pixel error, the tile payload and the derived pack of tiles.
# DRAFT layouts, kept behind this one folder. Depends on edenarchive.
add_component_dir (lod
    loderror tilekey tileselection terraintile tilepack tilebake texturedecode albedobake objectcluster clusterbuild nifflatten clusterpack clusterfade terrainfade tileswap clusterplan viewlimits fartier residency
    )

# Skinned body part NIF reading and the seam measurements of the merged legacy body (task 0069, stage a). Depends on nif.
add_component_dir (bodynif
    skinnedshape seamanalysis analysisreport skeletonread
    )

#<vsgopenmw-code-style>
# Views terrain.
# Depends on pipeline.
add_component_dir (terrain
    bounds coverage batchindex storage indexbuffer builder layermaps chunkfade
    lod view viewdata paging preload
    )
# Adapts nif to vsg::Node and kf to Anim::ControllerMap.
# Depends on nif, pipeline, animation.
add_component_dir (vsgadapters/nif
    nif anim channel particle kf internaltexture automaps
    )
# openeden-vsg: COLLADA models through assimp (task 0010).
add_component_dir (vsgadapters/dae
    dae daeanim
    )
add_component_dir (vsgadapters
    # openeden-vsg: tangents for normal maps (task 0012, V4h).
    tangents
    # Encapsulates vfs component.
    vfs
    )
add_component_dir (vsgadapters/sdl
    window surface
    )
add_component_dir (vsgadapters/mygui
    render texture manualtexture
    )
# Assembles vulkan pipelines.
add_component_dir (pipeline
    builder options override graphics gbufferblend particle particledata terrain descriptorvalue layout sets bindings computebindings viewbindings viewdata material descriptorimagecache descriptorbuffercache lightgrid fogofwar worldoverlay
    )
# Implements algorithms.
add_component_dir (render
    engine download screenshot framecapture headless limitframerate rendertexture cascade computeimage
    screen depth attachmentformat renderpass multisample debuggeometry simplewater intersect defaultsampler
    gputiming devicereport processmemory framecounters scenecensus hdrmath hdrtarget tonemap bloom gbuffermath velocity taamath taa aotemporalmath aotemporal vramaccount vramsource vramcontroller vramorder vramtexturemips vramscaler materialoverrides probes probebake skyvisbake ambientocclusion
    )
# Renders pipelines.
# Depends on pipeline, render.
add_component_dir (view
    scene descriptors collectlights defaultstate lightgrid shadow pointshadow env volumefog cloudvolume
    )
# Provides dynamic scene graph objects.
add_component_dir (animation
    # Channels map time to value.
    channel interpolate extrapolate path range reset
    # Controllers modify objects.
    controller tcontroller tmutable updatedata rotate roll skin morph transformcontroller texmat switch stateswitch color cullparticles
    # Nodes may be placed in the scene graph.
    transform billboard
    # Tools support nodes.
    softwareskin clone attachbone
    # Logic plays animations.
    animation visitor context update autoplay transformupdate tags bones controllermap contents meta
    )
# Creates animation scene graph.
# Depends on esm, animation.
add_component_dir (mwanimation
    context play groups clone decorate attach
    color position light lighteffect effect effects
    create object objectanim actor wielding face
    )
# Conveniently extends vsg.
# vsgutil must not depend on other components.
# vsgutil may be implicitely depended on.
#add_base_dir(vsgutil
add_component_dir (vsgutil
    # Object extensions.
    attachable name id operation image debugmessenger
    # Nodes.
    setviewportstate projection viewrelative computebin deferredbin nullbin suspendrenderpass distancelod recordwithviewid
    # Algorithms.
    convert bounds transform computetransform intersectnormal toggle group compilecontext compileop updatethreads arraytexture
    # Visitors.
    traverse traversestate nodepath cullnode searchbyname searchbytype cameradistanceintersector arraystate
    # I/O.
    cache virtualfile imageio imagepixels readimage texturecap textureresize texturemips decompress readnode readshader share shareimage deletionqueue
    # Slang twins of the GLSL shaders (task 0072).
    shaderpermutation spirvinterface passstages compilermessage slangruntime
    # Temporary workarounds.
    setbin sharedview
    )
# Task 0072, stage h: the Slang compiler in the game is loaded at run time (LoadLibrary or dlopen), never linked; the
# header is needed for the API's types only. Without the pinned archive (no slangc) the file compiles to a stub.
if (OPENEDEN_SLANGC)
    get_filename_component(openeden_slang_bin "${OPENEDEN_SLANGC}" DIRECTORY)
    get_filename_component(openeden_slang_root "${openeden_slang_bin}" DIRECTORY)
    set_source_files_properties(vsgutil/slangruntime.cpp PROPERTIES COMPILE_DEFINITIONS OPENEDEN_SLANG_RUNTIME=1)
endif()
# Supports MyGUI.
# Depends on vfs, vsgadapters/mygui.
add_component_dir (myguiplatform #(mygui
    # Backends.
    vfs platform loglistener
    # Layers.
    scalinglayer #additivelayer
    ## Widgets.
    #
    ## Loaders.
    #fontloader
    )
#</vsgopenmw-code-style>

add_component_dir (bsa
    bsafile compressedbsafile ba2gnrlfile ba2dx10file ba2file memorystream
    )

add_component_dir (physicshelpers
    collisionobject
    heightfield
    operators
    processtrianglecallback
    transformboundingbox
    )

# CreativeMotion's physics interface over a Jolt world the host owns (task 0035).
add_component_dir (cmphysics
    hostedjoltworld
    )

add_component_dir (vfs
    manager archive bsaarchive filesystemarchive pathutil registerarchives rangeread archivefactory file entryname
    edenarchive
    )

#<vsgopenmw-code-style="pending">
# Assembles I/O components for use.
# Depends on vsgadapters/vfs, vsgadapters/nif, nifbullet.
add_component_dir (resource
    # Interfaces.
    resourcesystem
    # ReaderWriters.
    shadersettings fallback decompress
    resourcemanager
    objectcache multiobjectcache niffilemanager
    physicsshapemanager physicsshape foreachphysicsobject cachestats assimpscene
    )
#</vsgopenmw-code-style>

# OpenEden task 0011: the navmesh debug draw, renderer-free (it draws into a DebugPrimitiveSink).
add_component_dir (sceneutil
    detourdebugdraw navmesh workqueue
    )

add_component_dir (nif
    base controller data effect extra niffile nifkey nifstream niftypes node parent particle physics property record recordptr texture
    )

# Nif::Scene: a NIF file as plain arrays, with the packed vertex formats of Skyrim SE decoded (task 0082).
# Depends on nif. Formats layer: no renderer, physics or game rule.
add_component_dir (nifscene
    build vertexdecode
    )

add_component_dir (nifjolt
    joltnifloader
    )

add_component_dir (toutf8
    tablesgen
    toutf8
    )

add_component_dir(esm attr common defs esmcommon records util luascripts format refid esmbridge esmterrain
    formid
    fourcc
    stringrefid
    generatedrefid
    indexrefid
    serializerefid
    esm3exteriorcellrefid
    quaternion
    vector3
    exteriorcelllocation
    )

# openeden-vsg: the .omwfx parser, which produces plain data, and its Vulkan passes (task 0012).
add_component_dir(fx technique lexer lexertypes parseconstants types vulkanpass uniformsettings widgets)


add_component_dir (esm3
    esmreader esmwriter loadacti loadalch loadappa loadarmo loadbody loadbook loadbsgn loadcell
    loadclas loadclot loadcont loadcrea loaddial loaddoor loadench loadfact loadglob loadgmst
    loadinfo loadingr loadland loadlevlist loadligh loadlock loadprob loadrepa loadltex loadmgef loadmisc
    loadnpc loadpgrd loadrace loadregn loadscpt loadskil loadsndg loadsoun loadspel loadsscr loadstat
    loadweap aipackage effectlist spelllist variant variantimp loadtes3 cellref filter
    savedgame journalentry queststate locals globalscript player objectstate cellid cellstate globalmap
    inventorystate containerstate npcstate creaturestate dialoguestate statstate npcstats creaturestats
    weatherstate quickkeys fogstate spellstate activespells creaturelevliststate doorstate projectilestate debugprofile
    aisequence magiceffects custommarkerstate stolenitems transport animationstate controlsstate mappings readerscache
    infoorder timestamp formatversion landrecorddata selectiongroup dialoguecondition
    refnum actoridconverter
    )

add_component_dir (esm3terrain
    storage
    )

add_component_dir (esm4
    actor
    cellgrid
    common
    dialogue
    effect
    grid
    grouptype
    inventory
    lighting
    loadachr
    loadacti
    loadalch
    loadaloc
    loadammo
    loadanio
    loadappa
    loadarma
    loadarmo
    loadaspc
    loadbook
    loadbptd
    loadcell
    loadclas
    loadclfm
    loadclot
    loadcont
    loadcrea
    loaddial
    loaddobj
    loaddoor
    loadeyes
    loadflor
    loadflst
    loadfurn
    loadglob
    loadgmst
    loadgras
    loadgrup
    loadhair
    loadhdpt
    loadidle
    loadidlm
    loadimod
    loadinfo
    loadingr
    loadkeym
    loadland
    loadlgtm
    loadligh
    loadltex
    loadlvlc
    loadlvli
    loadlvln
    loadmato
    loadmisc
    loadmset
    loadmstt
    loadmusc
    loadnavi
    loadnavm
    loadnote
    loadnpc
    loadotft
    loadpack
    loadpgrd
    loadpgre
    loadpwat
    loadqust
    loadrace
    loadrefr
    loadregn
    loadroad
    loadsbsp
    loadscol
    loadscpt
    loadscrl
    loadsgst
    loadslgm
    loadsndr
    loadsoun
    loadstat
    loadtact
    loadterm
    loadtes4
    loadtree
    loadtxst
    loadweap
    loadwrld
    magiceffectid
    reader
    readerutils
    reference
    script
    typetraits
    vertex
)

# Conveniently extends STL.
# misc must not depend on other components.
# misc may be implicitely depended on.
#add_base_dir(stl
# vsgopenmw-fixme(find-my-place) = constants resourcehelpers color
add_component_dir (misc
    barrier budgetmeasurement buildqueue color compression constants convert coordinateconverter display endianness float16 frameratelimiter
    guarded loadtrace math mathutil messageformatparser notnullptr objectpool progressreporter quietwindow resourcehelpers
    rng strongtypedef thread timeconvert timer tuplehelpers tuplemeta utf8stream vsgmath weakcache windows
    )

add_component_dir (misc/strings
    algorithm conversion format lower
    )

#vsgopenmw-delete-me
# Note, Debug::Log is deprecated because of syntax and compatibility issues with redirecting std::cout/std::cerr.
# Use std::cout/std::cerr or vsg::Logger instead.
add_component_dir (debug
    debuglog writeflags debugging
    # gldebug debugdraw
    )

IF(NOT WIN32 AND NOT APPLE)
    add_compile_definitions(GLOBAL_DATA_PATH="${GLOBAL_DATA_PATH}")
    add_compile_definitions(GLOBAL_CONFIG_PATH="${GLOBAL_CONFIG_PATH}")
ENDIF()
add_component_dir (files
    #vsgopenmw-move-me(xdgpath)
    linuxpath androidpath windowspath macospath fixedpath multidircollection collections configurationmanager
    constrainedfilestream memorystream hash configfileparser openfile constrainedfilestreambuf conversion
    istreamptr streamwithbuffer utils
    )

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set_source_files_properties(files/configfileparser.cpp PROPERTIES COMPILE_FLAGS -Wno-shadow)
endif()

add_component_dir (compiler
    context controlparser errorhandler exception exprparser extensions fileparser generator
    lineparser literals locals output parser scanner scriptparser skipparser streamerrorhandler
    stringparser tokenloc nullerrorhandler opcodes extensions0 declarationparser
    quickfileparser discardparser junkparser
    )

add_component_dir (interpreter
    context controlopcodes genericopcodes installopcodes interpreter localopcodes mathopcodes
    miscopcodes opcodes program runtime types defines
    )

# Task 0092: the ObScript runtime of the common compatibility pack (a contract implementation over compiler and
# interpreter; no ESM record, no engine type in its public header).
add_component_dir (script_obscript
    obscriptruntime obscriptlimits obscriptcompile
    )

add_component_dir (translation
    translation
    )

#vsgopenmw-delete-me
add_component_dir (loadinglistener
    loadinglistener asynclistener
    )

#vsgopenmw-move-me(components/mygui)
add_component_dir (widgets
    box fontwrapper imagebutton tags list numericeditbox sharedstatebutton windowcaption widgets
    )

#vsgopenmw-move-me(components/mygui)
add_component_dir (fontloader
    fontloader
    )

#<vgopenmw-code-style="pending">
#vsgopenmw-move-me(components/sdl)
# Wraps SDL.
add_component_dir (sdlutil
    init inputwrapper videowrapper events cursormanager sdlmappings
    )
#</vsgopenmw-code-style>

add_component_dir (version
    version
    )

add_component_dir (fallback
    fallback validate
    )

add_component_dir (lua_ui
    registerscriptsettings scriptsettings
    properties widget element util layers content alignment resources
    adapter text textedit window image container flex
    )
copy_resource_file("lua_ui/content.lua" "${OPENMW_RESOURCES_ROOT}" "resources/lua_libs/content.lua")


#vsgopenmw-delete-me
if(WIN32)
    add_component_dir (crashcatcher
        freezeresponse
        windowscrashcatcher
        windowscrashdumppathhelpers
        windowscrashmonitor
        windowscrashshm
    )
elseif(NOT ANDROID)
    add_component_dir (crashcatcher
        crashcatcher
    )
endif()

add_component_dir(detournavigator
    agentbounds
    areatype
    asyncnavmeshupdater
    bounds
    cellgridbounds
    changetype
    collisionshapetype
    commulativeaabb
    dbrefgeometryobject
    debug
    exceptions
    findrandompointaroundcircle
    findsmoothpath
    flags
    generatenavmeshtile
    gettilespositions
    guardednavmeshcacheitem
    heightfieldmeshbuilder
    heightfieldshape
    makenavmesh
    navigator
    navigatorimpl
    navigatorstub
    navigatorutils
    navmeshcacheitem
    navmeshdata
    navmeshdb
    navmeshdbutils
    navmeshmanager
    navmeshtilescache
    navmeshtileview
    objectid
    objecttransform
    offmeshconnection
    offmeshconnectionsmanager
    preparednavmeshdata
    preparednavmeshdatatuple
    raycast
    recast
    recastallocutils
    recastcontext
    recastglobalallocator
    recastmesh
    recastmeshbuilder
    recastmeshobject
    recastmeshprovider
    recastmeshtiles
    recastparams
    recasttempallocator
    ref
    serialization
    settings
    settingsutils
    sharednavmeshcacheitem
    stats
    status
    tilebounds
    tilecachedrecastmeshmanager
    tileposition
    tilespositionsrange
    updateguard
    version
    waitconditiontype
    )

#vsgopenmw-fixme(find-my-place)
add_component_dir(loadinglistener
    reporter
    )

add_component_dir(serialization
    binaryreader
    binarywriter
    vsgyaml
    sizeaccumulator
    )

add_component_dir(sqlite3
    db
    request
    statement
    transaction
)

# openeden-vsg: kept for openmw-physicsobjecttool and the resource physics preloading (OpenMW 0.51).
add_component_dir(esmloader
    lessbyid
    load
    esmdata
)

#vsgopenmw-delete-me
add_component_dir(navmeshtool
    protocol
    )

add_component_dir(platform
    #vsgopenmw-delete-me
    platform
    file
    )

if (WIN32)
#vsgopenmw-cleanup-platform-support
    add_component_dir(platform
        filewin32
        )
elseif (UNIX)
#vsgopenmw-cleanup-platform-support
    add_component_dir(platform
        fileposix
        )
else ()
    add_component_dir(platform
        filestdio
        )
endif()

add_component_dir(testing
    expecterror
    util
)

add_component_dir(extension
    manifest
    load_error
    manifest_io
    dynamic_library
    extension_loader
    dependency_resolver
    service_registry
    lifecycle_manager
    event_bus
    entity_manager
    settings_service
    vfs_service
    host_impl
    logging_service
    profiler_service
    scratch_memory
    debug_draw_service
    physics_service
)

add_component_dir(inertialmotion
    locomotionstate
)

if (USE_QT)
    add_component_qt_dir (contentselector
        model/modelitem model/esmfile
        model/contentmodel
        model/loadordererror
        view/combobox view/contentselector
        )

    add_component_qt_dir (config
        gamesettings
        launchersettings
        )

    add_component_qt_dir (l10n
        qttranslations
        )

    add_component_qt_dir (process
        processinvoker
        )

    add_component_qt_dir (misc
        helpviewer utf8qtextstream hash scalableicon
        )

    add_component_qt_dir (files
        qtconversion
        qtconfigpath
        )

    add_component_qt_dir (platform
        application
        )

    add_component_qt_dir (vfs
        qtconversion
        )

endif()

if (MSVC)
    # these TUs take a lot of memory for codegen
    # in CI, they often go over the 8GB memory limit if another cl.exe is running at the same time
    # this option (make sure you're not looking at the docs for the identically-named linker option) stops four codegen threads being spun off
    # it hurts build time ~33% to enable this unnecessarily, though, so target it to the hungriest boys
    set_source_files_properties(
        detournavigator/tilecachedrecastmeshmanager.cpp
        lua/utilpackage.cpp
        PROPERTIES
        COMPILE_OPTIONS /cgthreads1
    )
endif()

include_directories(${CMAKE_CURRENT_BINARY_DIR})

find_package(SQLite3 REQUIRED)

add_library(components STATIC ${COMPONENT_FILES})

target_include_directories(components PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(components SYSTEM BEFORE PUBLIC ${JOLT_INCLUDE_DIRS})
# The vendored CreativeMotion physics interface (headers only, standard library only) that components/cmphysics implements.
target_include_directories(components SYSTEM PUBLIC ${CMAKE_SOURCE_DIR}/components/creativemotion/physics/interface/include)
# openeden-vsg: pugixml, compiled into the assimp library, reads COLLADA animation clips (vsgadapters/dae/daeanim.cpp).
target_include_directories(components SYSTEM PRIVATE ${OPENEDEN_PUGIXML_INCLUDE_DIR})
# Slang's header (components/vsgutil/slangruntime.cpp, the one file that includes it) as a system header: third-party code
# whose interfaces have non-virtual destructors and stray semicolons that CI's -Werror flags would reject (the gate of
# ea7e3657c7). It is private, so nothing else sees it.
if (OPENEDEN_SLANGC)
    target_include_directories(components SYSTEM PRIVATE "${openeden_slang_root}/include")
endif()

if (ANDROID)
    set_property(TARGET components PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

if (APPLE)
    target_compile_definitions(components PUBLIC GL_SILENCE_DEPRECATION=1)
endif()

target_link_libraries(components
    ${COLLADA_DOM_LIBRARIES}

    Boost::program_options
    Boost::iostreams
    vsg::vsg
    vsgXchange::vsgXchange
    # The Eden Archive reference library (task 0052), Apache-2.0, used by vfs/edenarchive.cpp
    edenarchive
    # meshoptimizer (task 0059, MIT), used by lod/clusterbuild.cpp
    openeden_meshoptimizer
    # openeden-vsg: collision shapes of non-NIF models (resource/physicsshapemanager.cpp)
    assimp::assimp
    SDL2::SDL2
    ${OPENGL_gl_LIBRARY}
    ${MyGUI_LIBRARIES}
    ${LUA_LIBRARIES}
    LZ4::LZ4
    RecastNavigation::DebugUtils
    RecastNavigation::Detour
    RecastNavigation::Recast
    SQLite::SQLite3
    smhasher
    ${ICU_LIBRARIES}
    ZLIB::ZLIB
    ${JOLT_LIBRARIES}
    )

if(yaml-cpp_VERSION VERSION_GREATER_EQUAL 0.8.0)
    target_link_libraries(components yaml-cpp::yaml-cpp)
    target_link_libraries(components nlohmann_json::nlohmann_json)
else()
    target_link_libraries(components yaml-cpp)
    target_link_libraries(components nlohmann_json::nlohmann_json)
endif()

if (WIN32)
    target_link_libraries(components
    ${Boost_LOCALE_LIBRARY}
    ${Boost_ZLIB_LIBRARY})
endif()

if (USE_QT)
    add_library(components_qt STATIC ${COMPONENT_QT_FILES})
    target_link_libraries(components_qt components Qt::Widgets Qt::Core)

    if (BUILD_LAUNCHER OR BUILD_WIZARD)
        add_dependencies(components_qt qm-files)
    endif()

    if (BUILD_WITH_CODE_COVERAGE)
        target_compile_options(components_qt PRIVATE --coverage)
        target_link_libraries(components_qt gcov)
    endif()
endif()


if (WIN32)
    target_link_libraries(components shlwapi)
endif()

# Fix for not visible pthreads functions for linker with glibc 2.15
if (UNIX AND NOT APPLE)
    target_link_libraries(components ${CMAKE_THREAD_LIBS_INIT})
endif()

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(components PRIVATE --coverage)
    target_link_libraries(components gcov)
endif()


# Make the variable accessible for other subdirectories
set(COMPONENT_FILES ${COMPONENT_FILES} PARENT_SCOPE)

target_compile_definitions(components PUBLIC
    MYGUI_DONT_USE_OBSOLETE=ON
)

if(USE_QT)
    set_property(TARGET components_qt PROPERTY AUTOMOC ON)
    set_property(TARGET components_qt PROPERTY AUTOUIC ON)
    set_property(TARGET components_qt PROPERTY AUTOUIC_SEARCH_PATHS contentselector)
endif(USE_QT)

if (MSVC AND PRECOMPILE_HEADERS_WITH_MSVC)
    target_precompile_headers(components PUBLIC
        <sol/sol.hpp>

        <MyGUI_Widget.h>

        <algorithm>
        <filesystem>
        <fstream>
        <functional>
        <memory>
        <ostream>
        <string>
        <vector>
    )

    target_precompile_headers(components PRIVATE
        <boost/program_options/options_description.hpp>
    )
endif()
