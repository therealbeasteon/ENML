add_executable(system.shell
    src/main.cpp
)

target_link_libraries(system.shell PRIVATE
    osshell
    emnl::app_manager
    emnl::osservice
    emnl_warnings
)

target_compile_features(system.shell PRIVATE cxx_std_20)
target_compile_options(system.shell PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
