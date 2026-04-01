function(create_build_info NAME)

	# Set build platform; to be written to BuildInfo.h
	set(QRL_BUILD_OS "${CMAKE_SYSTEM_NAME}")

	if (CMAKE_COMPILER_IS_MINGW)
		set(QRL_BUILD_COMPILER "mingw")
	elseif (CMAKE_COMPILER_IS_MSYS)
		set(QRL_BUILD_COMPILER "msys")
	elseif (CMAKE_COMPILER_IS_GNUCXX)
		set(QRL_BUILD_COMPILER "g++")
	elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
		set(QRL_BUILD_COMPILER "msvc")
	elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
		set(QRL_BUILD_COMPILER "clang")
	elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
		set(QRL_BUILD_COMPILER "appleclang")
	else ()
		set(QRL_BUILD_COMPILER "unknown")
	endif ()

	set(QRL_BUILD_PLATFORM "${QRL_BUILD_OS}.${QRL_BUILD_COMPILER}")

	#cmake build type may be not speCified when using msvc
	if (CMAKE_BUILD_TYPE)
		set(_cmake_build_type ${CMAKE_BUILD_TYPE})
	else()
		set(_cmake_build_type "${CMAKE_CFG_INTDIR}")
	endif()

	# Generate header file containing useful build information
	add_custom_target(${NAME}_BuildInfo.h ALL
		WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
		COMMAND ${CMAKE_COMMAND} -DQRL_SOURCE_DIR=${PROJECT_SOURCE_DIR} -DQRL_BUILDINFO_IN=${QRL_CMAKE_DIR}/templates/BuildInfo.h.in -DQRL_DST_DIR=${PROJECT_BINARY_DIR}/include/${PROJECT_NAME} -DQRL_CMAKE_DIR=${QRL_CMAKE_DIR}
		-DQRL_BUILD_TYPE="${_cmake_build_type}"
		-DQRL_BUILD_OS="${QRL_BUILD_OS}"
		-DQRL_BUILD_COMPILER="${QRL_BUILD_COMPILER}"
		-DQRL_BUILD_PLATFORM="${QRL_BUILD_PLATFORM}"
		-DPROJECT_VERSION="${PROJECT_VERSION}"
		-DPROJECT_VERSION_MAJOR="${PROJECT_VERSION_MAJOR}"
		-DPROJECT_VERSION_MINOR="${PROJECT_VERSION_MINOR}"
		-DPROJECT_VERSION_PATCH="${PROJECT_VERSION_PATCH}"
		-P "${QRL_SCRIPTS_DIR}/buildinfo.cmake"
		)
	include_directories("${PROJECT_BINARY_DIR}/include")
endfunction()
