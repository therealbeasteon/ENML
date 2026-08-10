add_library(osaccessibility STATIC
    src/transport.cpp
    src/session_channel.cpp
    src/broker.cpp
)
add_library(emnl::osaccessibility ALIAS osaccessibility)

target_include_directories(osaccessibility PUBLIC include)
target_link_libraries(osaccessibility
    PUBLIC
        emnl::osui
        emnl::osipc
    PRIVATE
        emnl_warnings
)
target_compile_features(osaccessibility PUBLIC cxx_std_20)
target_compile_options(osaccessibility PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
