# Which archives Autorun asks nxvk for, and in what order.
#
# nxvk's OpenGL half only works whole-archived (Mesa's EGL, Gallium and Zink
# reach each other through symbols no entry point names), and the Vulkan half
# only works whole-archived for the same reason; the support archives have to
# come after both, in a group, or the link stops on the first Mesa utility
# symbol. Nothing here needs a toolchain: the trees are made up, and the CMake
# module is asked about them.
#
#   cmake -P horizon-wine/tests/nxvk_archives.cmake

set(here "${CMAKE_CURRENT_LIST_DIR}")
set(module "${here}/../cmake/Nxvk.cmake")
include("${module}")

# nxvk's Makefile, GL_LIBS, SUPPORT_LIBS and the driver, as of its `switch`
# branch. A test that had to be updated because the module changed would not be
# a test, so these are written out here.
set(gl_archives
    src/egl/libEGL.a
    src/mesa/libmesa.a
    src/compiler/glsl/libglsl.a
    src/compiler/glsl/glcpp/libglcpp.a
    src/mesa/glapi/shared-glapi/libglapi.a
    src/mesa/glapi/glapi/libglapi_bridge.a
    src/gallium/auxiliary/libgallium.a
    src/gallium/drivers/zink/libzink.a
    src/gallium/winsys/zink/drm/libzinkwinsys.a
    src/gallium/winsys/sw/null/libws_null.a
    src/gallium/winsys/sw/wrapper/libwsw.a)
set(support_archives
    src/util/libmesa_util.a
    src/util/libmesa_util_simd.a
    src/util/blake3/libblake3.a
    src/c11/impl/libmesa_util_c11.a
    src/nouveau/compiler/libnak.a
    src/nouveau/compiler/libnak_rs.a
    src/compiler/rust/libcompiler_c_helpers.a
    src/nouveau/headers/libnvidia_headers_c.a
    src/nouveau/nil/libnil.a
    src/nouveau/nil/liblibnil_format_table.a
    src/compiler/nir/libnir.a
    src/compiler/libcompiler.a
    src/nouveau/mme/libnouveau_mme.a
    src/nouveau/winsys/libnouveau_ws.a
    src/vulkan/util/libvulkan_util.a
    src/compiler/spirv/libvtn.a
    src/util/libxmlconfig.a)
set(driver_archive src/nouveau/vulkan/libnvk.a)

function(fail what)
    message(FATAL_ERROR "nxvk archives: ${what}")
endfunction()

function(make_file path)
    get_filename_component(dir "${path}" DIRECTORY)
    file(MAKE_DIRECTORY "${dir}")
    file(WRITE "${path}" "")
endfunction()

function(expect_order list first second)
    list(FIND ${list} "${first}" a)
    list(FIND ${list} "${second}" b)
    if(a EQUAL -1)
        fail("${first} is not in ${list}")
    elseif(b EQUAL -1)
        fail("${second} is not in ${list}")
    elseif(NOT a LESS b)
        fail("${first} has to come before ${second}")
    endif()
endfunction()

function(expect_missing list entry)
    list(FIND ${list} "${entry}" at)
    if(NOT at EQUAL -1)
        fail("${entry} must not be in ${list}")
    endif()
endfunction()

# The module and nxvk's Makefile have to name the same archives.
if(NOT "${_nxvk_gl_archives}" STREQUAL "${gl_archives}")
    fail("the OpenGL archives are not nxvk's GL_LIBS:\n  module: ${_nxvk_gl_archives}\n  nxvk:   ${gl_archives}")
endif()
if(NOT "${_nxvk_support_archives}" STREQUAL "${support_archives}")
    fail("the support archives are not nxvk's SUPPORT_LIBS:\n  module: ${_nxvk_support_archives}\n  nxvk:   ${support_archives}")
endif()

set(scratch "${CMAKE_CURRENT_BINARY_DIR}/nxvk-test")
file(REMOVE_RECURSE "${scratch}")

# 1. Installed: `make install-gl` into a prefix.
set(prefix "${scratch}/portlibs")
foreach(archive libnvk.a libnvk_support.a libnvk_gl.a)
    make_file("${prefix}/lib/${archive}")
endforeach()
make_file("${prefix}/include/vulkan/vulkan.h")

wine_nx_nxvk_detect("${prefix}")
if(NOT NXVK_DESCRIPTION MATCHES "installed in")
    fail("an installed nxvk was not recognised: ${NXVK_DESCRIPTION}")
endif()
if(NOT NXVK_INCLUDE_DIRS STREQUAL "${prefix}/include")
    fail("the installed headers are not ${prefix}/include: ${NXVK_INCLUDE_DIRS}")
endif()
expect_order(NXVK_ARCHIVES -Wl,--whole-archive "${prefix}/lib/libnvk_gl.a")
expect_order(NXVK_ARCHIVES "${prefix}/lib/libnvk_gl.a" "${prefix}/lib/libnvk.a")
expect_order(NXVK_ARCHIVES "${prefix}/lib/libnvk.a" -Wl,--no-whole-archive)
expect_order(NXVK_ARCHIVES -Wl,--no-whole-archive -Wl,--start-group)
expect_order(NXVK_ARCHIVES -Wl,--start-group "${prefix}/lib/libnvk_support.a")
expect_order(NXVK_ARCHIVES "${prefix}/lib/libnvk_support.a" -Wl,--end-group)
expect_order(NXVK_ARCHIVES -Wl,--end-group -Wl,-u,vk_icdGetInstanceProcAddr)
expect_missing(NXVK_ARCHIVES "${prefix}/lib/libEGL.a")

# 2. Built but not installed: the same code, still one archive per directory.
set(checkout "${scratch}/nxvk")
foreach(archive ${gl_archives} ${support_archives} ${driver_archive})
    make_file("${checkout}/switch/build/cross-zink/${archive}")
endforeach()
make_file("${checkout}/include/vulkan/vulkan.h")

set(WINE_NX_NXVK_BUILD "" CACHE PATH "" FORCE)
wine_nx_nxvk_detect("${checkout}")
if(NOT NXVK_DESCRIPTION MATCHES "built in")
    fail("a built nxvk was not recognised: ${NXVK_DESCRIPTION}")
endif()
foreach(archive ${gl_archives})
    list(FIND NXVK_ARCHIVES "${checkout}/switch/build/cross-zink/${archive}" at)
    if(at EQUAL -1)
        fail("the OpenGL archive ${archive} is not linked")
    endif()
endforeach()
expect_order(NXVK_ARCHIVES -Wl,--whole-archive "${checkout}/switch/build/cross-zink/src/gallium/drivers/zink/libzink.a")
expect_order(NXVK_ARCHIVES "${checkout}/switch/build/cross-zink/src/gallium/drivers/zink/libzink.a"
             "${checkout}/switch/build/cross-zink/${driver_archive}")
expect_order(NXVK_ARCHIVES "${checkout}/switch/build/cross-zink/${driver_archive}" -Wl,--no-whole-archive)
expect_order(NXVK_ARCHIVES -Wl,--no-whole-archive "${checkout}/switch/build/cross-zink/src/util/libmesa_util.a")
expect_order(NXVK_ARCHIVES "${checkout}/switch/build/cross-zink/src/util/libmesa_util.a" -Wl,--end-group)

# 3. A named build tree that is not there, and an archive missing from one that
#    is: both stop the configure with the nxvk target that builds the missing
#    piece, instead of failing later in the link.
file(WRITE "${scratch}/missing-build.cmake" "
    include(\"${module}\")
    wine_nx_nxvk_detect(\"${checkout}\")
")
execute_process(COMMAND "${CMAKE_COMMAND}" -DWINE_NX_NXVK_BUILD=${scratch}/no-such-tree
                -P "${scratch}/missing-build.cmake"
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
if(result EQUAL 0)
    fail("a build tree that is not there did not stop the configure")
endif()
if(NOT output MATCHES "make gl")
    fail("the message for a missing build tree does not say what builds it: ${output}")
endif()

file(REMOVE "${checkout}/switch/build/cross-zink/src/gallium/drivers/zink/libzink.a")
execute_process(COMMAND "${CMAKE_COMMAND}" -P "${scratch}/missing-build.cmake"
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
if(result EQUAL 0)
    fail("a build tree without libzink.a did not stop the configure")
endif()
if(NOT output MATCHES "libzink.a" OR NOT output MATCHES "make gl")
    fail("the message for a missing libzink.a does not name it and its target: ${output}")
endif()

# 4. A directory that is neither.
file(WRITE "${scratch}/not-nxvk.cmake" "
    include(\"${module}\")
    wine_nx_nxvk_detect(\"${scratch}\")
")
execute_process(COMMAND "${CMAKE_COMMAND}" -P "${scratch}/not-nxvk.cmake"
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
if(result EQUAL 0)
    fail("a directory that is not nxvk did not stop the configure")
endif()
if(NOT output MATCHES "libnvk_gl.a")
    fail("the message for a directory that is not nxvk does not say what it looks for: ${output}")
endif()

file(REMOVE_RECURSE "${scratch}")
message(STATUS "nxvk archives: the link line, the build tree and the refusals are as nxvk asks")
