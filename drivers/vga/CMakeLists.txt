add_library(vga INTERFACE)

target_sources(vga INTERFACE ${CMAKE_CURRENT_LIST_DIR}/vga.c)

target_include_directories(vga INTERFACE ${CMAKE_CURRENT_LIST_DIR})
