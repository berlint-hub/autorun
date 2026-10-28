# nxvk (github.com/PalindromicBreadLoaf/nxvk) is a second port of Mesa 26 to
# Horizon: Vulkan through NVK, and OpenGL as Zink over that same NVK. It gives
# the runtime what mesa-switch gives it -- EGL on the screen's NWindow, desktop
# GL, and a Vulkan ICD with VK_NN_vi_surface -- but not in the same archives.
#
# devkitPro's Mesa ships one archive per API (libEGL.a, libGL.a, libglapi.a,
# libvulkan.a) which can be linked in a group. nxvk's OpenGL half is Mesa's own
# per-directory statics (libEGL.a, libmesa.a, libgallium.a, libzink.a, ...) and
# its Vulkan half is libnvk.a; both are built to be whole-archived, because
# only part of each is reached through the API entry points, and their support
# archives (Mesa's utility code, NAK, NIL, NIR, the Vulkan helpers) follow in a
# group. That is exactly how nxvk's own switch/build/build-nro.sh links its
# smoke programs, and nxvk-gl.pc records the same line for the installed case.
#
# Either of two shapes is accepted, and the variables set here are the same
# either way:
#
#   installed  <dir>/lib/libnvk.a, libnvk_support.a, libnvk_gl.a and
#              <dir>/include/vulkan, as nxvk's `sudo make install-gl` leaves
#              them in $DEVKITPRO/portlibs/switch;
#   built      a checkout that ran `make` and `make gl` but was not installed,
#              where the same code is still one archive per directory under
#              switch/build/cross-zink (WINE_NX_NXVK_BUILD names another tree).
#
# nxvk's own files (src/egl/drivers/horizon, src/vulkan/wsi/wsi_switch.c,
# src/nouveau/vulkan/nvkmd/nvgpu) are GPL-2.0-or-later, so a runtime linked
# against them is a combined GPL work: the package has to carry its source.
# See documentation/technical.md.

set(WINE_NX_NXVK_BUILD "" CACHE PATH
    "nxvk's build tree when its archives are not installed (default: <dir>/switch/build/cross-zink)")

# The OpenGL frontend, whole-archived. Named as in nxvk's Makefile (GL_LIBS).
set(_nxvk_gl_archives
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

# What both halves link against, in a group after them (Makefile SUPPORT_LIBS).
set(_nxvk_support_archives
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

set(_nxvk_driver_archive src/nouveau/vulkan/libnvk.a)

# zlib and expat are the only outside libraries either half needs; Mesa leaves
# libelf and zstd out of a portlibs build (see the Makefile's PORTLIBS).
function(_nxvk_outside_libraries out)
    foreach(_name z expat)
        find_library(_found_${_name} ${_name} HINTS "$ENV{DEVKITPRO}/portlibs/switch/lib")
        if(_found_${_name})
            list(APPEND _libs "${_found_${_name}}")
        else()
            list(APPEND _libs "${_name}") # a bare name is -l<name>, which the toolchain resolves
        endif()
    endforeach()
    set(${out} "${_libs}" PARENT_SCOPE)
endfunction()

# wine_nx_nxvk_detect(<dir>) stops the configure when the archives are not
# there, naming the one that is missing and the nxvk target that builds it, and
# otherwise sets in the caller's scope:
#   NXVK_ARCHIVES      the whole link line for EGL, GL and the Vulkan ICD
#   NXVK_INCLUDE_DIRS  the headers that belong to those archives
#   NXVK_DEFINITIONS   the -D flags nxvk-gl.pc asks applications for
#   NXVK_DESCRIPTION   one line for the configure output
function(wine_nx_nxvk_detect dir)
    get_filename_component(dir "${dir}" ABSOLUTE)
    if(NOT IS_DIRECTORY "${dir}")
        message(FATAL_ERROR "WINE_NX_NXVK_DIR is not a directory: ${dir}")
    endif()

    _nxvk_outside_libraries(_outside)

    if(EXISTS "${dir}/lib/libnvk_gl.a")
        # Installed into a prefix (nxvk's `make install-gl`).
        foreach(_archive libnvk.a libnvk_support.a libnvk_gl.a)
            if(NOT EXISTS "${dir}/lib/${_archive}")
                message(FATAL_ERROR
                    "nxvk is installed in ${dir} but lib/${_archive} is missing: run "
                    "\"make gl\" and \"sudo make install-gl\" in the nxvk checkout")
            endif()
        endforeach()
        set(_archives
            -Wl,--whole-archive
            "${dir}/lib/libnvk_gl.a"
            "${dir}/lib/libnvk.a"
            -Wl,--no-whole-archive
            -Wl,--start-group
            "${dir}/lib/libnvk_support.a"
            ${_outside}
            -Wl,--end-group
            -Wl,-u,vk_icdGetInstanceProcAddr)
        set(_includes "${dir}/include")
        set(_description "nxvk (Zink over NVK), installed in ${dir}")
    elseif(EXISTS "${dir}/switch/build" OR EXISTS "${dir}/src/gallium/drivers/zink")
        # A checkout that was built but not installed.
        set(_build "${WINE_NX_NXVK_BUILD}")
        if(NOT _build)
            set(_build "${dir}/switch/build/cross-zink")
        endif()
        get_filename_component(_build "${_build}" ABSOLUTE)
        if(NOT IS_DIRECTORY "${_build}")
            message(FATAL_ERROR
                "nxvk's build tree is not there: ${_build}\n"
                "Build the OpenGL half first, inside nxvk's toolchain image:\n"
                "  make image && make && make gl\n"
                "or name another tree with -DWINE_NX_NXVK_BUILD=<switch/build/cross-zink>")
        endif()
        set(_gl "")
        foreach(_archive ${_nxvk_gl_archives})
            if(NOT EXISTS "${_build}/${_archive}")
                message(FATAL_ERROR
                    "nxvk's build tree ${_build} has no ${_archive}: run \"make gl\" there "
                    "(it builds the EGL, Mesa, Gallium and Zink archives together)")
            endif()
            list(APPEND _gl "${_build}/${_archive}")
        endforeach()
        if(NOT EXISTS "${_build}/${_nxvk_driver_archive}")
            message(FATAL_ERROR
                "nxvk's build tree ${_build} has no ${_nxvk_driver_archive}: run \"make\" there")
        endif()
        set(_support "")
        foreach(_archive ${_nxvk_support_archives})
            if(NOT EXISTS "${_build}/${_archive}")
                message(FATAL_ERROR
                    "nxvk's build tree ${_build} has no ${_archive}, one of libnvk.a's support "
                    "archives: build the driver with \"make\" and the Zink stack with \"make gl\"")
            endif()
            list(APPEND _support "${_build}/${_archive}")
        endforeach()
        set(_archives
            -Wl,--whole-archive
            ${_gl}
            "${_build}/${_nxvk_driver_archive}"
            -Wl,--no-whole-archive
            -Wl,--start-group
            ${_support}
            ${_outside}
            -Wl,--end-group
            -Wl,-u,vk_icdGetInstanceProcAddr)
        set(_includes "${dir}/include")
        set(_description "nxvk (Zink over NVK), built in ${_build}")
    else()
        message(FATAL_ERROR
            "WINE_NX_NXVK_DIR is neither an nxvk install prefix (lib/libnvk_gl.a) nor an nxvk "
            "checkout (switch/build): ${dir}")
    endif()

    if(NOT EXISTS "${_includes}/vulkan/vulkan.h")
        message(FATAL_ERROR
            "no Vulkan headers in ${_includes}: nxvk's include/vulkan and include/vk_video are "
            "part of its install, and a checkout has them in its own include/")
    endif()

    set(NXVK_ARCHIVES "${_archives}" PARENT_SCOPE)
    set(NXVK_INCLUDE_DIRS "${_includes}" PARENT_SCOPE)
    # nxvk-gl.pc's Cflags. HAVE_PTHREAD is what Zink's client headers expect;
    # Wine itself has no use for the name and nothing reads it.
    set(NXVK_DEFINITIONS -DHAVE_PTHREAD PARENT_SCOPE)
    set(NXVK_DESCRIPTION "${_description}" PARENT_SCOPE)
endfunction()
