add_library(osipc STATIC
    src/channel_linux.cpp
    src/decoder.cpp
    src/encoder.cpp
    src/rpc.cpp
    src/wire.cpp
)

add_library(emnl::osipc ALIAS osipc)

target_include_directories(osipc
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(osipc
    PUBLIC
        emnl::oscore
        emnl_warnings
)

target_compile_options(osipc PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
