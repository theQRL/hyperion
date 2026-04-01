# QRVMC: Quantum Resistant Client-VM Connector API.
# Copyright 2016-2019 The EVMC Authors.
# Licensed under the Apache License, Version 2.0.


add_library(qrvmc_loader STATIC loader.h loader.c)
set_target_properties(qrvmc_loader PROPERTIES LINKER_LANGUAGE C)
target_link_libraries(qrvmc_loader INTERFACE ${CMAKE_DL_LIBS})
target_include_directories(qrvmc_loader PUBLIC ${PROJECT_SOURCE_DIR}/test/)
set_target_properties(qrvmc_loader PROPERTIES
    POSITION_INDEPENDENT_CODE TRUE
)

add_library(qrvmc INTERFACE)
target_sources(qrvmc INTERFACE
    ${PROJECT_SOURCE_DIR}/test/qrvmc/qrvmc.h
    ${PROJECT_SOURCE_DIR}/test/qrvmc/qrvmc.hpp
    ${PROJECT_SOURCE_DIR}/test/qrvmc/helpers.h
    ${PROJECT_SOURCE_DIR}/test/qrvmc/utils.h
)
target_include_directories(qrvmc INTERFACE ${PROJECT_SOURCE_DIR}/test/)
target_link_libraries(qrvmc INTERFACE qrvmc_loader)