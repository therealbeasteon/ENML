add_library(osdisplay STATIC
    src/compositor.cpp
    src/input.cpp
    src/input_bridge.cpp
    src/trusted_overlay.cpp
    src/trusted_mark.cpp
    src/buffer_linux.cpp
)
add_library(emnl::osdisplay ALIAS osdisplay)

target_include_directories(osdisplay PUBLIC include)
target_link_libraries(osdisplay PUBLIC emnl::oscore PRIVATE emnl_warnings)
target_compile_features(osdisplay PUBLIC cxx_std_20)
target_compile_options(osdisplay PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
