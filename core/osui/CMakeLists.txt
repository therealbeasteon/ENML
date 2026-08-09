add_library(osui STATIC
    src/types.cpp
    src/tree.cpp
    src/layout.cpp
    src/design.cpp
    src/collection.cpp
    src/collection_content.cpp
    src/collection_changes.cpp
    src/text.cpp
    src/paragraph.cpp
    src/renderer.cpp
    src/render_damage.cpp
    src/raster.cpp
    src/contour_aa.cpp
    src/frame_raster.cpp
    src/glyph_raster.cpp
    src/text_command_raster.cpp
    src/input.cpp
    src/accessibility.cpp
    src/motion.cpp
)
add_library(emnl::osui ALIAS osui)

target_include_directories(osui PUBLIC include)
target_link_libraries(osui PUBLIC emnl::oscore PRIVATE emnl_warnings)
target_compile_features(osui PUBLIC cxx_std_20)
target_compile_options(osui PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
