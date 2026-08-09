set(STORAGE_OSIDL ${CMAKE_CURRENT_SOURCE_DIR}/storage.osidl)
set(STORAGE_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)

set(STORAGE_GENERATED_FILES
    ${STORAGE_GENERATED_DIR}/storage_types.generated.hpp
    ${STORAGE_GENERATED_DIR}/storage_codec.generated.hpp
    ${STORAGE_GENERATED_DIR}/storage_codec.generated.cpp
    ${STORAGE_GENERATED_DIR}/storage_client.generated.hpp
    ${STORAGE_GENERATED_DIR}/storage_server.generated.hpp
    ${STORAGE_GENERATED_DIR}/storage_abi.generated.json
)

if(CMAKE_CROSSCOMPILING)
    if(NOT EMNL_OSIDLC_EXECUTABLE)
        message(FATAL_ERROR
            "Cross builds require -DEMNL_OSIDLC_EXECUTABLE=/absolute/path/to/host/osidlc")
    endif()
    if(NOT IS_ABSOLUTE "${EMNL_OSIDLC_EXECUTABLE}")
        message(FATAL_ERROR "EMNL_OSIDLC_EXECUTABLE must be an absolute host path")
    endif()
    set(STORAGE_OSIDLC_COMMAND "${EMNL_OSIDLC_EXECUTABLE}")
    set(STORAGE_OSIDLC_DEPENDS ${STORAGE_OSIDL})
else()
    set(STORAGE_OSIDLC_COMMAND $<TARGET_FILE:osidlc>)
    set(STORAGE_OSIDLC_DEPENDS osidlc ${STORAGE_OSIDL})
endif()

add_custom_command(
    OUTPUT ${STORAGE_GENERATED_FILES}
    COMMAND ${STORAGE_OSIDLC_COMMAND}
        --input ${STORAGE_OSIDL}
        --out-dir ${STORAGE_GENERATED_DIR}
    DEPENDS ${STORAGE_OSIDLC_DEPENDS}
    VERBATIM
    COMMENT "Generating Storage Service OSIDL bindings"
)

add_custom_target(storage_osidl_generated DEPENDS ${STORAGE_GENERATED_FILES})

add_library(storage_generated STATIC
    ${STORAGE_GENERATED_DIR}/storage_codec.generated.cpp
)
add_dependencies(storage_generated storage_osidl_generated)

target_include_directories(storage_generated
    PUBLIC
        ${STORAGE_GENERATED_DIR}
)

target_link_libraries(storage_generated
    PUBLIC
        emnl::osipc
        emnl_warnings
)

target_compile_options(storage_generated PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)

add_library(emnl::storage_generated ALIAS storage_generated)
