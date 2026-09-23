cmake_minimum_required(VERSION 3.20)

project(tat C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(tat STATIC
	lib/tat.c

	vendor/vterm/src/encoding.c
	vendor/vterm/src/keyboard.c
	vendor/vterm/src/mouse.c
	vendor/vterm/src/parser.c
	vendor/vterm/src/pen.c
	vendor/vterm/src/screen.c
	vendor/vterm/src/state.c
	vendor/vterm/src/unicode.c
	vendor/vterm/src/vterm.c
)

target_include_directories(tat
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/include

	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/vendor/vterm/include
		${CMAKE_CURRENT_SOURCE_DIR}/vendor/vterm/src
)

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
