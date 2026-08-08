add_library(osservice STATIC
    src/bootstrap.cpp
    src/identity.cpp
)

add_library(emnl::osservice ALIAS osservice)

target_include_directories(osservice
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(osservice
    PUBLIC
        emnl::osipc
        emnl_warnings
)

target_compile_options(osservice PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
