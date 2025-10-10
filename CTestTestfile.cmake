# CMake generated Testfile for 
# Source directory: /root/keycuda
# Build directory: /root/keycuda
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[puzzle71_tests]=] "/root/keycuda/puzzle71_tests")
set_tests_properties([=[puzzle71_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/root/keycuda/CMakeLists.txt;353;add_test;/root/keycuda/CMakeLists.txt;0;")
subdirs("third_party/bitcoin-core-secp256k1")
subdirs("_deps/nlohmann_json-build")
subdirs("_deps/googletest-build")
