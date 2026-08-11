add_library(osdevice STATIC
    src/access.cpp
)

add_library(emnl::osdevice ALIAS osdevice)

target_include_directories(osdevice
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(osdevice PUBLIC emnl::oscore emnl_warnings)

target_compile_options(osdevice PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
