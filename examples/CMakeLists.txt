add_executable(example_simple simple.c)
target_link_libraries(example_simple PRIVATE softline)
softline_apply_warnings(example_simple)

add_executable(example_chat chat.c)
target_link_libraries(example_chat PRIVATE softline)
softline_apply_warnings(example_chat)
