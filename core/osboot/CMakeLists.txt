add_library(osboot STATIC
    src/state.cpp
    src/attestation.cpp
    src/sealing.cpp
)

add_library(emnl::osboot ALIAS osboot)

target_include_directories(osboot
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(osboot PUBLIC emnl::oscore emnl_warnings)

target_compile_options(osboot PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
