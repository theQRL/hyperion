if (EMSCRIPTEN)
	CreateExportedFunctionsForEMSDK(
		ExportedFunctions
		hyperion_license
		hyperion_version
		hyperion_compile
		hyperion_alloc
		hyperion_free
		hyperion_reset
	)
	# Specify which functions to export in hypjson.js.
	# Note that additional Emscripten-generated methods needed by hypc-js are
	# defined to be exported in cmake/ZondCompilerSettings.cmake.
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s EXPORTED_FUNCTIONS='${ExportedFunctions}'")
	add_executable(hypjson libhypc.cpp libhypc.h)
	target_link_libraries(hypjson PRIVATE hyperion)
else()
	add_library(libhypc libhypc.cpp libhypc.h)
	set_target_properties(libhypc PROPERTIES OUTPUT_NAME hypc)
	target_link_libraries(libhypc PRIVATE hyperion)
endif()
