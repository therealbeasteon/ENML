add_library(emnl_key_service STATIC
    src/service.cpp
)

add_library(emnl::key_service ALIAS emnl_key_service)

target_include_directories(emnl_key_service
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(emnl_key_service
    PUBLIC
        emnl::oskeys
        emnl::osipc
        emnl_warnings
)

target_compile_options(emnl_key_service PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
