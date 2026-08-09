add_library(osui STATIC
    src/types.cpp
    src/tree.cpp
    src/layout.cpp
    src/design.cpp
    src/collection.cpp
    src/text.cpp
    src/renderer.cpp
    src/raster.cpp
)
add_library(emnl::osui ALIAS osui)

target_include_directories(osui PUBLIC include)
target_link_libraries(osui PUBLIC emnl::oscore PRIVATE emnl_warnings)
target_compile_features(osui PUBLIC cxx_std_20)
target_compile_options(osui PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
