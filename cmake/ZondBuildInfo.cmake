function(create_build_info NAME)

	# Set build platform; to be written to BuildInfo.h
	set(ZOND_BUILD_OS "${CMAKE_SYSTEM_NAME}")

	if (CMAKE_COMPILER_IS_MINGW)
		set(ZOND_BUILD_COMPILER "mingw")
	elseif (CMAKE_COMPILER_IS_MSYS)
		set(ZOND_BUILD_COMPILER "msys")
	elseif (CMAKE_COMPILER_IS_GNUCXX)
		set(ZOND_BUILD_COMPILER "g++")
	elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
		set(ZOND_BUILD_COMPILER "msvc")
	elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
		set(ZOND_BUILD_COMPILER "clang")
	elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
		set(ZOND_BUILD_COMPILER "appleclang")
	else ()
		set(ZOND_BUILD_COMPILER "unknown")
	endif ()

	set(ZOND_BUILD_PLATFORM "${ZOND_BUILD_OS}.${ZOND_BUILD_COMPILER}")

	#cmake build type may be not speCified when using msvc
	if (CMAKE_BUILD_TYPE)
		set(_cmake_build_type ${CMAKE_BUILD_TYPE})
	else()
		set(_cmake_build_type "${CMAKE_CFG_INTDIR}")
	endif()

	# Generate header file containing useful build information
	add_custom_target(${NAME}_BuildInfo.h ALL
		WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
		COMMAND ${CMAKE_COMMAND} -DZOND_SOURCE_DIR=${PROJECT_SOURCE_DIR} -DZOND_BUILDINFO_IN=${ZOND_CMAKE_DIR}/templates/BuildInfo.h.in -DZOND_DST_DIR=${PROJECT_BINARY_DIR}/include/${PROJECT_NAME} -DZOND_CMAKE_DIR=${ZOND_CMAKE_DIR}
		-DZOND_BUILD_TYPE="${_cmake_build_type}"
		-DZOND_BUILD_OS="${ZOND_BUILD_OS}"
		-DZOND_BUILD_COMPILER="${ZOND_BUILD_COMPILER}"
		-DZOND_BUILD_PLATFORM="${ZOND_BUILD_PLATFORM}"
		-DPROJECT_VERSION="${PROJECT_VERSION}"
		-DPROJECT_VERSION_MAJOR="${PROJECT_VERSION_MAJOR}"
		-DPROJECT_VERSION_MINOR="${PROJECT_VERSION_MINOR}"
		-DPROJECT_VERSION_PATCH="${PROJECT_VERSION_PATCH}"
		-P "${ZOND_SCRIPTS_DIR}/buildinfo.cmake"
		)
	include_directories("${PROJECT_BINARY_DIR}/include")
endfunction()
