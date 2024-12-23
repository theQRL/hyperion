# ZVMC: Zond Client-VM Connector API.
# Copyright 2016-2019 The ZVMC Authors.
# Licensed under the Apache License, Version 2.0.


add_library(zvmc_loader STATIC loader.h loader.c)
set_target_properties(zvmc_loader PROPERTIES LINKER_LANGUAGE C)
target_link_libraries(zvmc_loader INTERFACE ${CMAKE_DL_LIBS})
target_include_directories(zvmc_loader PUBLIC ${PROJECT_SOURCE_DIR}/test/)
set_target_properties(zvmc_loader PROPERTIES
    POSITION_INDEPENDENT_CODE TRUE
)

add_library(zvmc INTERFACE)
target_sources(zvmc INTERFACE
    ${PROJECT_SOURCE_DIR}/test/zvmc/zvmc.h
    ${PROJECT_SOURCE_DIR}/test/zvmc/zvmc.hpp
    ${PROJECT_SOURCE_DIR}/test/zvmc/helpers.h
    ${PROJECT_SOURCE_DIR}/test/zvmc/utils.h
)
target_include_directories(zvmc INTERFACE ${PROJECT_SOURCE_DIR}/test/)
target_link_libraries(zvmc INTERFACE zvmc_loader)
