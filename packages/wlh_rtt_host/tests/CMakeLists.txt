cmake_minimum_required(VERSION 3.16)
project(wlh_rtt_host_logic_tests C)

enable_testing()

add_executable(wlh_rtt_host_logic_tests
    test_logic.c
    ../src/wlh_rtt_logic.c
    ../transport/sdio/wlh_rtt_sdio_protocol.c
    ../iperf2/wlh_rtt_iperf2_proto.c
)
target_include_directories(wlh_rtt_host_logic_tests PRIVATE
    ../include
    ../src
    ../transport/sdio
    ../iperf2
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/protocol/include
)
target_compile_options(wlh_rtt_host_logic_tests PRIVATE
    -Wall -Wextra -Werror
)
add_test(NAME wlh_rtt_host_logic COMMAND wlh_rtt_host_logic_tests)
