add_library(emnl_compositor_service STATIC
    src/service.cpp
)
add_library(emnl::compositor_service ALIAS emnl_compositor_service)

target_include_directories(emnl_compositor_service
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(emnl_compositor_service
    PUBLIC
        emnl::osdisplay
        emnl::osipc
        emnl::osservice
        emnl_warnings
)

target_compile_options(emnl_compositor_service PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)

add_executable(system.compositor src/main.cpp)
target_link_libraries(system.compositor PRIVATE
    emnl::compositor_service
    emnl::osservice
    emnl_warnings
)
target_compile_options(system.compositor PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
