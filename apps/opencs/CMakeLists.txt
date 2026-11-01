# csmodel: the editor's data, documents, tools and preferences (task 0014, stage C1). It must build and be tested
# without any view code, as must eden-studio-tests. The three widgets are here because model/prefs/coloursetting.cpp
# and model/world/idcompletionmanager.cpp use them; they are plain Qt with no 3D.
set (OPENCS_SRC
    )

opencs_units (model/doc
    document operation saving documentmanager loader runner operationholder
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
    shortcuteventhandler shortcutmanager shortcutsetting modifiersetting stringsetting subcategory
    )

opencs_units (model/prefs
    category
    )

opencs_units (model/extension
    editorextensionhost
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

opencs_units (. editor capture)

opencs_units (view/doc
    viewmanager view operations operation subview startup filedialog newgame
    filewidget adjusterwidget loader globaldebugprofilemenu runlogsubview studioshell extensionmanager assetbrowser contextinspector outputlogpanel logtap
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
    rendercontext scenehost scenewidget worldspacewidget pagedworldspacewidget unpagedworldspacewidget previewwidget
    editmode lighting object cell cellarrow colouredgeometry actor cellborder cellmarker cellwater terrainstorage
    cameracontroller orbitcameramode
    instancemode instanceselectionmode instancemovemode selectionmode objectmarker overlaylines
    terrainshapemode terraintexturemode terrainselection brushdraw commands
    pathgrid pathgridmode pathgridselectionmode
    )

opencs_units (view/world
    scenesubview previewsubview
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
    dialogue pagebase page keybindingpage contextmenulist
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
