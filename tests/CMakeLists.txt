add_executable(test_validation test_validation.cpp)
target_link_libraries(test_validation PRIVATE ledger_core)
add_test(NAME test_validation COMMAND test_validation)

add_executable(test_wal test_wal.cpp)
target_link_libraries(test_wal PRIVATE ledger_core)
add_test(NAME test_wal COMMAND test_wal)

add_executable(fault_injection_test fault_injection_test.cpp)
target_link_libraries(fault_injection_test PRIVATE ledger_core)
add_test(NAME fault_injection_test COMMAND fault_injection_test)
