# CMake generated Testfile for 
# Source directory: /Users/mickelsamuel/dpdk-itch5-feedhandler
# Build directory: /Users/mickelsamuel/dpdk-itch5-feedhandler/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(RingBufferTest "/Users/mickelsamuel/dpdk-itch5-feedhandler/build/test_ring_buffer")
set_tests_properties(RingBufferTest PROPERTIES  _BACKTRACE_TRIPLES "/Users/mickelsamuel/dpdk-itch5-feedhandler/CMakeLists.txt;87;add_test;/Users/mickelsamuel/dpdk-itch5-feedhandler/CMakeLists.txt;0;")
add_test(ParserTest "/Users/mickelsamuel/dpdk-itch5-feedhandler/build/test_parser")
set_tests_properties(ParserTest PROPERTIES  _BACKTRACE_TRIPLES "/Users/mickelsamuel/dpdk-itch5-feedhandler/CMakeLists.txt;95;add_test;/Users/mickelsamuel/dpdk-itch5-feedhandler/CMakeLists.txt;0;")
add_test(MoldUDP64Test "/Users/mickelsamuel/dpdk-itch5-feedhandler/build/test_moldudp64")
set_tests_properties(MoldUDP64Test PROPERTIES  _BACKTRACE_TRIPLES "/Users/mickelsamuel/dpdk-itch5-feedhandler/CMakeLists.txt;103;add_test;/Users/mickelsamuel/dpdk-itch5-feedhandler/CMakeLists.txt;0;")
