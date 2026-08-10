add_library(oscollection STATIC
    src/transport.cpp
)
add_library(emnl::oscollection ALIAS oscollection)

target_include_directories(oscollection PUBLIC include)
target_link_libraries(oscollection
    PUBLIC
        emnl::osui
        emnl::osipc
    PRIVATE
        emnl_warnings
)
target_compile_features(oscollection PUBLIC cxx_std_20)
target_compile_options(oscollection PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
