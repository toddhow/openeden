# csmodel: the editor's data, documents, tools and preferences (task 0014, stage C1). It must build and be tested
# without any view code, as must eden-studio-tests. The three widgets are here because model/prefs/coloursetting.cpp
# and model/world/idcompletionmanager.cpp use them; they are plain Qt with no 3D.
set (OPENCS_SRC
    )

opencs_units (model/doc
    document operation saving documentmanager loader runner operationholder undorouting
    )

opencs_units (model/doc
    savingstate savingstages messages
    )

opencs_hdrs (model/doc
    state
    )


opencs_units (model/world
    idtable idtableproxymodel regionmap data commanddispatcher idtablebase resourcetable nestedtableproxymodel idtree infotableproxymodel landtexturetableproxymodel
    actoradapter idcollection
    )


opencs_units (model/world
    universalid commands columnbase columnimp scriptcontext refidcollection
    refiddata refidadapterimp ref collectionbase refcollection columns infocollection tablemimedata cellcoordinates cellselection resources resourcesmanager scope
    pathgrid land nestedcollection nestedcoladapterimp nestedinfocollection
    idcompletionmanager metadata defaultgmsts infoselectwrapper commandmacro
    )

opencs_hdrs (model/world
    columnimp disabletag idcollection collection info subcellcollection cell nestedtablewrapper record
    )


opencs_units (model/tools
    tools reportmodel mergeoperation
    )

opencs_units (model/tools
    mandatoryid skillcheck classcheck factioncheck racecheck soundcheck regioncheck
    birthsigncheck spellcheck referencecheck referenceablecheck scriptcheck bodypartcheck
    startscriptcheck search searchoperation searchstage pathgridcheck soundgencheck magiceffectcheck
    mergestages gmstcheck topicinfocheck journalcheck enchantmentcheck effectlistcheck
    )

opencs_hdrs (model/tools
    mergestate
    )


opencs_units (model/prefs
    state configmigration layoutstore setting intsetting doublesetting boolsetting enumsetting coloursetting shortcut
    shortcuteventhandler shortcutmanager shortcutsetting modifiersetting stringsetting subcategory commandline
    quietwindow
    )

opencs_units (model/prefs
    category
    )

opencs_units (model/extension
    editorextensionhost
    )

opencs_units (model/module
    moduleregistry modulehost overlaybuffer eventbus overlaypick dragsession labelstroke modulesettings taskrunner managersettingsstore
    )

opencs_units (model/mesh
    meshviewstate meshtree framemath meshgeometry texturereport animationkeys meshoverlay scenemodel
    )

opencs_units (model/lod
    tilemodel
    )

opencs_units (model/physics
    joltruntime studioworld debuglinedrawer pushscene
    )

opencs_units (model/filter
    unarynode narynode parser andnode ornode notnode textnode valuenode
    )

opencs_hdrs (model/filter
    leafnode booleannode
    )

opencs_units (view/widget
    coloreditor colorpickerpopup completerpopup
    )

set (CSMODEL_SRC ${OPENCS_SRC})
set (CSMODEL_HDR ${OPENCS_HDR})

add_library(csmodel STATIC
    ${CSMODEL_SRC}
)

target_link_libraries(csmodel
    Boost::program_options
    opas::motor opas::rig opas::math
    components_qt
    Qt::Widgets Qt::Core Qt::Network Qt::Svg
)

set_property(TARGET csmodel PROPERTY AUTOMOC ON)
set_property(TARGET csmodel PROPERTY AUTOUIC_SEARCH_PATHS ui)
set_property(TARGET csmodel PROPERTY AUTOUIC ON)

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(csmodel PRIVATE --coverage)
    target_link_libraries(csmodel gcov)
endif()

if (MSVC)
    # Debug version needs increased number of sections beyond 2^16
    if (CMAKE_CL_64)
        target_compile_options(csmodel PRIVATE $<$<CONFIG:Debug>:/bigobj>)
    endif (CMAKE_CL_64)
endif (MSVC)

set (OPENCS_SRC
    )
set (OPENCS_HDR
    )

opencs_units (. editor capture lodcheck)

opencs_units (view/lod
    tileviewer
    )

opencs_units (view/module
    moduleui
    )

opencs_units (view/doc
    viewmanager view operations operation subview startup filedialog newgame
    filewidget adjusterwidget loader globaldebugprofilemenu runlogsubview studioshell extensionmanager assetbrowser contextinspector outputlogpanel logtap
    quietwindows
    )


opencs_units (view/doc
    subviewfactory
    )

opencs_hdrs (view/doc
    subviewfactoryimp sizehint
    )


opencs_units (view/world
    table tablesubview scriptsubview util regionmapsubview tablebottombox creator genericcreator globalcreator
    cellcreator pathgridcreator referenceablecreator startscriptcreator referencecreator
    infocreator scriptedit dialoguesubview regionmap dragrecordtable nestedtable
    dialoguespinbox recordbuttonbar tableeditidaction scripterrortable extendedcommandconfigurator
    bodypartcreator landcreator tableheadermouseeventhandler
    )

opencs_units (view/world
    subviews enumdelegate vartypedelegate recordstatusdelegate idtypedelegate datadisplaydelegate
    scripthighlighter idvalidator dialoguecreator idcompletiondelegate
    colordelegate dragdroputils
    )

opencs_units (view/widget
    scenetoolbar scenetool scenetoolmode pushbutton scenetooltoggle scenetoolrun modebutton
    scenetooltoggle2 scenetooltexturebrush scenetoolshapebrush droplineedit
    )

opencs_units (view/render
    rendercontext scenehost physicshub scenewidget worldspacewidget pagedworldspacewidget unpagedworldspacewidget previewwidget
    editmode lighting object cell cellarrow colouredgeometry actor cellborder cellmarker cellwater terrainstorage
    cameracontroller orbitcameramode meshscene meshviewwidget
    instancemode instanceselectionmode instancemovemode selectionmode objectmarker overlaylines
    terrainshapemode terraintexturemode terrainselection brushdraw commands
    pathgrid pathgridmode pathgridselectionmode
    )

opencs_units (view/world
    scenesubview previewsubview meshsubview
    )

opencs_hdrs (view/render
    mask tagbase instancedragmodes camerapose picking instancemath selectionmarkergeometry terraintoolgeometry pathgridgeometry
    )


opencs_units (view/tools
    reportsubview reporttable searchsubview searchbox merge
    )

opencs_units (view/tools
    subviews
    )

opencs_units (view/prefs
    dialogue pagebase page keybindingpage contextmenulist modulesettingspage
    )

opencs_units (view/filter
    filterbox recordfilterbox editwidget
    )

set (OPENCS_US
    )

set (OPENCS_RES ${CMAKE_SOURCE_DIR}/files/opencs/resources.qrc
                ${CMAKE_SOURCE_DIR}/files/launcher/launcher.qrc
    )

source_group (eden-studio FILES main.cpp ${OPENCS_SRC} ${OPENCS_HDR})

if(WIN32)
    set(OPENCS_RES ${OPENCS_RES} ${CMAKE_SOURCE_DIR}/files/windows/QWindowsVistaDark/dark.qrc)
    set(QT_USE_QTMAIN TRUE)
    set(OPENCS_RC_FILE ${CMAKE_SOURCE_DIR}/files/windows/opencs.rc)
else(WIN32)
    set(OPENCS_RC_FILE "")
endif(WIN32)

qt_add_resources(OPENCS_RES_SRC ${OPENCS_RES})

if(APPLE)
    set (OPENCS_MAC_ICON "${CMAKE_SOURCE_DIR}/files/mac/eden-studio.icns")
    set (OPENCS_CFG "${OpenMW_BINARY_DIR}/defaults-cs.cfg")
    set (OPENCS_DEFAULT_FILTERS_FILE "${OpenMW_BINARY_DIR}/resources/defaultfilters")
    set (OPENCS_OPENMW_CFG "${OpenMW_BINARY_DIR}/openmw.cfg")
else()
    set (OPENCS_MAC_ICON "")
    set (OPENCS_CFG "")
    set (OPENCS_DEFAULT_FILTERS_FILE "")
    set (OPENCS_OPENMW_CFG "")
endif(APPLE)

add_library(eden-studio-lib STATIC
    ${OPENCS_SRC}
    ${OPENCS_UI_HDR}
    ${OPENCS_MOC_SRC}
    ${OPENCS_RES_SRC}
)

if(BUILD_OPENCS)
    openmw_add_executable(eden-studio
        MACOSX_BUNDLE
        ${OPENCS_MAC_ICON}
        ${OPENCS_CFG}
        ${OPENCS_DEFAULT_FILTERS_FILE}
        ${OPENCS_OPENMW_CFG}
        ${OPENCS_RC_FILE}
        main.cpp
    )

    target_link_libraries(eden-studio eden-studio-lib)

    set_property(TARGET eden-studio PROPERTY AUTOMOC ON)
    set_property(TARGET eden-studio PROPERTY AUTOUIC_SEARCH_PATHS ui)
    set_property(TARGET eden-studio PROPERTY AUTOUIC ON)

    if (BUILD_WITH_CODE_COVERAGE)
        target_compile_options(eden-studio PRIVATE --coverage)
        target_link_libraries(eden-studio gcov)
    endif()
endif()

if(APPLE AND BUILD_OPENCS)
    set(OPENCS_BUNDLE_NAME "Eden Studio")
    set(OPENCS_BUNDLE_RESOURCES_DIR "${OpenMW_BINARY_DIR}/${OPENCS_BUNDLE_NAME}.app/Contents/Resources")

    set(OPENMW_RESOURCES_ROOT ${OPENCS_BUNDLE_RESOURCES_DIR})

    add_subdirectory(../../files/ ${CMAKE_CURRENT_BINARY_DIR}/files)

    set_target_properties(eden-studio PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${OpenMW_BINARY_DIR}"
        OUTPUT_NAME ${OPENCS_BUNDLE_NAME}
        MACOSX_BUNDLE_ICON_FILE "eden-studio.icns"
        MACOSX_BUNDLE_BUNDLE_NAME "Eden Studio"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.openmw.opencs"
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${OPENMW_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${OPENMW_VERSION}
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/files/mac/eden-studio-Info.plist.in"
        )

    set_source_files_properties(${OPENCS_MAC_ICON} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    set_source_files_properties(${OPENCS_CFG} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    set_source_files_properties(${OPENCS_DEFAULT_FILTERS_FILE} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources/resources)
    set_source_files_properties(${OPENCS_OPENMW_CFG} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)

    add_custom_command(TARGET eden-studio
        POST_BUILD
        COMMAND cp "${OpenMW_BINARY_DIR}/resources/version" "${OPENCS_BUNDLE_RESOURCES_DIR}/resources")
endif()

target_link_libraries(eden-studio-lib
    csmodel
    vsgQt
)

target_link_libraries(eden-studio-lib Qt::Widgets Qt::Core Qt::Network Qt::Svg)

if (WIN32 AND BUILD_OPENCS)
    target_sources(eden-studio PRIVATE ${CMAKE_SOURCE_DIR}/files/windows/other-apps.manifest)
endif()

if (WIN32 AND BUILD_OPENCS)
    INSTALL(TARGETS eden-studio RUNTIME DESTINATION ".")

    get_generator_is_multi_config(multi_config)
    if (multi_config)
        SET(INSTALL_SOURCE "${OpenMW_BINARY_DIR}/$<CONFIG>")
    else ()
        SET(INSTALL_SOURCE "${OpenMW_BINARY_DIR}")
    endif ()

    INSTALL(FILES "${INSTALL_SOURCE}/defaults-cs.cfg" DESTINATION ".")
    # The shared renderer reads the game's settings too (Settings::Manager::load).
    INSTALL(FILES "${INSTALL_SOURCE}/defaults.cfg" DESTINATION ".")
endif()

if (MSVC)
    # Debug version needs increased number of sections beyond 2^16
    if (CMAKE_CL_64)
        set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /bigobj")
    endif (CMAKE_CL_64)
endif (MSVC)

if(APPLE AND BUILD_OPENCS)
    INSTALL(TARGETS eden-studio BUNDLE DESTINATION "." COMPONENT Bundle)
endif()

if(USE_QT)
    set_property(TARGET eden-studio-lib PROPERTY AUTOMOC ON)
    set_property(TARGET eden-studio-lib PROPERTY AUTOUIC_SEARCH_PATHS ui)
    set_property(TARGET eden-studio-lib PROPERTY AUTOUIC ON)
endif(USE_QT)

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(eden-studio-lib PRIVATE --coverage)
    target_link_libraries(eden-studio-lib gcov)
endif()

if (MSVC AND PRECOMPILE_HEADERS_WITH_MSVC)
    target_precompile_headers(eden-studio-lib PRIVATE
        <boost/program_options/options_description.hpp>

        <algorithm>
        <fstream>
        <functional>
        <ostream>
        <string>
        <string_view>
        <vector>
    )
endif()

# The sample editor module (task 0031, sdk/examples/sample_editor_module): a real module library in editor-modules/,
# where Eden Studio looks for the core systems' ones. The tests load it, and so can OPENEDEN_CS_MODULES.
add_library(sample_editor_module SHARED ${CMAKE_SOURCE_DIR}/sdk/examples/sample_editor_module/sample_editor_module.cpp)
target_compile_features(sample_editor_module PRIVATE cxx_std_20)
target_link_libraries(sample_editor_module PRIVATE nlohmann_json::nlohmann_json)
set_target_properties(sample_editor_module PROPERTIES
    OUTPUT_NAME "sample_editor_module"
    DEBUG_POSTFIX ""
    RELEASE_POSTFIX ""
    RELWITHDEBINFO_POSTFIX ""
    MINSIZEREL_POSTFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/editor-modules"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/editor-modules")
foreach(config ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${config} config_upper)
    set_target_properties(sample_editor_module PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/editor-modules"
        LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/editor-modules")
endforeach()

# CreativeMotion's editor module (task 0032, editor_modules/creativemotion_editor): a core system's tools, in a library of
# its own that only Eden Studio loads. The sources without the entry point are a static library the tests link as well.
set(CM_EDITOR_DIR ${CMAKE_SOURCE_DIR}/editor_modules/creativemotion_editor)
add_library(creativemotion_editor_objects OBJECT
    ${CM_EDITOR_DIR}/rig_document.cpp ${CM_EDITOR_DIR}/profile_document.cpp
    ${CM_EDITOR_DIR}/creativemotion_editor_module.cpp)
target_compile_features(creativemotion_editor_objects PRIVATE cxx_std_20)
target_link_libraries(creativemotion_editor_objects PUBLIC opas::rig)
target_include_directories(creativemotion_editor_objects PRIVATE ${CMAKE_SOURCE_DIR}/sdk/include)
set_property(TARGET creativemotion_editor_objects PROPERTY POSITION_INDEPENDENT_CODE ON)

add_library(creativemotion_editor SHARED ${CM_EDITOR_DIR}/creativemotion_editor_export.cpp
    $<TARGET_OBJECTS:creativemotion_editor_objects>)
target_compile_features(creativemotion_editor PRIVATE cxx_std_20)
target_link_libraries(creativemotion_editor PRIVATE opas::rig)
set_target_properties(creativemotion_editor PROPERTIES
    OUTPUT_NAME "creativemotion_editor"
    DEBUG_POSTFIX ""
    RELEASE_POSTFIX ""
    RELWITHDEBINFO_POSTFIX ""
    MINSIZEREL_POSTFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/editor-modules"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/editor-modules")
foreach(config ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${config} config_upper)
    set_target_properties(creativemotion_editor PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/editor-modules"
        LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/editor-modules")
endforeach()

# AdvancedBodies' editor module (task 0069 stage f, editor_modules/advancedbodies_editor): the body package panel, a core
# system's tools in a library of its own that only Eden Studio loads. As for CreativeMotion's, the sources without the
# entry point are an object library the tests link as well. It reads with components/body (eden_body) and nothing of
# the engine.
set(AB_EDITOR_DIR ${CMAKE_SOURCE_DIR}/editor_modules/advancedbodies_editor)
add_library(advancedbodies_editor_objects OBJECT
    ${AB_EDITOR_DIR}/bodyview.cpp ${AB_EDITOR_DIR}/body_document.cpp
    ${AB_EDITOR_DIR}/advancedbodies_editor_module.cpp)
target_compile_features(advancedbodies_editor_objects PRIVATE cxx_std_20)
target_link_libraries(advancedbodies_editor_objects PUBLIC eden_body PRIVATE nlohmann_json::nlohmann_json)
target_include_directories(advancedbodies_editor_objects PRIVATE ${CMAKE_SOURCE_DIR}/sdk/include)
set_property(TARGET advancedbodies_editor_objects PROPERTY POSITION_INDEPENDENT_CODE ON)

add_library(advancedbodies_editor SHARED ${AB_EDITOR_DIR}/advancedbodies_editor_export.cpp
    $<TARGET_OBJECTS:advancedbodies_editor_objects>)
target_compile_features(advancedbodies_editor PRIVATE cxx_std_20)
target_include_directories(advancedbodies_editor PRIVATE ${CMAKE_SOURCE_DIR}/sdk/include)
target_link_libraries(advancedbodies_editor PRIVATE eden_body nlohmann_json::nlohmann_json)
set_target_properties(advancedbodies_editor PROPERTIES
    OUTPUT_NAME "advancedbodies_editor"
    DEBUG_POSTFIX ""
    RELEASE_POSTFIX ""
    RELWITHDEBINFO_POSTFIX ""
    MINSIZEREL_POSTFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/editor-modules"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/editor-modules")
foreach(config ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${config} config_upper)
    set_target_properties(advancedbodies_editor PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/editor-modules"
        LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/editor-modules")
endforeach()
# Eden Studio loads it from editor-modules/ (the core list): build it with the editor.
if (BUILD_OPENCS)
    add_dependencies(eden-studio advancedbodies_editor)
endif()
