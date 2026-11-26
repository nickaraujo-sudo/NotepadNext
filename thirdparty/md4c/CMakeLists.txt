enable_language(C)
# md4c - Markdown parser (MIT), usado pela prévia do craftpad++
add_library(md4c STATIC md4c.c md4c-html.c entity.c)
target_include_directories(md4c PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(md4c PROPERTIES POSITION_INDEPENDENT_CODE ON)
