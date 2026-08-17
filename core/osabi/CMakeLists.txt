add_library(osabi STATIC
    src/syscall.cpp
    src/syscall_aarch64.cpp
)

add_library(emnl::osabi ALIAS osabi)

target_include_directories(osabi
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# The kernel's headers, deliberately not the kernel's library.
#
# The type of a call's arguments is the kernel's to define; a second definition
# here would be the second-source-of-truth defect docs/M7_11_MEMORY.md and
# docs/M7_12_CKX_FORMAT.md have each already refused once. But no kernel code
# should be linkable into a user program merely because it uses the ABI, so this
# takes the include directories and not the target.
target_include_directories(osabi
    PUBLIC
        $<TARGET_PROPERTY:oskernel,INTERFACE_INCLUDE_DIRECTORIES>
)

target_link_libraries(osabi PUBLIC emnl::oscore emnl_warnings)

target_compile_options(osabi PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
