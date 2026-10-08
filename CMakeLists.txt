cmake_minimum_required(VERSION 3.25)

project(MojaveInSkyrim VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# The design sheets are the source of truth: regenerate Sheets.h (after a preflight) when one changes.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(GLOB DESIGN_SHEETS CONFIGURE_DEPENDS design/*.json)
add_custom_command(
	OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/src/generated/Sheets.h ${CMAKE_CURRENT_SOURCE_DIR}/package/MojaveInSkyrim.ini
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/sheets.py gen
	DEPENDS ${DESIGN_SHEETS} tools/sheets.py tools/esm.py tools/bsa.py
	COMMENT "Preflight + generate from design sheets"
	VERBATIM)
add_custom_target(sheets DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/generated/Sheets.h)

# CommonLibSSE-NG (SE + AE; no VR)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/CommonLibSSE-NG CommonLibSSE EXCLUDE_FROM_ALL)
if(NOT COMMAND add_commonlibsse_plugin)
	include(extern/CommonLibSSE-NG/cmake/CommonLibSSE.cmake)
endif()

file(GLOB MIS_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)

add_commonlibsse_plugin(
	MojaveInSkyrim
	NAME "MojaveInSkyrim"
	AUTHOR "Mojave in Skyrim"
	USE_ADDRESS_LIBRARY
	SOURCES ${MIS_SOURCES} src/generated/Sheets.h src/miniaudio_impl.c
)
add_dependencies(MojaveInSkyrim sheets)

# Ultimate ASI Loader loads *.asi from the game folder.
set_target_properties(MojaveInSkyrim PROPERTIES SUFFIX ".asi")

target_include_directories(MojaveInSkyrim PRIVATE src extern/miniaudio)
target_precompile_headers(MojaveInSkyrim PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/PCH.h>)
target_link_libraries(MojaveInSkyrim PRIVATE dbghelp.lib psapi.lib)
target_compile_definitions(MojaveInSkyrim PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN MIS_VERSION="${PROJECT_VERSION}")
if(MSVC)
	# C4062: a switch over a sheet enum must name every value (no default), so a new sheet row
	# that the code doesn't handle is a build error.
	target_compile_options(MojaveInSkyrim PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive- /Zc:preprocessor /utf-8 /MP /we4062>)
	target_link_options(MojaveInSkyrim PRIVATE /DEBUG:FULL)
endif()
set_source_files_properties(src/miniaudio_impl.c PROPERTIES COMPILE_OPTIONS "/W0")
