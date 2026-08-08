add_library(oscore STATIC
    src/native_handle_linux.cpp
)

add_library(emnl::oscore ALIAS oscore)

target_include_directories(oscore
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(oscore PUBLIC emnl_warnings)

target_compile_options(oscore PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
