subdir('shader')
subdir('reshade')

imgui_src = [
    'imgui/imgui.cpp',
    'imgui/imgui_draw.cpp',
    'imgui/imgui_tables.cpp',
    'imgui/imgui_widgets.cpp',
    'imgui/backends/imgui_impl_vulkan.cpp',
]

overlay_src = [
    'overlay/imgui_overlay.cpp',
    'overlay/view_add_effects.cpp',
    'overlay/view_configs.cpp',
    'overlay/view_debug.cpp',
    'overlay/view_diagnostics.cpp',
    'overlay/view_settings.cpp',
    'overlay/view_shader_manager.cpp',
    'overlay/view_shader_test.cpp',
    'overlay/view_main.cpp',
    'overlay/params/field_editor.cpp',
    'overlay/params/fields/field_bool.cpp',
    'overlay/params/fields/field_float.cpp',
    'overlay/params/fields/field_floatvec.cpp',
    'overlay/params/fields/field_int.cpp',
    'overlay/params/fields/field_intvec.cpp',
    'overlay/params/fields/field_uint.cpp',
    'overlay/params/fields/field_uintvec.cpp',
]

vkBasalt_src = [
    'basalt.cpp',
    'buffer.cpp',
    'command_buffer.cpp',
    'config.cpp',
    'config_serializer.cpp',
    'descriptor_set.cpp',
    'effects/effect.cpp',
    'effects/effect_registry.cpp',
    'effects/effect_reshade.cpp',
    'effects/effect_simple.cpp',
    'effects/effect_transfer.cpp',
    'effects/builtin/builtin_effects.cpp',
    'effects/builtin/effect_cas.cpp',
    'effects/builtin/effect_deband.cpp',
    'effects/builtin/effect_depth_composite.cpp',
    'effects/builtin/effect_dls.cpp',
    'effects/builtin/effect_fxaa.cpp',
    'effects/builtin/effect_lut.cpp',
    'effects/builtin/effect_smaa.cpp',
    'reshade_parser.cpp',
    'fake_swapchain.cpp',
    'format.cpp',
    'framebuffer.cpp',
    'graphics_pipeline.cpp',
    'image.cpp',
    'image_view.cpp',
    'keyboard_input.cpp',
    'logger.cpp',
    'logical_swapchain.cpp',
    'lut_cube.cpp',
    'memory.cpp',
    'renderpass.cpp',
    'reshade_uniforms.cpp',
    'sampler.cpp',
    'settings_manager.cpp',
    'shader.cpp',
    'stb_image.c',
    'stb_image_resize.c',
    'util.cpp',
    'vkdispatch.cpp',
]

x11_dep = dependency('x11')
xi_dep = dependency('xi')

conf_paths = configuration_data()
conf_paths.set_quoted('SYSCONFDIR', join_paths(get_option('prefix'), get_option('sysconfdir')))
conf_paths.set_quoted('DATADIR', join_paths(get_option('prefix'), get_option('datadir')))

configure_file(output: 'config_paths.hpp',
               configuration : conf_paths)

# X11/X.h library contains many definitions (e.g. None)
# which conflicts with the rest codebase with with Meson --unity=on option
keyboard_input_x11_lib = static_library('keyboard_input_x11', 'keyboard_input_x11.cpp')
mouse_input_lib = static_library('mouse_input', 'mouse_input.cpp')
input_blocker_lib = static_library('input_blocker', 'input_blocker.cpp')

imgui_inc = include_directories('imgui')
overlay_inc = include_directories('overlay')
effects_inc = include_directories('effects')
effects_builtin_inc = include_directories('effects/builtin')
effects_params_inc = include_directories('effects/params')

shared_library(meson.project_name().to_lower(),
    vkBasalt_src, overlay_src, imgui_src, shader_include,
    link_with: [keyboard_input_x11_lib, mouse_input_lib, input_blocker_lib],
    include_directories : [vkBasalt_include_path, imgui_inc, overlay_inc, effects_inc, effects_builtin_inc, effects_params_inc],
    cpp_args : ['-DIMGUI_IMPL_VULKAN_NO_PROTOTYPES'],
    dependencies : [x11_dep, xi_dep, reshade_dep],
    gnu_symbol_visibility: 'hidden',
    install : true,
    install_dir : lib_dir)
