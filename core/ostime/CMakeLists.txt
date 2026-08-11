add_library(ostime STATIC
    src/protection.cpp
)

add_library(emnl::ostime ALIAS ostime)

target_include_directories(ostime
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(ostime PUBLIC emnl::oscore emnl_warnings)

target_compile_options(ostime PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
