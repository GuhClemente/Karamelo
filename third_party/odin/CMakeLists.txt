cmake_minimum_required(VERSION 3.20)
project(odin LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Assemble the .S files with the C++ compiler driver
if(NOT CMAKE_ASM_COMPILER)
	set(CMAKE_ASM_COMPILER "${CMAKE_CXX_COMPILER}")
endif()
enable_language(ASM)
# Windows: odin.rc, the manifest and the icon of odin.exe
if(WIN32)
	enable_language(RC)
	add_compile_definitions(NOMINMAX)
endif()

include(GNUInstallDirs)

option(ODIN_NATIVE "Tune for this machine's CPU (-march=native)" ON)
option(ODIN_LTO "Link-time optimisation (-flto)" ON)
option(ODIN_GLES "Use OpenGL ES for embedded systems" OFF)
set(ODIN_OPT_FLAGS "-O3 -fomit-frame-pointer -fno-stack-protector" CACHE STRING
	"Optimisation flags")
set(ODIN_PGO "" CACHE STRING "Profile-guided build: generate or use")
set_property(CACHE ODIN_PGO PROPERTY STRINGS "" generate use)
set(ODIN_VERSION "" CACHE STRING "Version string (default: git commit)")
find_program(GLSLANG glslangValidator HINTS $ENV{VULKAN_SDK}/bin $ENV{VULKAN_SDK}/macOS/bin)
if(NOT GLSLANG)
	message(FATAL_ERROR "glslangValidator not found: set -DGLSLANG=<path>")
endif()
find_program(AWK NAMES awk gawk mawk REQUIRED)

separate_arguments(OPT_FLAGS UNIX_COMMAND "${ODIN_OPT_FLAGS}")
if(ODIN_LTO)
	list(APPEND OPT_FLAGS -flto)
endif()
if(ODIN_NATIVE)
	if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
		list(APPEND OPT_FLAGS -mcpu=native)
	else()
		list(APPEND OPT_FLAGS -march=native)
	endif()
endif()
# No implicit FMA: identical results on every CPU
list(APPEND OPT_FLAGS -ffp-contract=off)
# Each function starts a line of 64 bytes: its loops keep their place in the
# lines when other code moves (a loop moved across a line changed the time of
# the sound by up to 10 %), so that a build is as fast as the code it has
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
	list(APPEND OPT_FLAGS -falign-functions=64)
endif()
if(ODIN_PGO STREQUAL "generate")
	list(APPEND OPT_FLAGS -fprofile-generate -fprofile-update=single)
elseif(ODIN_PGO STREQUAL "use")
	list(APPEND OPT_FLAGS -fprofile-use -fprofile-correction -Wno-missing-profile)
elseif(NOT ODIN_PGO STREQUAL "")
	message(FATAL_ERROR "ODIN_PGO is generate, use or empty, not '${ODIN_PGO}'")
endif()

# Libraries from pkg-config, else built from 3rdparty/ (ODIN_BUNDLED_LIBS).
# Raw pkg-config flags: pkg_check_modules would put -I/usr/include first.
set(ODIN_BUNDLED_LIBS AUTO CACHE STRING "Use 3rdparty/ libraries: AUTO, ON or OFF")
set_property(CACHE ODIN_BUNDLED_LIBS PROPERTY STRINGS AUTO ON OFF)
if(NOT ODIN_BUNDLED_LIBS MATCHES "^(AUTO|ON|OFF)$")
	message(FATAL_ERROR "ODIN_BUNDLED_LIBS is AUTO, ON or OFF, not '${ODIN_BUNDLED_LIBS}'")
endif()
# Let set() override the subprojects' option()
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

# INTERFACE target odin_<name> from pkg-config; sets odin_<name>_FOUND
function(odin_system_lib name)
	set(odin_${name}_FOUND FALSE PARENT_SCOPE)
	if(NOT PKG_CONFIG_FOUND)
		return()
	endif()
	execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE} --exists ${ARGN} RESULT_VARIABLE result)
	if(NOT result EQUAL 0)
		return()
	endif()
	add_library(odin_${name} INTERFACE)
	foreach(kind cflags libs)
		execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE} --${kind} ${ARGN}
			OUTPUT_VARIABLE out OUTPUT_STRIP_TRAILING_WHITESPACE)
		separate_arguments(out UNIX_COMMAND "${out}")
		if(kind STREQUAL "cflags")
			target_compile_options(odin_${name} INTERFACE ${out})
		else()
			target_link_libraries(odin_${name} INTERFACE ${out})
		endif()
	endforeach()
	message(STATUS "${ARGN}: from the system")
	set(odin_${name}_FOUND TRUE PARENT_SCOPE)
endfunction()
function(odin_system_only name)
	odin_system_lib(${name} ${ARGN})
	if(NOT odin_${name}_FOUND)
		message(FATAL_ERROR "pkg-config: ${ARGN} not found")
	endif()
endfunction()
# odin_<name> from the system, else from 3rdparty/<folder>
function(odin_library name folder target)
	if(NOT ODIN_BUNDLED_LIBS STREQUAL "ON")
		odin_system_lib(${name} ${ARGN})
		if(odin_${name}_FOUND)
			return()
		endif()
	endif()
	if(ODIN_BUNDLED_LIBS STREQUAL "OFF")
		message(FATAL_ERROR "pkg-config: ${ARGN} not found (ODIN_BUNDLED_LIBS=OFF)")
	endif()
	if(NOT EXISTS ${CMAKE_SOURCE_DIR}/3rdparty/${folder}/CMakeLists.txt)
		message(FATAL_ERROR "${ARGN} not found: git submodule update --init")
	endif()
	add_subdirectory(3rdparty/${folder} EXCLUDE_FROM_ALL)
	get_target_property(real ${target} ALIASED_TARGET)
	if(NOT real)
		set(real ${target})
	endif()
	target_compile_options(${real} PRIVATE -O2)
	add_library(odin_${name} INTERFACE)
	target_link_libraries(odin_${name} INTERFACE ${target})
	message(STATUS "${ARGN}: from 3rdparty/${folder}")
endfunction()

if(ODIN_GLES AND NOT (UNIX AND NOT APPLE))
	message(FATAL_ERROR "ODIN_GLES is for Linux")
endif()
find_package(PkgConfig QUIET)

set(SDL_SHARED OFF)
set(SDL_STATIC ON)
set(SDL_TEST_LIBRARY OFF)
set(SDL_TESTS OFF)
set(SDL_EXAMPLES OFF)
set(SDL_INSTALL OFF)
odin_library(sdl SDL SDL3::SDL3-static sdl3)

set(JANSSON_BUILD_SHARED_LIBS OFF)
set(JANSSON_EXAMPLES OFF)
set(JANSSON_WITHOUT_TESTS ON)
set(JANSSON_BUILD_DOCS OFF)
set(JANSSON_INSTALL OFF)
odin_library(jansson jansson jansson::jansson jansson)

if(NOT ODIN_GLES)
	set(glew-cmake_BUILD_SHARED OFF)
	set(glew-cmake_BUILD_STATIC ON)
	set(ONLY_LIBS ON)
	set(PKG_CONFIG_REPRESENTATIVE_TARGET OFF)
	odin_library(glew glew libglew_static glew)
endif()

include(CheckIncludeFileCXX)
if(NOT ODIN_BUNDLED_LIBS STREQUAL "ON")
	check_include_file_cxx(vulkan/vulkan_core.h ODIN_SYSTEM_VULKAN_HEADERS)
	if(NOT ODIN_SYSTEM_VULKAN_HEADERS AND PKG_CONFIG_FOUND)
		execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE} --cflags-only-I vulkan
			OUTPUT_VARIABLE vulkan_cflags RESULT_VARIABLE result OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
		if(result EQUAL 0 AND NOT vulkan_cflags STREQUAL "")
			separate_arguments(vulkan_cflags UNIX_COMMAND "${vulkan_cflags}")
			set(ODIN_SYSTEM_VULKAN_HEADERS TRUE)
		endif()
	endif()
endif()
add_library(odin_vulkan_headers INTERFACE)
# VK_KHR_portability_subset (MoltenVK) is a beta extension; each file sees
# the same headers
target_compile_definitions(odin_vulkan_headers INTERFACE VK_ENABLE_BETA_EXTENSIONS)
if(ODIN_SYSTEM_VULKAN_HEADERS)
	target_compile_options(odin_vulkan_headers INTERFACE ${vulkan_cflags})
	message(STATUS "vulkan headers: from the system")
elseif(ODIN_BUNDLED_LIBS STREQUAL "OFF")
	message(FATAL_ERROR "Vulkan headers not found (ODIN_BUNDLED_LIBS=OFF)")
elseif(EXISTS ${CMAKE_SOURCE_DIR}/3rdparty/Vulkan-Headers/include/vulkan/vulkan_core.h)
	target_include_directories(odin_vulkan_headers SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/3rdparty/Vulkan-Headers/include)
	message(STATUS "vulkan headers: from 3rdparty/Vulkan-Headers")
else()
	message(FATAL_ERROR "Vulkan headers not found: git submodule update --init")
endif()

add_library(odin_deps INTERFACE)
target_link_libraries(odin_deps INTERFACE odin_jansson odin_vulkan_headers)
if(APPLE)
	target_link_libraries(odin_deps INTERFACE odin_glew "-framework OpenGL")
	set(HOST_WARN_FLAGS -Wno-sign-conversion)
elseif(UNIX AND ODIN_GLES)
	odin_system_only(gles glesv2)
	odin_system_only(x11 x11)
	target_link_libraries(odin_deps INTERFACE odin_gles odin_x11)
elseif(UNIX)
	odin_system_only(gl gl)
	odin_system_only(x11 x11)
	target_link_libraries(odin_deps INTERFACE odin_gl odin_glew odin_x11)
else()
	target_link_libraries(odin_deps INTERFACE odin_glew)
endif()

set(ODIN_SOURCES
	getopt.c host/app/main.cpp host/app/cpu_check.cpp host/app/options.cpp host/app/atvtrack_play.cpp
	machine/boards/atvtrack/atvtrack.cpp machine/boards/atvtrack/atvtrack_state.cpp machine/boards/atvtrack/atvtrack_fpga.cpp
	machine/chips/pmx1/pmx1.cpp machine/chips/pmx1/pmx1_core.cpp machine/chips/pmx1/pmx1_scene.cpp
	machine/chips/pmx1/pmx1_host.cpp machine/chips/pmx1/pmx1_ta.cpp
	host/ui/menu_session.cpp host/ui/menu_session_input.cpp
	host/ui/menu_session_state.cpp host/app/session.cpp
	host/app/library.cpp host/video/display.cpp
	host/video/display_bezel.cpp host/video/display_window.cpp
	host/gfx/host_graphics_gl.cpp host/gfx/host_graphics_vk.cpp
	host/gfx/vk_common.cpp host/renderer/hikaru/vk/drawing_vk.cpp
	host/renderer/hikaru/vk/pictures_vk.cpp
	host/renderer/hikaru/vk/pmx_vk.cpp
	host/renderer/hikaru/vk/scene_vk.cpp
	host/renderer/hikaru/vk/translucent_vk.cpp
	host/renderer/hikaru/gl/drawing_gl.cpp
	host/renderer/hikaru/gl/pmx_gl.cpp
	host/renderer/hikaru/gl/pictures_gl.cpp
	host/renderer/hikaru/gl/programs_gl.cpp
	host/renderer/hikaru/gl/scene_gl.cpp
	host/renderer/hikaru/gl/translucent_gl.cpp
	host/video/x11_window.cpp host/ui/menu.cpp host/ui/menu_input.cpp
	host/ui/menu_pages.cpp host/ui/menu_library.cpp
	host/ui/menu_bar.cpp host/ui/cabinet_view.cpp host/ui/sound_parts.cpp host/ui/sound_view.cpp
	host/ui/menu_kit.cpp host/ui/menu_nav.cpp
	host/ui/system_fonts.cpp host/app/rom_files.cpp
	host/ui/folder_dialog.cpp host/ui/pad_view.cpp
	host/ui/pad_drawings.cpp host/ui/pad_drawings_al2009man.cpp
	host/ui/gun_drawings.cpp
	host/input/keyboard.cpp host/app/log_file.cpp
	host/app/version.cpp host/app/hotkeys.cpp host/input/controls.cpp
	host/input/pointers.cpp host/input/wheel_force.cpp host/input/gun_serial.cpp
	host/input/evdev.cpp host/input/rawinput.cpp
	host/input/outputs_network.cpp host/input/sockets.cpp
	common/cabinet_settings.cpp host/input/control_panel.cpp
	host/input/bindings.cpp host/input/wheels.cpp host/app/settings.cpp host/app/games.cpp
	host/input/roles.cpp machine/core/state_file.cpp
	host/app/user_files.cpp machine/core/worker.cpp
	machine/boards/hikaru/hikaru.cpp
	machine/boards/hikaru/hikaru_netbd.cpp
	machine/boards/hikaru/hikaru_bus.cpp
	machine/boards/hikaru/hikaru_state.cpp machine/core/scheduler.cpp
	machine/chips/memctl.cpp machine/boards/hikaru/rom_board.cpp
	machine/chips/eeprom_93c46.cpp machine/chips/x76f100.cpp
	machine/chips/crypt_315_5881.cpp machine/chips/actel_m1.cpp
	machine/cpu/sh4/sh4.cpp machine/cpu/sh4/sh4_fsca.cpp
	machine/core/host_code.cpp machine/core/host_code_arm64.cpp
	machine/cpu/sh4/sh4_host.cpp machine/cpu/sh4/sh4_host_arm64.cpp
	machine/cpu/sh4/sh4_translate.cpp machine/cpu/sh4/sh4_ops.cpp
	machine/cpu/sh4/sh4_onchip.cpp machine/cpu/sh4/sh4_clock.cpp
	machine/boards/hikaru/mie_board.cpp machine/chips/mie.cpp
	machine/cpu/z80/z80.cpp machine/cpu/z80/z80_ops.cpp
	machine/cabinet/jvs.cpp
	machine/cabinet/io_837_13551_imitation.cpp
	machine/cabinet/io_837_13741_imitation.cpp
	machine/cabinet/io_837_13551.cpp machine/chips/tmp90ph44.cpp
	machine/cabinet/io_837_13741.cpp machine/chips/d71054.cpp
	machine/cabinet/drive_838_13366.cpp
	machine/cabinet/netbd_837_13404.cpp
	machine/cabinet/netbd_ring.cpp machine/cpu/m68000/m68000.cpp
	machine/cpu/m68000/m68000_ops.cpp machine/cpu/tlcs90/tlcs90.cpp
	machine/chips/card_reader.cpp machine/chips/von4_card.cpp
	machine/boards/hikaru/sound_board.cpp machine/chips/aica/aica.cpp
	machine/chips/aica/aica_dsp.cpp host/audio/audio_out.cpp
	machine/chips/aica/aica_voice.cpp common/wav_file.cpp
	machine/cpu/arm7/arm7.cpp machine/cpu/arm7/arm7_ops.cpp
	machine/cpu/arm7/arm7_host.cpp
	machine/cpu/arm7/arm7_host_arm64.cpp
	machine/cpu/arm7/arm7_translate.cpp
	machine/boards/hikaru/hikaru_gpu.cpp
	machine/chips/antarctic/antarctic.cpp
	machine/chips/antarctic/antarctic_ops.cpp
	machine/chips/antarctic/antarctic_mesh.cpp
	machine/chips/antarctic/antarctic_disasm.cpp
	machine/chips/antarctic/antarctic_list.cpp
	machine/chips/antarctic/antarctic_io.cpp
	machine/chips/eurasia/eurasia.cpp
	host/renderer/hikaru/renderer.cpp
	host/renderer/hikaru/renderer_scene.cpp
	host/renderer/hikaru/renderer_translucent.cpp
	host/renderer/hikaru/renderer_programs.cpp
	host/gfx/program_cache.cpp
	host/renderer/hikaru/renderer_textures.cpp
	host/renderer/hikaru/renderer_times.cpp
	host/renderer/hikaru/renderer_layers.cpp
	host/renderer/hikaru/renderer_state.cpp
	host/renderer/hikaru/renderer_debug.cpp host/gfx/gl_common.cpp
	host/video/picture_filter_gl.cpp host/video/picture_filter_vk.cpp
	host/video/picture_preset.cpp host/ui/loading_screen.cpp)
list(TRANSFORM ODIN_SOURCES PREPEND src/odin/)

# Third-party code, built without Odin's warnings
set(IMGUI_SOURCES imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp
	imgui_impl_sdl3.cpp imgui_impl_opengl3.cpp imgui_impl_vulkan.cpp)
list(TRANSFORM IMGUI_SOURCES PREPEND src/imgui/)

set(ARCHIVE_SOURCES 7zAlloc.c 7zArcIn.c 7zBuf.c 7zBuf2.c 7zCrc.c
	7zCrcOpt.c 7zDec.c 7zFile.c 7zStream.c Bcj2.c
	Bra.c Bra86.c BraIA64.c CpuArch.c Delta.c
	Lzma2Dec.c LzmaDec.c Ppmd7.c Ppmd7Dec.c)
list(TRANSFORM ARCHIVE_SOURCES PREPEND src/lzma/)
list(PREPEND ARCHIVE_SOURCES src/miniz/miniz.c src/odin/host/app/zip_crc.cpp)

# ImGui uses Odin's Vulkan function pointers
set(VULKAN_FLAGS IMGUI_IMPL_VULKAN_NO_PROTOTYPES)

add_library(odin_imgui OBJECT ${IMGUI_SOURCES})
set_target_properties(odin_imgui PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(odin_imgui PRIVATE src/imgui)
target_compile_definitions(odin_imgui PRIVATE ${VULKAN_FLAGS} $<$<BOOL:${ODIN_GLES}>:IMGUI_IMPL_OPENGL_ES3>)
target_compile_options(odin_imgui PRIVATE ${OPT_FLAGS})
target_link_libraries(odin_imgui PRIVATE odin_sdl odin_vulkan_headers)

add_library(odin_archive OBJECT ${ARCHIVE_SOURCES})
target_compile_options(odin_archive PRIVATE ${OPT_FLAGS})
# miniz's CRC-32 comes from zip_crc.cpp
set_source_files_properties(src/miniz/miniz.c PROPERTIES COMPILE_DEFINITIONS USE_EXTERNAL_MZCRC)
set_source_files_properties(src/odin/host/app/zip_crc.cpp PROPERTIES
	COMPILE_OPTIONS "-isystem;${CMAKE_SOURCE_DIR}/src/miniz")

# Vulkan shaders to SPIR-V
set(GLSL ${CMAKE_SOURCE_DIR}/src/odin/host/gfx/glsl)
set(SPIRV_DIR ${CMAKE_BINARY_DIR}/spirv)
file(MAKE_DIRECTORY ${SPIRV_DIR}/glsl)
set(SPIRV)
foreach(shader whole_picture.vert blend.frag compose.vert compose.frag present.frag
		mesh.vert mesh.geom mesh.frag sort.frag image.vert image.frag pmx.vert pmx.geom pmx.frag pmx_sort.frag
		whole_window.vert
		blur.frag show.frag average.frag sides.frag)
	string(REGEX REPLACE ".*\\." "" stage ${shader})
	set(out ${SPIRV_DIR}/glsl/${shader}.spv)
	if(shader STREQUAL "sort.frag")
		set(script "{ echo '#version 450'; echo '#define LISTS_SET 0'; cat \"$2\" \"$3\"; } | \"$0\" -V --stdin -S frag -o \"$1\"")
		set(inputs ${GLSL}/fragment_lists.glsl ${GLSL}/${shader})
	else()
		set(script "{ echo '#version 450'; cat \"$2\"; } | \"$0\" -V --stdin -S ${stage} -o \"$1\"")
		set(inputs ${GLSL}/${shader})
	endif()
	add_custom_command(OUTPUT ${out}
		COMMAND sh -c "${script}" ${GLSLANG} ${out} ${inputs}
		DEPENDS ${inputs} VERBATIM)
	list(APPEND SPIRV ${out})
endforeach()
set(out ${SPIRV_DIR}/glsl/mesh_collect.frag.spv)
add_custom_command(OUTPUT ${out}
	COMMAND sh -c "{ echo '#version 450'; echo '#define COLLECT 1'; echo '#define LISTS_SET 3'; cat \"$2\" \"$3\"; } | \"$0\" -V --stdin -S frag -o \"$1\""
		${GLSLANG} ${out} ${GLSL}/fragment_lists.glsl ${GLSL}/mesh.frag
	DEPENDS ${GLSL}/fragment_lists.glsl ${GLSL}/mesh.frag VERBATIM)
list(APPEND SPIRV ${out})
set(out ${SPIRV_DIR}/glsl/pmx_native.frag.spv)
add_custom_command(OUTPUT ${out}
	COMMAND sh -c "{ echo '#version 450'; echo '#define NATIVE 1'; cat \"$2\"; } | \"$0\" -V --stdin -S frag -o \"$1\""
		${GLSLANG} ${out} ${GLSL}/pmx.frag
	DEPENDS ${GLSL}/pmx.frag VERBATIM)
list(APPEND SPIRV ${out})
foreach(variant list native_list)
	set(out ${SPIRV_DIR}/glsl/pmx_${variant}.frag.spv)
	if(variant STREQUAL "list")
		set(defines "echo '#define LIST 1'")
	else()
		set(defines "echo '#define NATIVE 1'; echo '#define LIST 1'")
	endif()
	add_custom_command(OUTPUT ${out}
		COMMAND sh -c "{ echo '#version 450'; ${defines}; cat \"$2\"; } | \"$0\" -V --stdin -S frag -o \"$1\""
			${GLSLANG} ${out} ${GLSL}/pmx.frag
		DEPENDS ${GLSL}/pmx.frag VERBATIM)
	list(APPEND SPIRV ${out})
endforeach()
set(out ${SPIRV_DIR}/glsl/mesh_families.frag.spv)
add_custom_command(OUTPUT ${out}
	COMMAND sh -c "{ echo '#version 450'; echo '#define FAMILIES 1'; cat \"$2\"; } | \"$0\" -V --stdin -S frag -o \"$1\""
		${GLSLANG} ${out} ${GLSL}/mesh.frag
	DEPENDS ${GLSL}/mesh.frag VERBATIM)
list(APPEND SPIRV ${out})

# Files embedded with .incbin
set(UI ${CMAKE_SOURCE_DIR}/src/odin/host/ui)
set(GFX ${CMAKE_SOURCE_DIR}/src/odin/host/gfx)
file(GLOB PICTURES CONFIGURE_DEPENDS ${UI}/pictures/*.png)
file(GLOB GLSL_FILES CONFIGURE_DEPENDS ${GLSL}/*)
list(FILTER GLSL_FILES EXCLUDE REGEX "\\.spv$")
add_library(odin_embedded OBJECT
	${UI}/menu_pictures.S ${UI}/menu_fonts.S ${GFX}/glsl_texts.S ${GFX}/spirv.S ${SPIRV})
set_source_files_properties(${UI}/menu_pictures.S PROPERTIES
	COMPILE_OPTIONS "-Wa,-I,${UI}" OBJECT_DEPENDS "${PICTURES}")
set_source_files_properties(${UI}/menu_fonts.S PROPERTIES
	COMPILE_OPTIONS "-Wa,-I,${UI}" OBJECT_DEPENDS "${UI}/fonts/cozette.ttf")
set_source_files_properties(${GFX}/glsl_texts.S PROPERTIES
	COMPILE_OPTIONS "-Wa,-I,${GFX}" OBJECT_DEPENDS "${GLSL_FILES}")
set_source_files_properties(${GFX}/spirv.S PROPERTIES
	COMPILE_OPTIONS "-Wa,-I,${SPIRV_DIR}" OBJECT_DEPENDS "${SPIRV}")

# Version from git, only when the source folder is the repository top;
# reconfigures when HEAD or the index changes
set(version "${ODIN_VERSION}")
if(version STREQUAL "")
	find_package(Git QUIET)
	if(GIT_FOUND)
		execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --show-toplevel --short HEAD
			WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
			OUTPUT_VARIABLE git_out RESULT_VARIABLE git_result ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
		if(git_result EQUAL 0)
			string(REPLACE "\n" ";" git_out "${git_out}")
			list(GET git_out 0 git_top)
			list(GET git_out 1 git_head)
			file(REAL_PATH "${git_top}" git_top)
			file(REAL_PATH "${CMAKE_SOURCE_DIR}" source_top)
			if(git_top STREQUAL source_top)
				set(version "${git_head}")
				execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --path-format=absolute
						--git-path HEAD --git-path index --symbolic-full-name HEAD
					WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
					OUTPUT_VARIABLE git_paths ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
				string(REPLACE "\n" ";" git_paths "${git_paths}")
				foreach(path ${git_paths})
					if(path MATCHES "^refs/")
						execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --path-format=absolute --git-path ${path}
							WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
							OUTPUT_VARIABLE path ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
					endif()
					if(EXISTS "${path}")
						set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
					endif()
				endforeach()
			endif()
		endif()
	endif()
endif()
message(STATUS "Odin version: '${version}'")
set_source_files_properties(src/odin/host/app/version.cpp PROPERTIES
	COMPILE_DEFINITIONS "ODIN_VERSION=\"${version}\"")

set_source_files_properties(src/odin/host/app/rom_files.cpp PROPERTIES
	COMPILE_OPTIONS "-isystem;${CMAKE_SOURCE_DIR}/src/miniz;-isystem;${CMAKE_SOURCE_DIR}/src/lzma")
set_source_files_properties(src/odin/host/gfx/host_graphics_vk.cpp PROPERTIES
	COMPILE_DEFINITIONS "${VULKAN_FLAGS}")

add_executable(odin ${ODIN_SOURCES}
	$<TARGET_OBJECTS:odin_imgui> $<TARGET_OBJECTS:odin_archive> $<TARGET_OBJECTS:odin_embedded>)
set_target_properties(odin PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_include_directories(odin PRIVATE src)
target_compile_definitions(odin PRIVATE $<$<BOOL:${ODIN_GLES}>:ODIN_GLES>)
target_compile_options(odin PRIVATE -pthread -Wall -Wextra -Wpedantic -Wconversion -Wshadow
	${HOST_WARN_FLAGS} ${OPT_FLAGS})
target_link_options(odin PRIVATE -pthread ${OPT_FLAGS})
if(UNIX)
	target_link_libraries(odin PRIVATE m)
endif()
if(WIN32)
	target_link_libraries(odin PRIVATE ws2_32 hid setupapi)
	target_link_options(odin PRIVATE -static)
	# The manifest (UTF-8) and icon of odin.exe, found beside odin.rc, and
	# its version: the numbers of a tag v<a>.<b>.<c>, 0.0.0.0 for a commit
	set(numbers 0 0 0)
	if(version MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)")
		set(numbers ${CMAKE_MATCH_1} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3})
	endif()
	list(JOIN numbers "," version_commas)
	list(JOIN numbers "." version_dots)
	file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/odin_version.h CONTENT [[
#define ODIN_VERSION_NUMBERS @version_commas@,0
#define ODIN_VERSION_DOTS "@version_dots@.0"
#define ODIN_VERSION_TEXT "@version@"
]] @ONLY)
	target_sources(odin PRIVATE src/odin/host/app/odin.rc)
	set_property(SOURCE src/odin/host/app/odin.rc APPEND PROPERTY INCLUDE_DIRECTORIES
		${CMAKE_SOURCE_DIR}/src/odin/host/app ${CMAKE_BINARY_DIR})
	set_property(SOURCE src/odin/host/app/odin.rc APPEND PROPERTY OBJECT_DEPENDS
		${CMAKE_BINARY_DIR}/odin_version.h)
endif()
target_link_libraries(odin PRIVATE odin_deps odin_sdl)

# Data in build/share/odin, laid out as installed
set(DATA ${CMAKE_BINARY_DIR}/share/odin)
file(GLOB DATA_FILES CONFIGURE_DEPENDS RELATIVE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/shaders/*.txt)
file(GLOB_RECURSE FILTER_FILES CONFIGURE_DEPENDS RELATIVE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/filters/*)
list(APPEND DATA_FILES odin-games.json ${FILTER_FILES})
list(FILTER DATA_FILES EXCLUDE REGEX "\\.spv$")
foreach(file ${DATA_FILES})
	configure_file(${file} ${DATA}/${file} COPYONLY)
endforeach()

# Picture filters to SPIR-V
set(FILTER_AWK ${CMAKE_SOURCE_DIR}/src/odin/host/video/filter_spirv.awk)
file(GLOB FILTER_GLSL CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/filters/*/shaders/*.glsl)
set(FILTER_SPIRV)
foreach(glsl ${FILTER_GLSL})
	file(RELATIVE_PATH rel ${CMAKE_SOURCE_DIR} ${glsl})
	get_filename_component(folder ${glsl} DIRECTORY)
	add_custom_command(OUTPUT ${DATA}/${rel}.vert.spv
		COMMAND sh -c "\"$0\" -v part=VERTEX -f \"$1\" \"$2\"/*.glsl \"$3\" | \"$4\" -V -R --amb --aml --sub vert 8 --stdin -S vert -o \"$5\""
			${AWK} ${FILTER_AWK} ${folder} ${glsl} ${GLSLANG} ${DATA}/${rel}.vert.spv
		DEPENDS ${FILTER_GLSL} ${FILTER_AWK} VERBATIM)
	add_custom_command(OUTPUT ${DATA}/${rel}.frag.spv
		COMMAND sh -c "\"$0\" -v part=FRAGMENT -f \"$1\" \"$2\"/*.glsl \"$3\" | \"$4\" -V -R --amb --aml --stdin -S frag -o \"$5\""
			${AWK} ${FILTER_AWK} ${folder} ${glsl} ${GLSLANG} ${DATA}/${rel}.frag.spv
		DEPENDS ${FILTER_GLSL} ${FILTER_AWK} VERBATIM)
	list(APPEND FILTER_SPIRV ${DATA}/${rel}.vert.spv ${DATA}/${rel}.frag.spv)
endforeach()
add_custom_target(spirv ALL DEPENDS ${FILTER_SPIRV})

install(TARGETS odin RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ${DATA} DESTINATION ${CMAKE_INSTALL_DATADIR})
if(UNIX AND NOT APPLE)
	install(FILES dist/odin.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
	install(FILES dist/odin.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()

add_custom_target(check-layers
	COMMAND sh ${CMAKE_SOURCE_DIR}/tools/check-layers.sh
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} VERBATIM)

# ===========================================================================
# Libretro Core: cores/hikaru.dll
# ===========================================================================
set(HIKARU_LIBRETRO_SOURCES
    ${CMAKE_SOURCE_DIR}/../odin_libretro/libretro.cpp
    src/odin/machine/boards/hikaru/hikaru.cpp
    src/odin/machine/boards/hikaru/hikaru_netbd.cpp
    src/odin/machine/boards/hikaru/hikaru_bus.cpp
    src/odin/machine/boards/hikaru/hikaru_state.cpp
    src/odin/machine/core/scheduler.cpp
    src/odin/machine/chips/memctl.cpp
    src/odin/machine/boards/hikaru/rom_board.cpp
    src/odin/machine/chips/eeprom_93c46.cpp
    src/odin/machine/chips/x76f100.cpp
    src/odin/machine/chips/crypt_315_5881.cpp
    src/odin/machine/chips/actel_m1.cpp
    src/odin/machine/cpu/sh4/sh4.cpp
    src/odin/machine/cpu/sh4/sh4_fsca.cpp
    src/odin/machine/core/host_code.cpp
    src/odin/machine/core/host_code_arm64.cpp
    src/odin/machine/cpu/sh4/sh4_host.cpp
    src/odin/machine/cpu/sh4/sh4_host_arm64.cpp
    src/odin/machine/cpu/sh4/sh4_translate.cpp
    src/odin/machine/cpu/sh4/sh4_ops.cpp
    src/odin/machine/cpu/sh4/sh4_onchip.cpp
    src/odin/machine/cpu/sh4/sh4_clock.cpp
    src/odin/machine/boards/hikaru/mie_board.cpp
    src/odin/machine/chips/mie.cpp
    src/odin/machine/cpu/z80/z80.cpp
    src/odin/machine/cpu/z80/z80_ops.cpp
    src/odin/machine/cabinet/jvs.cpp
    src/odin/machine/cabinet/io_837_13551_imitation.cpp
    src/odin/machine/cabinet/io_837_13741_imitation.cpp
    src/odin/machine/cabinet/io_837_13551.cpp
    src/odin/machine/chips/tmp90ph44.cpp
    src/odin/machine/cabinet/io_837_13741.cpp
    src/odin/machine/chips/d71054.cpp
    src/odin/machine/cabinet/drive_838_13366.cpp
    src/odin/machine/cabinet/netbd_837_13404.cpp
    src/odin/machine/cabinet/netbd_ring.cpp
    src/odin/machine/cpu/m68000/m68000.cpp
    src/odin/machine/cpu/m68000/m68000_ops.cpp
    src/odin/machine/cpu/tlcs90/tlcs90.cpp
    src/odin/machine/chips/card_reader.cpp
    src/odin/machine/chips/von4_card.cpp
    src/odin/machine/boards/hikaru/sound_board.cpp
    src/odin/machine/chips/aica/aica.cpp
    src/odin/machine/chips/aica/aica_dsp.cpp
    src/odin/machine/chips/aica/aica_voice.cpp
    src/odin/common/wav_file.cpp
    src/odin/machine/cpu/arm7/arm7.cpp
    src/odin/machine/cpu/arm7/arm7_ops.cpp
    src/odin/machine/cpu/arm7/arm7_host.cpp
    src/odin/machine/cpu/arm7/arm7_host_arm64.cpp
    src/odin/machine/cpu/arm7/arm7_translate.cpp
    src/odin/machine/boards/hikaru/hikaru_gpu.cpp
    src/odin/machine/chips/antarctic/antarctic.cpp
    src/odin/machine/chips/antarctic/antarctic_ops.cpp
    src/odin/machine/chips/antarctic/antarctic_mesh.cpp
    src/odin/machine/chips/antarctic/antarctic_disasm.cpp
    src/odin/machine/chips/antarctic/antarctic_list.cpp
    src/odin/machine/chips/antarctic/antarctic_io.cpp
    src/odin/machine/chips/eurasia/eurasia.cpp
    src/odin/host/renderer/hikaru/renderer.cpp
    src/odin/host/renderer/hikaru/renderer_scene.cpp
    src/odin/host/renderer/hikaru/renderer_translucent.cpp
    src/odin/host/renderer/hikaru/renderer_programs.cpp
    src/odin/host/gfx/program_cache.cpp
    src/odin/host/renderer/hikaru/renderer_textures.cpp
    src/odin/host/renderer/hikaru/renderer_times.cpp
    src/odin/host/renderer/hikaru/renderer_layers.cpp
    src/odin/host/renderer/hikaru/renderer_state.cpp
    src/odin/host/renderer/hikaru/renderer_debug.cpp
    src/odin/host/gfx/gl_common.cpp
    src/odin/host/video/picture_filter_gl.cpp
    src/odin/host/video/picture_preset.cpp
    src/odin/host/renderer/hikaru/gl/drawing_gl.cpp
    src/odin/host/renderer/hikaru/gl/pmx_gl.cpp
    src/odin/host/renderer/hikaru/gl/pictures_gl.cpp
    src/odin/host/renderer/hikaru/gl/programs_gl.cpp
    src/odin/host/renderer/hikaru/gl/scene_gl.cpp
    src/odin/host/renderer/hikaru/gl/translucent_gl.cpp
    src/odin/machine/core/state_file.cpp
    src/odin/machine/core/worker.cpp
    src/odin/common/cabinet_settings.cpp
    src/odin/host/app/games.cpp
    src/odin/host/app/user_files.cpp
    src/odin/host/app/rom_files.cpp
    src/odin/host/app/log_file.cpp
    src/odin/host/app/version.cpp
    src/odin/host/ui/loading_screen.cpp
    src/odin/host/input/roles.cpp
    src/odin/host/input/sockets.cpp
)

add_library(hikaru SHARED
    ${HIKARU_LIBRETRO_SOURCES}
    $<TARGET_OBJECTS:odin_archive>
    $<TARGET_OBJECTS:odin_embedded>
)
set_target_properties(hikaru PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    PREFIX ""
    OUTPUT_NAME "hikaru"
)
target_include_directories(hikaru PRIVATE src ${CMAKE_SOURCE_DIR}/../../include)
target_compile_definitions(hikaru PRIVATE NOMINMAX)
target_compile_options(hikaru PRIVATE -pthread -Wall -Wextra -Wpedantic -Wconversion -Wshadow ${HOST_WARN_FLAGS} ${OPT_FLAGS})
target_link_options(hikaru PRIVATE -pthread ${OPT_FLAGS})
if(WIN32)
    target_link_libraries(hikaru PRIVATE odin_deps odin_sdl ws2_32 opengl32)
endif()
