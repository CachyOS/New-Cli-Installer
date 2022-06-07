project('ftxui', 'cpp',
    version: '3.0.0',
    license : 'MIT',
    default_options: ['cpp_std=c++20']
)

cpp = meson.get_compiler('cpp')
thread_dep = dependency('threads')
ranges = dependency('range-v3', version : ['>=0.11.0'], fallback : ['range-v3', 'range_dep'])

ftxui_inc = include_directories('include')
ftxui_priv_inc = include_directories('src')
ftxui_screen_lib = static_library('ftxui_screen',
    sources : [
        'src/ftxui/screen/color.cpp',
        'src/ftxui/screen/screen.cpp',
        'src/ftxui/screen/string.cpp',
        'src/ftxui/screen/terminal.cpp',
        'include/ftxui/screen/box.hpp',
        'include/ftxui/screen/color.hpp',
        'include/ftxui/screen/color_info.hpp',
        'include/ftxui/screen/screen.hpp',
        'include/ftxui/screen/string.hpp'
    ],
    include_directories : ftxui_inc,
    dependencies : [thread_dep, ranges]
)

ftxui_dom_lib = static_library('ftxui_dom',
    sources : [
      'include/ftxui/dom/elements.hpp',
      'include/ftxui/dom/flexbox_config.hpp',
      'include/ftxui/dom/node.hpp',
      'include/ftxui/dom/requirement.hpp',
      'include/ftxui/dom/take_any_args.hpp',
      'src/ftxui/dom/automerge.cpp',
      'src/ftxui/dom/blink.cpp',
      'src/ftxui/dom/bold.cpp',
      'src/ftxui/dom/border.cpp',
      'src/ftxui/dom/box_helper.hpp',
      'src/ftxui/dom/box_helper.cpp',
      'src/ftxui/dom/canvas.cpp',
      'src/ftxui/dom/clear_under.cpp',
      'src/ftxui/dom/color.cpp',
      'src/ftxui/dom/composite_decorator.cpp',
      'src/ftxui/dom/dbox.cpp',
      'src/ftxui/dom/dim.cpp',
      'src/ftxui/dom/flex.cpp',
      'src/ftxui/dom/flexbox.cpp',
      'src/ftxui/dom/flexbox_helper.cpp',
      'src/ftxui/dom/flexbox_helper.hpp',
      'src/ftxui/dom/focus.cpp',
      'src/ftxui/dom/frame.cpp',
      'src/ftxui/dom/gauge.cpp',
      'src/ftxui/dom/graph.cpp',
      'src/ftxui/dom/gridbox.cpp',
      'src/ftxui/dom/hbox.cpp',
      'src/ftxui/dom/inverted.cpp',
      'src/ftxui/dom/node.cpp',
      'src/ftxui/dom/node_decorator.hpp',
      'src/ftxui/dom/node_decorator.cpp',
      'src/ftxui/dom/paragraph.cpp',
      'src/ftxui/dom/reflect.cpp',
      'src/ftxui/dom/scroll_indicator.cpp',
      'src/ftxui/dom/separator.cpp',
      'src/ftxui/dom/size.cpp',
      'src/ftxui/dom/spinner.cpp',
      'src/ftxui/dom/table.cpp',
      'src/ftxui/dom/text.cpp',
      'src/ftxui/dom/underlined.cpp',
      'src/ftxui/dom/util.cpp',
      'src/ftxui/dom/vbox.cpp'
    ],
    include_directories : [ftxui_inc, ftxui_priv_inc],
    link_with : [ftxui_screen_lib],
    dependencies : [thread_dep, ranges]
)

ftxui_component_lib = static_library('ftxui_component',
    sources : [
      'include/ftxui/component/animation.hpp',
      'include/ftxui/component/captured_mouse.hpp',
      'include/ftxui/component/component.hpp',
      'include/ftxui/component/component_base.hpp',
      'include/ftxui/component/component_options.hpp',
      'include/ftxui/component/event.hpp',
      'include/ftxui/component/mouse.hpp',
      'include/ftxui/component/receiver.hpp',
      'include/ftxui/component/screen_interactive.hpp',
      'src/ftxui/component/animation.cpp',
      'src/ftxui/component/button.cpp',
      'src/ftxui/component/catch_event.cpp',
      'src/ftxui/component/checkbox.cpp',
      'src/ftxui/component/collapsible.cpp',
      'src/ftxui/component/component.cpp',
      'src/ftxui/component/component_options.cpp',
      'src/ftxui/component/container.cpp',
      'src/ftxui/component/dropdown.cpp',
      'src/ftxui/component/event.cpp',
      'src/ftxui/component/input.cpp',
      'src/ftxui/component/maybe.cpp',
      'src/ftxui/component/menu.cpp',
      'src/ftxui/component/radiobox.cpp',
      'src/ftxui/component/radiobox.cpp',
      'src/ftxui/component/renderer.cpp',
      'src/ftxui/component/resizable_split.cpp',
      'src/ftxui/component/screen_interactive.cpp',
      'src/ftxui/component/slider.cpp',
      'src/ftxui/component/terminal_input_parser.cpp',
      'src/ftxui/component/terminal_input_parser.hpp',
      'src/ftxui/component/util.cpp'
    ],
    include_directories : [ftxui_inc, ftxui_priv_inc],
    link_with : [ftxui_dom_lib],
    dependencies : [thread_dep, ranges]
)

ftxui_dep = declare_dependency(
    include_directories : ftxui_inc,
    link_with : [ftxui_screen_lib, ftxui_dom_lib, ftxui_component_lib]
)
