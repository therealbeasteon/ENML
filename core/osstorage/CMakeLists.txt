add_library(osstorage STATIC
    src/path.cpp
    src/private_storage_linux.cpp
    src/protected_format.cpp
    src/protected_crypto.cpp
    src/protected_key_record.cpp
    src/protected_atomic_replace.cpp
)

add_library(emnl::osstorage ALIAS osstorage)

target_include_directories(osstorage
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(osstorage
    PUBLIC
        emnl::oscore
        emnl::osipc
        emnl::oskeys
        emnl_warnings
)

target_compile_options(osstorage PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
