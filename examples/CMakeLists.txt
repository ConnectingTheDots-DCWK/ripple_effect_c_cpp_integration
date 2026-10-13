# Two terminal players over any compiled bundle folder: one in C on the
# header alone, one in C++ with a speaker that prints what it is told. Built
# when this is the top-level project; nothing installs them.

add_executable(ripple_c_player c_player/main.c)
set_target_properties(ripple_c_player PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
target_link_libraries(ripple_c_player PRIVATE Ripple::engine)
ripple_effect_warnings(ripple_c_player)
ripple_effect_copy_runtime(ripple_c_player)

add_executable(ripple_cpp_player cpp_player/main.cpp)
target_link_libraries(ripple_cpp_player PRIVATE Ripple::cpp)
ripple_effect_warnings(ripple_cpp_player)
ripple_effect_copy_runtime(ripple_cpp_player)
