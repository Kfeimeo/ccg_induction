# CMake generated Testfile for 
# Source directory: D:/Dev/C++
# Build directory: D:/Dev/C++
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
include("D:/Dev/C++/tests/ctest_default_config.cmake")
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test([=[scf_tests]=] "D:/Dev/C++/Debug/scf_tests.exe")
  set_tests_properties([=[scf_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/Dev/C++/CMakeLists.txt;49;add_test;D:/Dev/C++/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test([=[scf_tests]=] "D:/Dev/C++/Release/scf_tests.exe")
  set_tests_properties([=[scf_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/Dev/C++/CMakeLists.txt;49;add_test;D:/Dev/C++/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test([=[scf_tests]=] "D:/Dev/C++/MinSizeRel/scf_tests.exe")
  set_tests_properties([=[scf_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/Dev/C++/CMakeLists.txt;49;add_test;D:/Dev/C++/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test([=[scf_tests]=] "D:/Dev/C++/RelWithDebInfo/scf_tests.exe")
  set_tests_properties([=[scf_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/Dev/C++/CMakeLists.txt;49;add_test;D:/Dev/C++/CMakeLists.txt;0;")
else()
  add_test([=[scf_tests]=] NOT_AVAILABLE)
endif()
