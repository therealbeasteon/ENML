function(add_emnl_ui_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE emnl::osui emnl_warnings)
    target_compile_options(${name} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
    )
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS "m3-ui")
endfunction()

add_emnl_ui_test(ui_semantic_tree_test semantic_tree_test.cpp)
add_emnl_ui_test(ui_responsive_layout_test responsive_layout_test.cpp)
add_emnl_ui_test(ui_render_snapshot_test render_snapshot_test.cpp)
add_emnl_ui_test(ui_collection_window_test collection_window_test.cpp)
add_emnl_ui_test(ui_design_reflow_test design_reflow_test.cpp)
