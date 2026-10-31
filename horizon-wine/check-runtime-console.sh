#!/bin/sh
# Host tests for the runtime: the copy of stdout/stderr into its log, Horizon
# file access (rights, descriptor modes, open-file path matching) and the
# analog-stick mouse cursor.
set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cc="${CC:-$(command -v clang || command -v cc || command -v gcc)}"
[ -n "$cc" ] || { echo "No C compiler on PATH; set CC." >&2; exit 1; }
build="$(mktemp -d "${TMPDIR:-/tmp}/wine-nx-console.XXXXXX")"
trap 'rm -rf "$build"' EXIT HUP INT TERM
# -lm: glibc does not link the math library in by itself, clang's headers do.
flags="-std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -lm"
"$cc" $flags "$root/horizon-wine/tests/std_stream_lines.c" -o "$build/std_stream_lines"
"$build/std_stream_lines"
"$cc" $flags -I"$root/include" "$root/horizon-wine/tests/horizon_file_access.c" -o "$build/file_access"
"$build/file_access"
# The static unixlib table pairs FEX's CPU modules, including a variant build's
# libwow64fex-opt.dll; a miss leaves the module with no unixlib.
"$cc" $flags "$root/horizon-wine/tests/horizon_unixlib_names.c" -o "$build/unixlib_names"
"$build/unixlib_names"
CC="$cc" python3 "$root/horizon-wine/tests/check_fex_unixlib.py"
# The same table's contract for everything else it holds: exact names for the
# native and WoW64 callers, stable handles, and the FEX stems that pair a
# variant build with its unixlib.
CC="$cc" python3 "$root/horizon-wine/tests/check_static_unix_libs.py"
"$cc" $flags "$root/horizon-wine/tests/pointer_cursor.c" -o "$build/pointer_cursor"
"$build/pointer_cursor"
"$cc" $flags -I"$root/horizon-wine/source" \
    "$root/horizon-wine/tests/pointer_overlay.c" -o "$build/pointer_overlay"
"$build/pointer_overlay"
# Where the arrow lands on the panel, and the pointer following a screen that
# changed size under it.
CC="$cc" python3 "$root/horizon-wine/tests/check_cursor_mapping.py"
"$cc" $flags "$root/horizon-wine/tests/horizon_message_queue.c" -o "$build/message_queue"
"$build/message_queue"
"$cc" $flags "$root/horizon-wine/tests/horizon_win_timers.c" -o "$build/win_timers"
"$build/win_timers"
"$cc" $flags "$root/horizon-wine/tests/horizon_msg_queue.c" -o "$build/msg_queue"
"$build/msg_queue"
"$cc" $flags "$root/horizon-wine/tests/horizon_clipboard.c" -o "$build/clipboard"
"$build/clipboard"
"$cc" $flags "$root/horizon-wine/tests/launcher_list.c" -o "$build/launcher_list"
"$build/launcher_list"
"$cc" $flags "$root/horizon-wine/tests/launcher_settings.c" -o "$build/launcher_settings"
"$build/launcher_settings"
"$cc" $flags "$root/horizon-wine/tests/launcher_preset.c" \
    "$root/horizon-wine/source/launcher_preset.c" -o "$build/launcher_preset"
"$build/launcher_preset"
"$cc" $flags "$root/horizon-wine/tests/launcher_pe.c" -o "$build/launcher_pe"
"$build/launcher_pe"
"$cc" $flags "$root/horizon-wine/tests/sd_read_cache.c" -o "$build/sd_read_cache"
"$cc" $flags "$root/horizon-wine/tests/sd_write_buffer.c" -o "$build/sd_write_buffer"
"$cc" $flags "$root/horizon-wine/tests/sd_stat_cache.c" -o "$build/sd_stat_cache"
"$build/sd_stat_cache"
"$build/sd_write_buffer"
"$build/sd_read_cache"
# The device itself, over a fake card: what the cache holds as the runtime
# opens, writes and reads, and the directories it leaves alone -- FEX's code
# cache database is read a block at a time and does not take the game's chunks.
"$cc" $flags -I"$root/horizon-wine/tests/sd-cache-shims" \
    "$root/horizon-wine/tests/sd_cache_device.c" -o "$build/sd_cache_device"
"$build/sd_cache_device"
# And that the runtime still names that directory to it: the translator's cache
# is where fex_cache.h says the launcher measures it, both halves of it.
python3 "$root/horizon-wine/tests/check_sd_cache_exclusion.py"
# A program whose starts in a row did not come back begins the next one with the
# Compatibility fast paths, and a clean exit puts the count back to nothing.
"$cc" $flags "$root/horizon-wine/tests/start_guard.c" -o "$build/start_guard"
"$build/start_guard"
python3 "$root/horizon-wine/tests/check_start_guard.py"
"$cc" $flags "$root/horizon-wine/tests/box64_code_arena.c" -o "$build/box64_code_arena"
"$build/box64_code_arena"
"$cc" $flags "$root/horizon-wine/tests/horizon_registry.c" -o "$build/registry"
"$build/registry"
"$cc" $flags "$root/horizon-wine/tests/registry_paths.c" -o "$build/registry_paths"
"$build/registry_paths"
"$cc" $flags "$root/horizon-wine/tests/horizon_read_redirect.c" -o "$build/read_redirect"
"$build/read_redirect"
"$cc" $flags "$root/horizon-wine/tests/horizon_object_dirs.c" -o "$build/object_dirs"
"$build/object_dirs"
"$cc" $flags "$root/horizon-wine/tests/horizon_keyboard.c" -o "$build/keyboard"
"$build/keyboard"
"$cc" $flags -D__WINESRC__ -DWINE_UNIX_LIB -D_WIN64 -I"$root/include" "$root/horizon-wine/tests/horizon_completion.c" -o "$build/completion"
"$build/completion"
"$cc" $flags "$root/horizon-wine/tests/horizon_free_range.c" -o "$build/free_range"
"$build/free_range"
python3 "$root/horizon-wine/tests/check_horizon_va_search.py"
python3 "$root/horizon-wine/tests/check_horizon_guest_reserve.py"
python3 "$root/horizon-wine/tests/check_horizon_reservation_transition.py"
python3 "$root/horizon-wine/tests/check_horizon_reservation_bounds.py"
python3 "$root/horizon-wine/tests/check_horizon_thread_tls.py"
"$cc" $flags -pthread -I"$root/horizon-wine/tests/horizon_virtmem" \
    "$root/horizon-wine/tests/horizon_virtmem.c" -o "$build/virtmem"
"$build/virtmem"
python3 "$root/horizon-wine/tests/check_commit_chunks.py"
python3 "$root/horizon-wine/tests/check_waitable_timer.py"
"$cc" $flags -D__WINESRC__ -DWINE_UNIX_LIB -D_WIN64 \
    -I"$root/horizon-wine/tests/xinput-shims" -I"$root/include" \
    "$root/horizon-wine/tests/xinput_nx_pad.c" "$root/horizon-wine/source/input_profile.c" -lm -o "$build/xinput_pad"
"$build/xinput_pad"
"$cc" $flags "$root/horizon-wine/tests/thread_profile.c" -o "$build/thread_profile"
"$build/thread_profile"
"$cc" $flags "$root/horizon-wine/tests/shared_data_place.c" -o "$build/shared_data_place"
"$build/shared_data_place"
python3 "$root/horizon-wine/tests/check_core_affinity.py"
"$cc" $flags "$root/horizon-wine/tests/box64_options.c" -o "$build/box64_options"
"$build/box64_options"
"$cc" $flags "$root/horizon-wine/tests/fex_options.c" -o "$build/fex_options"
"$build/fex_options"
"$cc" $flags "$root/horizon-wine/tests/horizon_pool.c" -o "$build/horizon_pool"
python3 "$root/horizon-wine/tests/check_horizon_backings.py"
"$build/horizon_pool"
python3 "$root/horizon-wine/tests/check_horizon_backings.py"
"$cc" $flags -pthread "$root/horizon-wine/tests/horizon_memfile.c" -o "$build/horizon_memfile"
"$build/horizon_memfile"
python3 "$root/horizon-wine/tests/check_horizon_sections.py"
python3 "$root/horizon-wine/tests/check_horizon_image_info.py"
python3 "$root/horizon-wine/tests/check_memory_status.py"
python3 "$root/horizon-wine/tests/check_user_profile.py"
python3 "$root/horizon-wine/tests/check_lazy_file_views.py"
python3 "$root/horizon-wine/tests/check_horizon_sock_bind.py"
python3 "$root/horizon-wine/tests/check_afd_layouts.py"
python3 "$root/horizon-wine/tests/check_wow64_buffer_flush.py"
python3 "$root/horizon-wine/tests/check_dxvk_payload.py"
python3 "$root/horizon-wine/tests/check_vulkan_wow64_mapping.py"
python3 "$root/horizon-wine/tests/check_d3dkmt_resources.py"
python3 "$root/horizon-wine/tests/check_package_amd64.py"
if [ -f "$root/horizon-wine/vendor/dxvk/src/dxvk/dxvk_device_info.cpp" ]; then
    python3 "$root/horizon-wine/tests/check_dxvk_requirements.py"
fi
