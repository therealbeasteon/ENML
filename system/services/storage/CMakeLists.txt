add_library(emnl_storage_service STATIC
    src/service.cpp
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

add_executable(system_storage main.cpp)
set_target_properties(system_storage PROPERTIES OUTPUT_NAME "system.storage")

target_link_libraries(system_storage
    PRIVATE
        emnl::storage_service
        emnl::osservice
        emnl_warnings
)

target_compile_options(system_storage PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
