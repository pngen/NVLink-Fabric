# NVLink Fabric examples. Each is built from the public/installed API.
find_package(Threads REQUIRED)

function(nvf_example NAME SRC)
  add_executable(${NAME} ${SRC})
  target_link_libraries(${NAME} PRIVATE nvlinkfabric Threads::Threads)
  target_compile_options(${NAME} PRIVATE ${NVF_STATIC_WARNINGS})
  add_dependencies(${NAME} nvlinkfabric)
endfunction()

nvf_example(nvf_example_synthetic synthetic_topology.cpp)
nvf_example(nvf_example_stale stale_invalidation.cpp)

if(TARGET nvlinkfabric_nvidia)
  nvf_example(nvf_example_nvidia nvidia_discovery.cpp)
  target_link_libraries(nvf_example_nvidia PRIVATE nvlinkfabric_nvidia)
  target_compile_definitions(nvf_example_nvidia PRIVATE NVF_WITH_NVIDIA=1)
  add_dependencies(nvf_example_nvidia nvlinkfabric_nvidia)
endif()
