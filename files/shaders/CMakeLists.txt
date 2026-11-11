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
    lib/light/pointshadow.glsl
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
    lib/material/puddlemap.glsl
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
    gui/rml.vert
    gui/rml.frag
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
    post/fullscreen.vert
    post/tonemap.frag
    post/tonemapconstants.glsl
    post/bloomdown.frag
    post/bloomup.frag
    post/velocity.frag
    post/ao.frag
    post/omwfx.vert
    post/present.vert
    post/present.frag
)

copy_all_resource_files(${CMAKE_CURRENT_SOURCE_DIR} ${OPENMW_RESOURCES_ROOT} ${DDIRRELATIVE} "${SHADER_FILES}")

# Task 0072, stage h: the Slang compiler in the game compiles a define set that has no prebuilt permutation from the
# .slang sources, so they travel with the shaders (resources/shaders, installed with the rest of resources/), and so do
# the library files that compile them byte for byte as the build's slangc does (docs/dependencies.md): slang.dll,
# slang-compiler.dll, slang-glslang.dll and slang-glsl-module.dll on Windows, three .so files on Linux, in
# resources/slang. Without the optimiser in slang-glslang the compile succeeds and returns other code, which the game's
# self-test refuses; without the glsl module Slang writes a cache file into this folder.
if (OPENEDEN_SLANGC)
    file(GLOB_RECURSE openeden_slang_sources RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.slang")
    copy_all_resource_files(${CMAKE_CURRENT_SOURCE_DIR} ${OPENMW_RESOURCES_ROOT} ${DDIRRELATIVE}
        "${openeden_slang_sources}")
    get_filename_component(openeden_slang_bin "${OPENEDEN_SLANGC}" DIRECTORY)
    get_filename_component(openeden_slang_root "${openeden_slang_bin}" DIRECTORY)
    if (WIN32)
        set(openeden_slang_libraries "${openeden_slang_root}/bin/slang.dll"
            "${openeden_slang_root}/bin/slang-compiler.dll" "${openeden_slang_root}/bin/slang-glslang.dll"
            "${openeden_slang_root}/bin/slang-glsl-module.dll")
    else()
        file(GLOB openeden_slang_libraries "${openeden_slang_root}/lib/libslang-compiler.so*"
            "${openeden_slang_root}/lib/libslang-glslang-*.so" "${openeden_slang_root}/lib/libslang-glsl-module-*.so")
    endif()
    foreach (openeden_slang_library ${openeden_slang_libraries})
        file(COPY "${openeden_slang_library}" DESTINATION "${OPENMW_RESOURCES_ROOT}/resources/slang"
            FOLLOW_SYMLINK_CHAIN)
    endforeach()
    # The licence texts travel with the libraries (review of stage h, finding 1): Apache-2.0 section 4(a) and the BSD
    # and MIT notices of what the libraries contain need them beside any redistributed copy. docs/dependencies.md says
    # which licence applies to which file.
    file(COPY "${openeden_slang_root}/LICENSE" "${openeden_slang_root}/LICENSES" "${openeden_slang_root}/third-party-notices"
        DESTINATION "${OPENMW_RESOURCES_ROOT}/resources/slang")
endif()

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
#   -O2                          (permutations only, the twins of GLSL stages) Slang's default -O1 left the object shader's code
#                                0.3 ms of GPU time slower than the GLSL in a lit scene, -O2 is as fast (task 0072, stage j); the
#                                run-time compiler uses the same level (components/vsgutil/slangruntime.cpp)
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
    # Task 0072: sources may include the shared files under lib/ (by the path from this folder, hence -I).
    get_filename_component(source_dir "${source}" DIRECTORY)
    file(GLOB_RECURSE slang_deps "${CMAKE_CURRENT_SOURCE_DIR}/lib/*.slang" "${source_dir}/*.slang")
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
                    -warnings-disable 50011 -I "${CMAKE_CURRENT_SOURCE_DIR}"
                    -entry ${entry} -stage ${stage_name} -o "${output}.tmp"
            COMMAND $<TARGET_FILE:spirv-val> --target-env vulkan1.0 "${output}.tmp"
            COMMAND ${CMAKE_COMMAND} -E rename "${output}.tmp" "${output}"
            DEPENDS "${source}" ${slang_deps} spirv-val
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
# compiles SOURCE once per define set into <dir of STAGE_FILE>/<name of STAGE_FILE>.p<8 hex digits>.spv and writes
# <STAGE_FILE>.permutations, which lists each file with its define set. A set is the canonical list of names
# (sorted, one space between), and "-" is the set with no defines; DEFINE_SETS_FILE holds one set per line (lines
# starting with # are comments). A set is what the GLSL compiler really applies: the Mode defines the stage imports
# with #pragma import_defines (vsgUtil::effectiveDefines). With `[Rendering] slang shaders` on, a stage created with
# exactly a listed set uses its file; any other set keeps the GLSL. The flags and the spirv-val check are those of
# openeden_add_slang_shader.
#
# Stage b (review of stage a): a file is named by the first 8 hex digits of the SHA-1 of its define list, so a file
# left from an older list is never taken for another set; files of this stage that the current list does not make
# are removed when CMake runs, and so are the manifest and the files when the build has no slangc, so a build
# without Slang never ships twins from an older one. slangc itself is a dependency of every file. The C++ reader
# applies the same rules (stem of lower-case letters and digits, names of at most 64 bytes, at most 4096 sets).
function(openeden_add_slang_permutations)
    cmake_parse_arguments(P "" "SOURCE;STAGE_FILE;ENTRY;STAGE;DEFINE_SETS_FILE" "DEFINE_SETS" ${ARGN})
    get_filename_component(stage_dir "${P_STAGE_FILE}" DIRECTORY)
    get_filename_component(stage_name "${P_STAGE_FILE}" NAME)
    set(out_dir "${OPENMW_RESOURCES_ROOT}/${DDIRRELATIVE}/${stage_dir}")
    file(GLOB previous_files "${out_dir}/${stage_name}.p*.spv" "${out_dir}/${stage_name}.p*.spv.tmp")
    if (NOT OPENEDEN_SLANG_SHADERS_BUILT)
        if (previous_files OR EXISTS "${out_dir}/${stage_name}.permutations")
            file(REMOVE ${previous_files} "${out_dir}/${stage_name}.permutations")
        endif()
        return()
    endif()
    if (NOT stage_name MATCHES "^[a-z0-9]+\\.(vert|frag|comp)$")
        message(FATAL_ERROR "openeden_add_slang_permutations: \"${stage_name}\" is not <stem>.<vert|frag|comp>")
    endif()
    # Task 0072 (review of stages b and c): the stages with twins, for the clean-up of stages no longer declared and
    # for the list SlangPermutations checks (openeden_finish_slang_permutations below).
    set_property(GLOBAL APPEND PROPERTY OPENEDEN_SLANG_PERMUTATION_STAGES "${P_STAGE_FILE}")
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
    if (count EQUAL 0 OR count GREATER 4096)
        message(FATAL_ERROR "openeden_add_slang_permutations: ${count} define sets for ${P_STAGE_FILE} (1 to 4096)")
    endif()
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/${P_SOURCE}")
    file(MAKE_DIRECTORY "${out_dir}")
    # Every .slang file beside the source, and the shared ones under lib/, may be included by it.
    get_filename_component(source_dir "${source}" DIRECTORY)
    file(GLOB_RECURSE slang_deps "${CMAKE_CURRENT_SOURCE_DIR}/lib/*.slang" "${source_dir}/*.slang")
    set(outputs "")
    set(manifest "")
    set(names_seen "")
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
                string(LENGTH "${name}" name_length)
                if (name_length GREATER 64)
                    message(FATAL_ERROR "openeden_add_slang_permutations: \"${name}\" is longer than 64 bytes")
                endif()
                list(APPEND defines "-D${name}")
            endforeach()
            set(key "${set_text}")
        endif()
        string(SHA1 key_hash "${key}")
        string(SUBSTRING "${key_hash}" 0 8 short_hash)
        set(file_name "${stage_name}.p${short_hash}.spv")
        if (file_name IN_LIST names_seen)
            message(FATAL_ERROR "openeden_add_slang_permutations: two sets of ${P_STAGE_FILE} hash to ${file_name}")
        endif()
        list(APPEND names_seen "${file_name}")
        set(output "${out_dir}/${file_name}")
        add_custom_command(
            OUTPUT "${output}"
            COMMAND "${OPENEDEN_SLANGC}" "${source}" -target spirv -profile spirv_1_0 -matrix-layout-column-major
                    -warnings-disable 50011 -O2 ${defines} -I "${CMAKE_CURRENT_SOURCE_DIR}"
                    -entry ${P_ENTRY} -stage ${P_STAGE} -o "${output}.tmp"
            COMMAND $<TARGET_FILE:spirv-val> --target-env vulkan1.0 "${output}.tmp"
            COMMAND ${CMAKE_COMMAND} -E rename "${output}.tmp" "${output}"
            DEPENDS "${source}" ${slang_deps} "${OPENEDEN_SLANGC}" spirv-val
            COMMENT "Slang: ${P_SOURCE} ${P_ENTRY} [${set_text}] -> ${stage_dir}/${file_name}"
            VERBATIM)
        list(APPEND outputs "${output}")
        string(APPEND manifest "${file_name}\t${key}\n")
    endforeach()
    # Files of an older list, and half-written ones, must not lie beside the current ones.
    foreach (previous ${previous_files})
        if (NOT previous IN_LIST outputs)
            file(REMOVE "${previous}")
        endif()
    endforeach()
    file(GENERATE OUTPUT "${out_dir}/${stage_name}.permutations" CONTENT "${manifest}")
    # Task 0072, stage h: what the game needs to compile another set of this stage at run time (the same source, entry
    # point and stage this build compiles the listed sets with).
    file(GENERATE OUTPUT "${out_dir}/${stage_name}.slang-stage" CONTENT "${P_SOURCE}\t${P_ENTRY}\t${P_STAGE}\n")
    string(MAKE_C_IDENTIFIER "${P_STAGE_FILE}" target_suffix)
    add_custom_target(slang_perm_${target_suffix} DEPENDS ${outputs})
    add_dependencies(openeden_slang_shaders slang_perm_${target_suffix})
endfunction()

# The GUI (task 0072, stage a): the first family ported through openeden_add_slang_permutations. MyGUI's render
# system loads gui/gui.vert and gui/gui.frag with no defines.
openeden_add_slang_permutations(SOURCE gui/gui.slang STAGE_FILE gui/gui.vert ENTRY vertMain STAGE vertex DEFINE_SETS -)
openeden_add_slang_permutations(SOURCE gui/gui.slang STAGE_FILE gui/gui.frag ENTRY fragMain STAGE fragment DEFINE_SETS -)

# The RmlUi overlay (task 0111, U2): the twins of gui/rml.vert and gui/rml.frag, no defines.
openeden_add_slang_permutations(SOURCE gui/rml.slang STAGE_FILE gui/rml.vert ENTRY vertMain STAGE vertex DEFINE_SETS -)
openeden_add_slang_permutations(SOURCE gui/rml.slang STAGE_FILE gui/rml.frag ENTRY fragMain STAGE fragment DEFINE_SETS -)

# The sky (task 0072, stage b). A set is what the GLSL compiler really applies: the requested Mode defines that the stage
# imports with #pragma import_defines (vsgUtil::effectiveDefines), so the settings-driven defines every pipeline gets
# (COVER, GI, PBR, WETNESS...) do not multiply the sky's sets. The sets are those of OPENEDEN_SHADER_TRACE runs with the
# default settings and with every major feature on (task 0072, stage a); a sky model with other vertex attributes keeps
# the GLSL.
openeden_add_slang_permutations(
    SOURCE sky/atmosphere/atmosphere.slang STAGE_FILE sky/atmosphere/main.vert ENTRY vertMain STAGE vertex DEFINE_SETS -)
openeden_add_slang_permutations(
    SOURCE sky/atmosphere/atmosphere.slang STAGE_FILE sky/atmosphere/main.frag ENTRY fragMain STAGE fragment DEFINE_SETS -)
openeden_add_slang_permutations(SOURCE sky/clouds/clouds.vert.slang STAGE_FILE sky/clouds/main.vert ENTRY vertMain
    STAGE vertex DEFINE_SETS "COLOR NORMAL TEXCOORD TEXMAT VERTEX")
openeden_add_slang_permutations(SOURCE sky/clouds/clouds.frag.slang STAGE_FILE sky/clouds/main.frag ENTRY fragMain
    STAGE fragment DEFINE_SETS "COLOR DIFFUSE_MAP MATERIAL TEXCOORD")
openeden_add_slang_permutations(
    SOURCE sky/moon/moon.vert.slang STAGE_FILE sky/moon/main.vert ENTRY vertMain STAGE vertex DEFINE_SETS -)
openeden_add_slang_permutations(SOURCE sky/moon/moon.frag.slang STAGE_FILE sky/moon/main.frag ENTRY fragMain
    STAGE fragment DEFINE_SETS "DIFFUSE_MAP GLOW_MAP MATERIAL")
openeden_add_slang_permutations(SOURCE sky/night/night.vert.slang STAGE_FILE sky/night/main.vert ENTRY vertMain
    STAGE vertex DEFINE_SETS "COLOR NORMAL TEXCOORD VERTEX")
openeden_add_slang_permutations(SOURCE sky/night/night.frag.slang STAGE_FILE sky/night/main.frag ENTRY fragMain
    STAGE fragment DEFINE_SETS "COLOR DIFFUSE_MAP MATERIAL TEXCOORD")
openeden_add_slang_permutations(
    SOURCE sky/sun/sun.slang STAGE_FILE sky/sun/main.vert ENTRY vertMain STAGE vertex DEFINE_SETS -)
openeden_add_slang_permutations(
    SOURCE sky/sun/sun.slang STAGE_FILE sky/sun/main.frag ENTRY fragMain STAGE fragment DEFINE_SETS "DIFFUSE_MAP MATERIAL")
openeden_add_slang_permutations(
    SOURCE sky/sunglare/sunglare.vert.slang STAGE_FILE sky/sunglare/main.vert ENTRY vertMain STAGE vertex DEFINE_SETS -)
openeden_add_slang_permutations(SOURCE sky/sunglare/sunglare.frag.slang STAGE_FILE sky/sunglare/main.frag ENTRY fragMain
    STAGE fragment DEFINE_SETS MATERIAL)

# The compute passes (task 0072, stage c). They are read with vsgUtil::readShader and compiled with no defines, so each
# has the one set "-" (vsgUtil::withSlangTwin).
openeden_add_slang_permutations(SOURCE comp/fogofwar/update.slang STAGE_FILE comp/fogofwar/update.comp
    ENTRY updateMain STAGE compute DEFINE_SETS -)
openeden_add_slang_permutations(SOURCE comp/worldoverlay/update.slang STAGE_FILE comp/worldoverlay/update.comp
    ENTRY updateMain STAGE compute DEFINE_SETS -)
openeden_add_slang_permutations(SOURCE comp/light/assign.slang STAGE_FILE comp/light/assign.comp
    ENTRY assignMain STAGE compute DEFINE_SETS -)
openeden_add_slang_permutations(SOURCE comp/particle/simulate.slang STAGE_FILE comp/particle/simulate.comp
    ENTRY simulateMain STAGE compute DEFINE_SETS -)

# The water (task 0072, water family). Its vertex stage has no defines; its fragment stage imports BUMP_MAP and
# includes shadow.settings, whose "#define" lines join the set (water/main.frag.sets lists every combination).
openeden_add_slang_permutations(
    SOURCE water/water.vert.slang STAGE_FILE water/main.vert ENTRY vertMain STAGE vertex DEFINE_SETS -)
openeden_add_slang_permutations(SOURCE water/water.frag.slang STAGE_FILE water/main.frag ENTRY fragMain
    STAGE fragment DEFINE_SETS_FILE water/main.frag.sets)
# The terrain (task 0072, terrain family). Its sets come from Pipeline::terrainDefines and the settings files'
# "#define" lines; the vertex stage's are all listed, the fragment stage's are a part of the 26,040 the settings can
# make (terrain/main.frag.sets says which), and any other set draws with the GLSL.
openeden_add_slang_permutations(SOURCE terrain/terrain.vert.slang STAGE_FILE terrain/main.vert ENTRY vertMain
    STAGE vertex DEFINE_SETS_FILE terrain/main.vert.sets)
openeden_add_slang_permutations(SOURCE terrain/terrain.frag.slang STAGE_FILE terrain/main.frag ENTRY fragMain
    STAGE fragment DEFINE_SETS_FILE terrain/main.frag.sets)
# The object shader (task 0072, stage f). Its sets come from the content (each draw's Mode defines) and the settings;
# a set with TEXCOORD or PARTICLE names its count of texture coordinate sets (UVSETS_<n>), by which the pipeline
# chooses (vsgUtil::slangSelectionSet). default/main.vert.sets and main.frag.sets list the sets the default settings
# and the common profiles create; any other set draws with the GLSL.
openeden_add_slang_permutations(SOURCE default/default.vert.slang STAGE_FILE default/main.vert ENTRY vertMain
    STAGE vertex DEFINE_SETS_FILE default/main.vert.sets)
openeden_add_slang_permutations(SOURCE default/default.frag.slang STAGE_FILE default/main.frag ENTRY fragMain
    STAGE fragment DEFINE_SETS_FILE default/main.frag.sets)
# The post passes whose GLSL was written inside C++ (task 0072, stage g): the tonemap, bloom, velocity and ambient
# occlusion passes (components/render) and post-processing's present pass, read with vsgUtil::readPassStages. The
# tonemap's fragment stage has 16 sets (post/tonemap.frag.sets), the others none. post/omwfx.vert (stage h) is the
# .omwfx passes' vertex stage: its partner is always a fragment stage translated from an .omwfx file, which stays GLSL
# (decision 0009), and vsgUtil::readOmwfxVertexStage is the one place that takes this twin beside a GLSL stage. Its
# twin is post/fullscreen.slang's, as the GLSL files are the same text.
openeden_add_slang_permutations(
    SOURCE post/fullscreen.slang STAGE_FILE post/fullscreen.vert ENTRY vertMain STAGE vertex DEFINE_SETS -)
openeden_add_slang_permutations(
    SOURCE post/fullscreen.slang STAGE_FILE post/omwfx.vert ENTRY vertMain STAGE vertex DEFINE_SETS -)
openeden_add_slang_permutations(SOURCE post/tonemap.slang STAGE_FILE post/tonemap.frag ENTRY fragMain
    STAGE fragment DEFINE_SETS_FILE post/tonemap.frag.sets)
openeden_add_slang_permutations(
    SOURCE post/bloom.slang STAGE_FILE post/bloomdown.frag ENTRY downMain STAGE fragment DEFINE_SETS -)
openeden_add_slang_permutations(
    SOURCE post/bloom.slang STAGE_FILE post/bloomup.frag ENTRY upMain STAGE fragment DEFINE_SETS -)
openeden_add_slang_permutations(
    SOURCE post/velocity.slang STAGE_FILE post/velocity.frag ENTRY fragMain STAGE fragment DEFINE_SETS -)
openeden_add_slang_permutations(
    SOURCE post/ao.slang STAGE_FILE post/ao.frag ENTRY fragMain STAGE fragment DEFINE_SETS -)
openeden_add_slang_permutations(
    SOURCE post/present.slang STAGE_FILE post/present.vert ENTRY vertMain STAGE vertex DEFINE_SETS -)
openeden_add_slang_permutations(
    SOURCE post/present.slang STAGE_FILE post/present.frag ENTRY fragMain STAGE fragment DEFINE_SETS -)

# Task 0072 (review of stages b and c): after every family is declared, remove the manifest and files of any stage that
# is no longer declared (a family removed or renamed would otherwise stay in the resources, be installed, be chosen
# with the switch on and be tested), and write slang-permutation-stages.txt, the declared stages one per line, which
# SlangPermutations compares with what it finds. Without slangc every manifest goes and the list is empty.
function(openeden_finish_slang_permutations)
    set(root "${OPENMW_RESOURCES_ROOT}/${DDIRRELATIVE}")
    get_property(declared GLOBAL PROPERTY OPENEDEN_SLANG_PERMUTATION_STAGES)
    if (NOT OPENEDEN_SLANG_SHADERS_BUILT)
        set(declared "")
    endif()
    file(GLOB_RECURSE manifests RELATIVE "${root}" "${root}/*.permutations")
    foreach (manifest ${manifests})
        string(REGEX REPLACE "[.]permutations$" "" stage_file "${manifest}")
        if (NOT stage_file IN_LIST declared)
            get_filename_component(stage_dir "${stage_file}" DIRECTORY)
            get_filename_component(stage_name "${stage_file}" NAME)
            file(GLOB stale "${root}/${stage_dir}/${stage_name}.p*.spv" "${root}/${stage_dir}/${stage_name}.p*.spv.tmp")
            file(REMOVE ${stale} "${root}/${manifest}")
            message(STATUS "Slang: removed the twins of ${stage_file}, which is no longer declared")
        endif()
    endforeach()
    list(SORT declared)
    string(REPLACE ";" "\n" lines "${declared}")
    if (lines)
        string(APPEND lines "\n")
    endif()
    file(GENERATE OUTPUT "${root}/slang-permutation-stages.txt" CONTENT "${lines}")
endfunction()
openeden_finish_slang_permutations()

# The sun's single files of task 0023 (F1b), built from the same source. The game no longer loads them (the sun is a
# permutation family above); they stay only for the task 0023 test SlangSunTest, which reads them. Its removal is
# toddhow's decision (task 0072, stage b log).
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

# The cirrus layer (task 0079), new in Slang: a procedural high cloud sheet on a dome of its own. Path sky/cirrus; no
# GLSL pair (see Pipeline::Graphics::slangOnlyPaths). Loaded when [Rendering] cirrus is on; a missing file leaves the sky
# without it and a log line.
openeden_add_slang_shader(
    SOURCE sky/cirrus/cirrus.slang
    OUTPUT_DIR sky/cirrus
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

# The temporal anti-aliasing resolve (task 0075), new in Slang. Two compute passes loaded whenever [Rendering] taa is on,
# whatever [Rendering] slang shaders says; a missing file turns TAA off with a log line.
openeden_add_slang_shader(
    SOURCE post/taa.slang
    OUTPUT_DIR post
    STAGES resolveMain:compute:taaresolve outputMain:compute:taaoutput)

# Temporal accumulation of ambient occlusion (task 0076), new in Slang. Three compute passes loaded whenever
# [Rendering] ao temporal is on, whatever [Rendering] slang shaders says; a missing file turns it off with a log line.
openeden_add_slang_shader(
    SOURCE post/aotemporal.slang
    OUTPUT_DIR post
    STAGES genMain:compute:aogen resolveMain:compute:aoresolve copyMain:compute:aocopy)
