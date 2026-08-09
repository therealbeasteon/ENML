add_library(emnl_storage_service STATIC
    src/service.cpp
    src/control.cpp
)

add_library(emnl::storage_service ALIAS emnl_storage_service)

target_include_directories(emnl_storage_service
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(emnl_storage_service
    PUBLIC
        emnl::osstorage
        emnl::storage_generated
        emnl::osipc
        emnl::osservice
        emnl_warnings
)

target_compile_options(emnl_storage_service PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)

add_executable(system.storage src/main.cpp)
target_link_libraries(system.storage PRIVATE
    emnl::storage_service
    emnl::osservice
    emnl_warnings
)
target_compile_options(system.storage PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
