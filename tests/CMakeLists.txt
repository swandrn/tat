file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

foreach(TEST_SOURCE ${TEST_SOURCES})
	get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)

	add_executable(${TEST_NAME}
		${TEST_SOURCE}
	)

	target_link_libraries(${TEST_NAME}
		PRIVATE tat
	)

	add_test(
		NAME ${TEST_NAME}
		COMMAND ${TEST_NAME}
	)

	set_tests_properties(${TEST_NAME} PROPERTIES
		TIMEOUT 5
	)
endforeach()
