find_package(Freetype REQUIRED)
find_package(ICU REQUIRED COMPONENTS uc i18n)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HARFBUZZ REQUIRED IMPORTED_TARGET harfbuzz)

add_library(osui_linux_text_backend STATIC
    src/linux_text_backend.cpp
)
add_library(emnl::osui_linux_text_backend ALIAS osui_linux_text_backend)

target_include_directories(osui_linux_text_backend PUBLIC include)
target_link_libraries(osui_linux_text_backend
    PUBLIC emnl::osui
    PRIVATE Freetype::Freetype ICU::uc ICU::i18n PkgConfig::HARFBUZZ emnl_warnings
)
target_compile_features(osui_linux_text_backend PUBLIC cxx_std_20)
target_compile_options(osui_linux_text_backend PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
