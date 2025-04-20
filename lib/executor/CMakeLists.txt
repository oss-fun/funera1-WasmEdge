# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2019-2024 Second State INC

wasmedge_add_library(wasmedgeExecutor
  instantiate/import.cpp
  instantiate/function.cpp
  instantiate/global.cpp
  instantiate/table.cpp
  instantiate/memory.cpp
  instantiate/elem.cpp
  instantiate/data.cpp
  instantiate/export.cpp
  instantiate/module.cpp
  instantiate/component.cpp
  instantiate/component/instantiate_component_alias.cpp
  instantiate/component/instantiate_component_canon.cpp
  instantiate/component/instantiate_component_export.cpp
  instantiate/component/instantiate_component_import.cpp
  instantiate/component/instantiate_component_instance.cpp
  instantiate/component/instantiate_component_start.cpp
  instantiate/component/instantiate_component_type.cpp
  instantiate/tag.cpp
  engine/proxy.cpp
  engine/controlInstr.cpp
  engine/tableInstr.cpp
  engine/threadInstr.cpp
  engine/memoryInstr.cpp
  engine/variableInstr.cpp
  engine/refInstr.cpp
  engine/engine.cpp
  helper.cpp
  executor.cpp
  migrator.cpp
)

include(FetchContent)
FetchContent_Declare(
    wasmig
    GIT_REPOSITORY https://github.com/funera1/wasmig.git
    GIT_TAG main
)
FetchContent_GetProperties(wasmig)
if (NOT wasmig_POPULATED)
    message ("-- Fetching wasmig ..")
    FetchContent_Populate(wasmig)
    include_directories("${wasmig_SOURCE_DIR}/include")
    message(${wasmig_BINARY_DIR})
    add_subdirectory(${wasmig_SOURCE_DIR} ${wasmig_BINARY_DIR})
endif()

target_link_libraries(wasmedgeExecutor
  PUBLIC
  wasmedgeCommon
  wasmedgeSystem
  libwasmig
)
