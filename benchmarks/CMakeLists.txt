# NVLink Fabric benchmarks: measure completed operations only.
find_package(Threads REQUIRED)
add_executable(nvf_benchmarks benchmarks.cpp)
target_link_libraries(nvf_benchmarks PRIVATE nvlinkfabric Threads::Threads)
target_compile_options(nvf_benchmarks PRIVATE ${NVF_STATIC_WARNINGS})
add_dependencies(nvf_benchmarks nvlinkfabric)
