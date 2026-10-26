cmake_minimum_required(VERSION 3.20)

project(hisense_ir_ac_host_tests C)

enable_testing()

set(FLIPPER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../flipper/hisense_ac)

function(hac_host_test name)
    add_executable(${name} ${ARGN})
    target_compile_features(${name} PRIVATE c_std_11)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
    target_include_directories(${name} PRIVATE ../main)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

hac_host_test(codec_tests codec_test.c ../main/hisense_codec.c)
hac_host_test(ac_logic_tests ac_logic_test.c ../main/ac_logic.c ../main/hisense_codec.c)
hac_host_test(setpoint_routing_tests setpoint_routing_test.c ../main/ac_logic.c ../main/hisense_codec.c)
hac_host_test(commissioning_policy_tests commissioning_policy_test.c ../main/commissioning_policy.c)
hac_host_test(double_reset_tests double_reset_test.c ../main/double_reset.c)

# The codec is a copy of the proven Flipper codec. These fail if the copies drift.
add_test(NAME codec_copy_c_matches_flipper
    COMMAND ${CMAKE_COMMAND} -E compare_files ${FLIPPER_DIR}/hisense_codec.c ${CMAKE_CURRENT_SOURCE_DIR}/../main/hisense_codec.c)
add_test(NAME codec_copy_h_matches_flipper
    COMMAND ${CMAKE_COMMAND} -E compare_files ${FLIPPER_DIR}/hisense_codec.h ${CMAKE_CURRENT_SOURCE_DIR}/../main/hisense_codec.h)
