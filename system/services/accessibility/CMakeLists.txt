add_library(emnl_accessibility_service STATIC
    src/service.cpp
)
add_library(emnl::accessibility_service ALIAS emnl_accessibility_service)

target_include_directories(emnl_accessibility_service
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(emnl_accessibility_service
    PUBLIC
        emnl::osaccessibility
        emnl::osipc
        emnl::osservice
        emnl_warnings
)

target_compile_options(emnl_accessibility_service PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)

add_executable(system.accessibility src/main.cpp)
target_link_libraries(system.accessibility PRIVATE
    emnl::accessibility_service
    emnl::osservice
    emnl_warnings
)
target_compile_options(system.accessibility PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
