set(ECHO_OSIDL ${CMAKE_CURRENT_SOURCE_DIR}/echo.osidl)
set(ECHO_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)

set(ECHO_GENERATED_FILES
    ${ECHO_GENERATED_DIR}/echo_types.generated.hpp
    ${ECHO_GENERATED_DIR}/echo_codec.generated.hpp
    ${ECHO_GENERATED_DIR}/echo_codec.generated.cpp
    ${ECHO_GENERATED_DIR}/echo_client.generated.hpp
    ${ECHO_GENERATED_DIR}/echo_server.generated.hpp
    ${ECHO_GENERATED_DIR}/echo_abi.generated.json
)

add_custom_command(
    OUTPUT ${ECHO_GENERATED_FILES}
    COMMAND $<TARGET_FILE:osidlc>
        --input ${ECHO_OSIDL}
        --out-dir ${ECHO_GENERATED_DIR}
    DEPENDS osidlc ${ECHO_OSIDL}
    VERBATIM
    COMMENT "Generating Echo OSIDL bindings"
)

add_custom_target(echo_osidl_generated DEPENDS ${ECHO_GENERATED_FILES})

add_library(echo_generated STATIC
    ${ECHO_GENERATED_DIR}/echo_codec.generated.cpp
)
add_dependencies(echo_generated echo_osidl_generated)

target_include_directories(echo_generated
    PUBLIC
        ${ECHO_GENERATED_DIR}
)

target_link_libraries(echo_generated
    PUBLIC
        emnl::osipc
        emnl_warnings
)

target_compile_options(echo_generated PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)

add_library(emnl::echo_generated ALIAS echo_generated)
