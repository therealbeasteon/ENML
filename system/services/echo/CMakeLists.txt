add_executable(system_echo main.cpp)
set_target_properties(system_echo PROPERTIES OUTPUT_NAME "system.echo")

target_link_libraries(system_echo
    PRIVATE
        emnl::echo_generated
        emnl::osservice
        emnl_warnings
)

target_compile_options(system_echo PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
