add_library(osidlc_lib STATIC
    src/compiler.cpp
)

target_include_directories(osidlc_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(osidlc_lib PUBLIC emnl_warnings)

add_executable(osidlc
    src/main.cpp
)

target_link_libraries(osidlc PRIVATE osidlc_lib emnl_warnings)
