# NVLink Fabric test suite.
# Each test is a standalone executable registered with CTest. No test timeouts
# are used; a hanging test is a defect to be diagnosed and fixed, never killed.
find_package(Threads REQUIRED)

function(nvf_add_test NAME SRC)
  add_executable(${NAME} ${SRC})
  target_link_libraries(${NAME} PRIVATE nvlinkfabric Threads::Threads)
  target_include_directories(${NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_options(${NAME} PRIVATE ${NVF_STATIC_WARNINGS})
  add_dependencies(${NAME} nvlinkfabric)
  add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

nvf_add_test(nvf_smoke smoke.cpp)
nvf_add_test(nvf_persistence persistence_test.cpp)
nvf_add_test(nvf_protocol protocol_test.cpp)
nvf_add_test(nvf_runtime runtime_test.cpp)
nvf_add_test(nvf_property property_test.cpp)
nvf_add_test(nvf_race race_test.cpp)
nvf_add_test(nvf_adversarial adversarial_test.cpp)
add_dependencies(nvf_runtime nvlink_worker)
target_compile_definitions(nvf_runtime PRIVATE NVF_WORKER_EXE="$<TARGET_FILE:nvlink_worker>") 
if(TARGET nvlinkfabric_nvidia)
  nvf_add_test(nvf_nvidia nvidia_test.cpp)
  target_link_libraries(nvf_nvidia PRIVATE nvlinkfabric_nvidia)
endif()
