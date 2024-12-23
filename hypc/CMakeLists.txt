set(libhypcli_sources
	CommandLineInterface.cpp CommandLineInterface.h
	CommandLineParser.cpp CommandLineParser.h
	Exceptions.h
)

add_library(hypcli ${libhypcli_sources})
target_link_libraries(hypcli PUBLIC hyperion Boost::boost Boost::program_options)

set(sources main.cpp)

add_executable(hypc ${sources})
target_link_libraries(hypc PRIVATE hypcli)

include(GNUInstallDirs)
install(TARGETS hypc DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(HYPC_LINK_STATIC AND UNIX AND NOT APPLE)
	# Produce hypc as statically linked binary (includes C/C++ standard libraries)
	# This is not supported on macOS, see
	# https://developer.apple.com/library/content/qa/qa1118/_index.html.
	set_target_properties(
		hypc PROPERTIES
		LINK_FLAGS -static
		LINK_SEARCH_START_STATIC ON
		LINK_SEARCH_END_STATIC ON
	)
elseif(HYPC_STATIC_STDLIBS AND UNIX AND NOT APPLE)
	set_target_properties(
		hypc PROPERTIES
		LINK_FLAGS "-static-libgcc -static-libstdc++"
	)
endif()
