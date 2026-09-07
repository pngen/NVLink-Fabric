cmake_minimum_required(VERSION 3.20)

project(NVLinkFabric
  VERSION 1.0.0
  DESCRIPTION "NVLink Fabric: discover, model, measure, and govern NVLink-class accelerator interconnects"
  LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Policy and global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Options
option(NVF_BUILD_TESTS "Build the NVLink Fabric test suite" ON)
option(NVF_BUILD_EXAMPLES "Build NVLink Fabric examples" ON)
option(NVF_BUILD_BENCHMARKS "Build NVLink Fabric benchmarks" ON)
option(NVF_BUILD_CLI "Build the nvlink_fabric_inspect CLI tool" ON)
option(NVF_ENABLE_CUDA "Build the NVIDIA/CUDA/NVML backend (requires CUDA toolkit)" ON)
option(NVF_ENABLE_ASAN "Instrument the vendor-neutral core with AddressSanitizer" OFF)
option(NVF_INSTALL "Generate install and package targets" ON)

# ---------------------------------------------------------------------------
# Compiler / warning configuration (first-party code)
# ---------------------------------------------------------------------------
set(NVF_STATIC_WARNINGS "")
if(MSVC)
  # /W4 for warnings, /WX to treat warnings as errors (first-party code only).
  set(NVF_STATIC_WARNINGS /W4 /WX /permissive- /EHsc /GR /Zc:__cplusplus)
  if(NVF_ENABLE_ASAN)
    # ASan requires debug-info emission; /Z7 embeds CodeView into objects.
    add_compile_options(/fsanitize=address /Z7)
    add_link_options(/DEBUG)
  endif()
else()
  set(NVF_STATIC_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
  if(NVF_ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  endif()
endif()

# Set default warning flags for all targets in this project.
add_compile_options(${NVF_STATIC_WARNINGS})

# ---------------------------------------------------------------------------
# Source file lists
# ---------------------------------------------------------------------------
set(NVF_CORE_SOURCES
  src/core/registry.cpp
  src/core/path.cpp
  src/core/policy.cpp
  src/core/persistence.cpp
  src/core/serialization.cpp
  src/core/runtime.cpp
  src/core/protocol.cpp
)

set(NVF_BACKEND_SOURCES
  src/backend/backend.cpp
  src/backend/synthetic_backend.cpp
  src/backend/nop_backend.cpp
)

# ---------------------------------------------------------------------------
# Static topology library (vendor-neutral core + vendor-neutral backends)
# ---------------------------------------------------------------------------
add_library(nvlinkfabric STATIC ${NVF_CORE_SOURCES} ${NVF_BACKEND_SOURCES})
add_library(nvlinkfabric::nvlinkfabric ALIAS nvlinkfabric)

# Public include interface: build tree and install tree separated so the
# exported package never references a source-directory path.
target_include_directories(nvlinkfabric PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_include_directories(nvlinkfabric PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_definitions(nvlinkfabric PUBLIC NVLINKFABRIC_VERSION="1.0.0")

set_target_properties(nvlinkfabric PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
target_link_libraries(nvlinkfabric PUBLIC Threads::Threads)
if(MSVC)
  target_link_libraries(nvlinkfabric PUBLIC ws2_32)
endif()
target_compile_options(nvlinkfabric PRIVATE ${NVF_STATIC_WARNINGS})

# ---------------------------------------------------------------------------
# Optional NVIDIA/CUDA/NVML backend (separate target; keeps CUDA optional)
# ---------------------------------------------------------------------------
if(NVF_ENABLE_CUDA)
  find_package(CUDAToolkit QUIET)
  if(CUDAToolkit_FOUND)
    message(STATUS "NVLink Fabric: CUDA backend ENABLED (CUDA ${CUDAToolkit_VERSION})")
    add_library(nvlinkfabric_nvidia STATIC src/backend/nvidia_backend.cpp)
    add_library(nvlinkfabric::nvidia ALIAS nvlinkfabric_nvidia)
    target_include_directories(nvlinkfabric_nvidia PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:include>)
    target_link_libraries(nvlinkfabric_nvidia PRIVATE nvlinkfabric
      CUDA::cudart CUDA::nvml CUDA::cuda_driver CUDA::nvrtc)
    set_target_properties(nvlinkfabric_nvidia PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_options(nvlinkfabric_nvidia PRIVATE ${NVF_STATIC_WARNINGS})
    if(CUDAToolkit_INCLUDE_DIRS OR CUDAToolkit_INCLUDE_DIR)
      target_include_directories(nvlinkfabric_nvidia SYSTEM PRIVATE
        ${CUDAToolkit_INCLUDE_DIRS} ${CUDAToolkit_INCLUDE_DIR})
    endif()
  else()
    message(STATUS "NVLink Fabric: CUDA toolkit not found; CUDA backend DISABLED")
  endif()
endif()

# ---------------------------------------------------------------------------
# Inspection CLI
# ---------------------------------------------------------------------------
if(NVF_BUILD_CLI)
  add_executable(nvlink_fabric_inspect tools/nvlink_fabric_inspect.cpp)
  target_link_libraries(nvlink_fabric_inspect PRIVATE nvlinkfabric)
  target_compile_options(nvlink_fabric_inspect PRIVATE ${NVF_STATIC_WARNINGS})
  if(TARGET nvlinkfabric_nvidia)
    target_link_libraries(nvlink_fabric_inspect PRIVATE nvlinkfabric_nvidia)
    target_compile_definitions(nvlink_fabric_inspect PRIVATE NVF_WITH_NVIDIA=1)
  endif()
endif()

# ---------------------------------------------------------------------------
# Tests / examples / benchmarks
# ---------------------------------------------------------------------------
# Worker process executable (used by the multiprocess worker-death proof).
add_executable(nvlink_worker tools/nvlink_worker.cpp)
target_link_libraries(nvlink_worker PRIVATE nvlinkfabric Threads::Threads ws2_32)
target_compile_options(nvlink_worker PRIVATE ${NVF_STATIC_WARNINGS})

if(NVF_BUILD_TESTS)
  find_package(Threads REQUIRED)
  enable_testing()
  add_subdirectory(tests)
endif()

if(NVF_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(NVF_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

# ---------------------------------------------------------------------------
# Install + package config
# ---------------------------------------------------------------------------
if(NVF_INSTALL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  install(TARGETS nvlinkfabric
    EXPORT NVLinkFabricTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  if(TARGET nvlinkfabric_nvidia)
    install(TARGETS nvlinkfabric_nvidia
      EXPORT NVLinkFabricTargets
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
      INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  endif()

  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  install(EXPORT NVLinkFabricTargets
    FILE NVLinkFabricTargets.cmake
    NAMESPACE nvlinkfabric::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NVLinkFabric)

  configure_package_config_file(
    cmake/NVLinkFabricConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/NVLinkFabricConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NVLinkFabric)

  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/NVLinkFabricConfigVersion.cmake
    VERSION 1.0.0
    COMPATIBILITY SameMajorVersion)

  install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/NVLinkFabricConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/NVLinkFabricConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NVLinkFabric)
endif()
