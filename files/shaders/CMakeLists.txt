if (NOT DEFINED OPENMW_RESOURCES_ROOT)
    return()
endif()

# Copy resource files into the build directory
set(DDIRRELATIVE resources/shaders)
set(SHADER_FILES
    lib/pushconstants.glsl
    lib/inout.glsl
    lib/inout_texcoord.glsl
    lib/sets.glsl
    lib/attributes.glsl
    lib/groundcover.glsl
    lib/light/select.glsl
    lib/light/grid.glsl
    lib/light/gridsize.glsl
    lib/light/equations.glsl
    lib/light/maxlights.glsl
    lib/light/pointlights.glsl
    lib/light/pointspecular.glsl
    lib/light/pointdiffuse.glsl
    lib/light/pixellighting.glsl
    lib/light/pbrlighting.glsl
    lib/light/directional.glsl
    lib/particle/util.glsl
    lib/particle/weatherocclusion.glsl
    lib/util/quickstep.glsl
    lib/particle/data.glsl
    lib/material/terms.glsl
    lib/material/pbr.glsl
    lib/view/gi.glsl
    lib/view/reflection.glsl
    lib/view/skyvis.glsl
    lib/view/cloudshadow.glsl
    lib/view/volumecloudshadow.glsl
    lib/material/wetness.glsl
    lib/material/cover.glsl
    lib/light/precipitation.glsl
    lib/material/bindings.glsl
    lib/material/data.glsl
    lib/material/descriptors_vert.glsl
    lib/material/descriptors_frag.glsl
    lib/material/alphatest.glsl
    lib/material/parallax.glsl
    lib/object/object.glsl
    lib/object/data.glsl
    lib/view/bindings.glsl
    lib/view/data.glsl
    lib/view/shadow.glsl
    lib/view/env.glsl
    lib/view/depthbuffer.glsl
    lib/view/depthrange.glsl
    lib/view/screencoord.glsl
    lib/view/scene.glsl
    lib/view/linear.glsl
    lib/view/raytrace.glsl
    lib/view/reflect.glsl
    lib/view/cascades.glsl
    lib/view/fog.glsl
    lib/view/volume.glsl
    lib/view/dither.glsl
    lib/math/random.glsl
    lib/math/distance.glsl
    lib/math/plane.glsl
    lib/math/rotate.glsl
    lib/math/billboard.glsl
    lib/math/projection.glsl
    default/main.vert
    default/main.frag
    terrain/inout.glsl
    terrain/bindings.glsl
    terrain/data.glsl
    terrain/main.vert
    terrain/main.frag
    water/main.vert
    water/main.frag
    water/fresnel.glsl
    water/rainripples.glsl
    gui/gui.vert
    gui/gui.frag
    sky/atmosphere/inout.glsl
    sky/atmosphere/main.frag
    sky/atmosphere/main.vert
    sky/clouds/inout.glsl
    sky/clouds/lit.glsl
    sky/clouds/main.frag
    sky/clouds/main.vert
    sky/moon/main.frag
    sky/moon/main.vert
    sky/night/inout.glsl
    sky/night/main.frag
    sky/night/main.vert
    sky/sun/main.frag
    sky/sun/main.vert
    sky/sunglare/main.frag
    sky/sunglare/main.vert
    comp/bindings.glsl
    comp/worldoverlay/workgroupsize.glsl
    comp/worldoverlay/update.comp
    comp/light/workgroupsize.glsl
    comp/light/assign.comp
    comp/particle/data.glsl
    comp/particle/workgroupsize.glsl
    comp/particle/bindings.glsl
    comp/particle/simulate.comp
    comp/fogofwar/workgroupsize.glsl
    comp/fogofwar/constants.glsl
    comp/fogofwar/update.comp
)

copy_all_resource_files(${CMAKE_CURRENT_SOURCE_DIR} ${OPENMW_RESOURCES_ROOT} ${DDIRRELATIVE} "${SHADER_FILES}")

# Shaders written in Slang (task 0023, F1; research note 0005, section 7).
#
# openeden_add_slang_shader(SOURCE <file.slang> OUTPUT_DIR <dir under resources/shaders>
#                           STAGES <entry>:<stage>:<suffix> ...)
# runs the pinned slangc once for each stage and writes <OUTPUT_DIR>/main.<suffix>.spv beside the GLSL shaders, where the
# shader reader finds it. For example vertMain:vertex:vert makes main.vert.spv from the entry point vertMain.
# slangc takes one output file per run, so each stage is its own command.
#
# Flags, and why:
#   -profile spirv_1_0           the same SPIR-V version the GLSL path produces (Vulkan 1.0 instance)
#   -matrix-layout-column-major  matrices lie in memory as GLSL's mat4 does, so the C++ side uploads the same bytes
#   -warnings-disable 50011      Slang warns that SPIR-V 1.0 is old; that is the point
# slangc validates its output with the SPIRV-Tools inside it unless -skip-spirv-validation is given, which it is not.
# On top of that every output is checked by the pinned spirv-val (extern/CMakeLists.txt) for Vulkan 1.0. slangc writes
# to <output>.tmp, spirv-val checks it, and only a valid file is renamed to <output>: a rejected shader fails the build
# and leaves no .spv behind that a second build would take as up to date.
# Where there is no slangc (OPENEDEN_SLANGC empty) nothing is built and the game uses the GLSL.
set(OPENEDEN_SLANG_SHADERS_BUILT OFF CACHE INTERNAL "True when the .slang shaders are compiled by this build")
if (OPENEDEN_SLANGC)
    get_generator_is_multi_config(openeden_multi_config)
    if (openeden_multi_config)
        # copy_resource_file writes resources into <root>/<Config>/ for these generators; the .spv files are not.
        message(WARNING "Slang shaders are not built with a multi-config generator yet; the GLSL shaders are used")
    else()
        add_custom_target(openeden_slang_shaders ALL)
        # Proof that the validator rejects a broken file (otherwise "passes spirv-val" would mean nothing).
        add_custom_target(openeden_spirv_val_rejects_bad_input ALL
            COMMAND ${CMAKE_COMMAND} -DSPIRV_VAL=$<TARGET_FILE:spirv-val>
                    -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_spirv_val_rejects.cmake
            DEPENDS spirv-val
            COMMENT "spirv-val: a broken SPIR-V file must be rejected"
            VERBATIM)
        add_dependencies(openeden_slang_shaders openeden_spirv_val_rejects_bad_input)
        set(OPENEDEN_SLANG_SHADERS_BUILT ON CACHE INTERNAL "True when the .slang shaders are compiled by this build")
    endif()
endif()

function(openeden_add_slang_shader)
    cmake_parse_arguments(S "" "SOURCE;OUTPUT_DIR" "STAGES" ${ARGN})
    if (NOT OPENEDEN_SLANG_SHADERS_BUILT)
        return()
    endif()
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/${S_SOURCE}")
    set(out_dir "${OPENMW_RESOURCES_ROOT}/${DDIRRELATIVE}/${S_OUTPUT_DIR}")
    file(MAKE_DIRECTORY "${out_dir}")
    set(outputs "")
    foreach (stage ${S_STAGES})
        string(REPLACE ":" ";" parts "${stage}")
        list(GET parts 0 entry)
        list(GET parts 1 stage_name)
        list(GET parts 2 suffix)
        set(output "${out_dir}/main.${suffix}.spv")
        add_custom_command(
            OUTPUT "${output}"
            COMMAND "${OPENEDEN_SLANGC}" "${source}" -target spirv -profile spirv_1_0 -matrix-layout-column-major
                    -warnings-disable 50011 -entry ${entry} -stage ${stage_name} -o "${output}.tmp"
            COMMAND $<TARGET_FILE:spirv-val> --target-env vulkan1.0 "${output}.tmp"
            COMMAND ${CMAKE_COMMAND} -E rename "${output}.tmp" "${output}"
            DEPENDS "${source}" spirv-val
            COMMENT "Slang: ${S_SOURCE} ${entry} -> ${S_OUTPUT_DIR}/main.${suffix}.spv"
            VERBATIM)
        list(APPEND outputs "${output}")
    endforeach()
    string(MAKE_C_IDENTIFIER "${S_SOURCE}" target_suffix)
    add_custom_target(slang_${target_suffix} DEPENDS ${outputs})
    add_dependencies(openeden_slang_shaders slang_${target_suffix})
endfunction()

# Slang twins of GLSL stages, one SPIR-V file per define set (task 0072; components/vsgutil/shaderpermutation.hpp).
#
# openeden_add_slang_permutations(SOURCE <file.slang> STAGE_FILE <the GLSL stage, e.g. gui/gui.vert>
#                                 ENTRY <entry point> STAGE <vertex|fragment|compute>
#                                 [DEFINE_SETS <set> ...] [DEFINE_SETS_FILE <file>])
# compiles SOURCE once per define set into <dir of STAGE_FILE>/<name of STAGE_FILE>.p<NNNN>.spv and writes
# <STAGE_FILE>.permutations, which lists each file with its define set. A set is the canonical list of names
# (sorted, one space between), and "-" is the set with no defines; DEFINE_SETS_FILE holds one set per line (lines
# starting with # are comments). With `[Rendering] slang shaders` on, a stage created with exactly a listed set uses
# its file; any other set keeps the GLSL. The flags and the spirv-val check are those of openeden_add_slang_shader.
function(openeden_add_slang_permutations)
    cmake_parse_arguments(P "" "SOURCE;STAGE_FILE;ENTRY;STAGE;DEFINE_SETS_FILE" "DEFINE_SETS" ${ARGN})
    if (NOT OPENEDEN_SLANG_SHADERS_BUILT)
        return()
    endif()
    set(sets ${P_DEFINE_SETS})
    if (P_DEFINE_SETS_FILE)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${P_DEFINE_SETS_FILE}")
        file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/${P_DEFINE_SETS_FILE}" file_sets)
        foreach (line ${file_sets})
            string(STRIP "${line}" line)
            if (NOT line STREQUAL "" AND NOT line MATCHES "^#")
                list(APPEND sets "${line}")
            endif()
        endforeach()
    endif()
    list(LENGTH sets count)
    if (count EQUAL 0)
        message(FATAL_ERROR "openeden_add_slang_permutations: no define sets for ${P_STAGE_FILE}")
    endif()
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/${P_SOURCE}")
    get_filename_component(stage_dir "${P_STAGE_FILE}" DIRECTORY)
    get_filename_component(stage_name "${P_STAGE_FILE}" NAME)
    set(out_dir "${OPENMW_RESOURCES_ROOT}/${DDIRRELATIVE}/${stage_dir}")
    file(MAKE_DIRECTORY "${out_dir}")
    # Every .slang file beside the source may be included by it.
    get_filename_component(source_dir "${source}" DIRECTORY)
    file(GLOB_RECURSE slang_deps "${CMAKE_CURRENT_SOURCE_DIR}/lib/*.slang" "${source_dir}/*.slang")
    set(outputs "")
    set(manifest "")
    set(index 0)
    foreach (set_text ${sets})
        if (NOT set_text MATCHES "^(-|[A-Za-z_][A-Za-z0-9_]*( [A-Za-z_][A-Za-z0-9_]*)*)$")
            message(FATAL_ERROR "openeden_add_slang_permutations: \"${set_text}\" is not a define set (${P_STAGE_FILE})")
        endif()
        set(defines "")
        set(key "")
        if (NOT set_text STREQUAL "-")
            string(REPLACE " " ";" names "${set_text}")
            set(sorted ${names})
            list(SORT sorted)
            list(REMOVE_DUPLICATES sorted)
            if (NOT sorted STREQUAL names)
                message(FATAL_ERROR "openeden_add_slang_permutations: \"${set_text}\" is not sorted (${P_STAGE_FILE})")
            endif()
            foreach (name ${names})
                list(APPEND defines "-D${name}")
            endforeach()
            set(key "${set_text}")
        endif()
        string(LENGTH "000${index}" len)
        math(EXPR start "${len} - 4")
        string(SUBSTRING "000${index}" ${start} 4 padded)
        set(file_name "${stage_name}.p${padded}.spv")
        set(output "${out_dir}/${file_name}")
        add_custom_command(
            OUTPUT "${output}"
            COMMAND "${OPENEDEN_SLANGC}" "${source}" -target spirv -profile spirv_1_0 -matrix-layout-column-major
                    -warnings-disable 50011 ${defines} -entry ${P_ENTRY} -stage ${P_STAGE} -o "${output}.tmp"
            COMMAND $<TARGET_FILE:spirv-val> --target-env vulkan1.0 "${output}.tmp"
            COMMAND ${CMAKE_COMMAND} -E rename "${output}.tmp" "${output}"
            DEPENDS "${source}" ${slang_deps} spirv-val
            COMMENT "Slang: ${P_SOURCE} ${P_ENTRY} [${set_text}] -> ${stage_dir}/${file_name}"
            VERBATIM)
        list(APPEND outputs "${output}")
        string(APPEND manifest "${file_name}\t${key}\n")
        math(EXPR index "${index} + 1")
    endforeach()
    file(GENERATE OUTPUT "${out_dir}/${stage_name}.permutations" CONTENT "${manifest}")
    string(MAKE_C_IDENTIFIER "${P_STAGE_FILE}" target_suffix)
    add_custom_target(slang_perm_${target_suffix} DEPENDS ${outputs})
    add_dependencies(openeden_slang_shaders slang_perm_${target_suffix})
endfunction()

# The GUI (task 0072, stage a): the first family ported through openeden_add_slang_permutations. MyGUI's render
# system loads gui/gui.vert and gui/gui.frag with no defines.
openeden_add_slang_permutations(SOURCE gui/gui.slang STAGE_FILE gui/gui.vert ENTRY vertMain STAGE vertex DEFINE_SETS -)
openeden_add_slang_permutations(SOURCE gui/gui.slang STAGE_FILE gui/gui.frag ENTRY fragMain STAGE fragment DEFINE_SETS -)

# The sun, ported by hand as the proof of the pipeline (task 0023, F1b). Used when [Rendering] slang shaders is on.
openeden_add_slang_shader(
    SOURCE sky/sun/sun.slang
    OUTPUT_DIR sky/sun
    STAGES vertMain:vertex:vert fragMain:fragment:frag)

# The procedural sun (task 0047), new in Slang. Its path is sky/sunproc, not sky/sun, so that the textured sun (GLSL, and
# the Slang port above) and the procedural one can both be loaded; it has no GLSL pair (see Pipeline::Graphics::
# slangOnlyPaths). Loaded when [Rendering] procedural sun is on; a missing file leaves the textured sun with a log line.
openeden_add_slang_shader(
    SOURCE sky/sun/proceduralsun.slang
    OUTPUT_DIR sky/sunproc
    STAGES vertMain:vertex:vert fragMain:fragment:frag)

# The volumetric fog's compute passes (task 0041). They have no GLSL: the game loads these files whenever
# [Rendering] volumetrics is on, whatever [Rendering] slang shaders says, and leaves volumetrics off with a log line
# if they are missing.
openeden_add_slang_shader(
    SOURCE comp/volume/volume.slang
    OUTPUT_DIR comp/volume
    STAGES fillMain:compute:fill integrateMain:compute:integrate temporalMain:compute:temporal)

# The volumetric clouds' compute passes (task 0042), loaded whenever [Rendering] volumetric clouds is on; a missing file
# leaves the clouds off with a log line.
openeden_add_slang_shader(
    SOURCE comp/cloud/cloud.slang
    OUTPUT_DIR comp/cloud
    STAGES noiseShapeMain:compute:noiseshape noiseDetailMain:compute:noisedetail noiseCurlMain:compute:noisecurl
        marchMain:compute:march resolveMain:compute:resolve copyMain:compute:copy shadowMain:compute:shadow)
