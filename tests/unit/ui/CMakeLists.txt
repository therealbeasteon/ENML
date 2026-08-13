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
add_emnl_ui_test(ui_collection_content_test collection_content_test.cpp)
add_emnl_ui_test(ui_collection_changes_test collection_changes_test.cpp)
add_emnl_ui_test(ui_design_reflow_test design_reflow_test.cpp)
add_emnl_ui_test(ui_decision_layout_test decision_layout_test.cpp)
add_emnl_ui_test(ui_contrast_test contrast_test.cpp)
add_emnl_ui_test(ui_renderer_command_test renderer_command_test.cpp)
add_emnl_ui_test(ui_render_damage_test render_damage_test.cpp)
add_emnl_ui_test(ui_text_layout_test text_layout_test.cpp)
add_emnl_ui_test(ui_paragraph_layout_test paragraph_layout_test.cpp)
add_emnl_ui_test(ui_raster_test raster_test.cpp)
add_emnl_ui_test(ui_contour_aa_test contour_aa_test.cpp)
add_emnl_ui_test(ui_glyph_raster_test glyph_raster_test.cpp)
add_emnl_ui_test(ui_text_command_raster_test text_command_raster_test.cpp)
add_emnl_ui_test(ui_input_transform_test input_transform_test.cpp)
add_emnl_ui_test(ui_input_routing_test input_routing_test.cpp)
add_emnl_ui_test(ui_accessibility_bridge_test accessibility_bridge_test.cpp)
add_emnl_ui_test(ui_motion_timeline_test motion_timeline_test.cpp)
add_emnl_ui_test(ui_identity_adaptation_test identity_adaptation_test.cpp)
add_emnl_ui_test(ui_wallpaper_widget_test wallpaper_widget_test.cpp)
add_emnl_ui_test(ui_system_scene_test system_scene_test.cpp)
add_emnl_ui_test(ui_system_interactions_test system_interactions_test.cpp)
add_emnl_ui_test(ui_finish_contract_test ui_finish_contract_test.cpp)

if(EMNL_BUILD_LINUX_TEXT_BACKEND)
    find_file(EMNL_TEST_FONT_FILE
        NAMES DejaVuSans.ttf
        PATHS /usr/share/fonts/truetype/dejavu
        NO_DEFAULT_PATH
        REQUIRED
    )
    add_executable(ui_linux_text_backend_test linux_text_backend_test.cpp)
    target_link_libraries(ui_linux_text_backend_test
        PRIVATE emnl::osui_linux_text_backend emnl_warnings
    )
    target_compile_definitions(ui_linux_text_backend_test PRIVATE
        EMNL_TEST_FONT_PATH="${EMNL_TEST_FONT_FILE}"
    )
    target_compile_options(ui_linux_text_backend_test PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
    )
    add_test(NAME ui_linux_text_backend_test COMMAND ui_linux_text_backend_test)
    set_tests_properties(ui_linux_text_backend_test PROPERTIES LABELS "m3-ui")
endif()
