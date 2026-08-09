add_library(osapp STATIC
    src/bootstrap.cpp
    src/bootstrap_v2.cpp
    src/service_session.cpp
    src/principal_store_linux.cpp
)

add_library(emnl::osapp ALIAS osapp)

target_include_directories(osapp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(osapp
    PUBLIC
        emnl::osipc
        emnl::ospkg
        emnl_warnings
)

target_compile_options(osapp PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
