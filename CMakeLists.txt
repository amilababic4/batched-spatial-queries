cmake_minimum_required(VERSION 3.15)
project(P07 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# PSI biblioteka (git submodule u reference/psi) - bez njenih testova i primjera
set(PSI_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(PSI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PSI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(reference/psi)

find_package(Threads REQUIRED)
find_package(OpenMP REQUIRED)

# Brute force programi (nas postojeci kod)
foreach(name brute_force brute_force_ViseUpita)
  add_executable(${name} src/${name}.cpp)
  target_compile_options(${name} PRIVATE -O3)
endforeach()

# Tri varijante KD-tree programa iz istog izvornog fajla
foreach(name psi_seq psi_parlay psi_omp)
  add_executable(${name} src/psi_benchmark.cpp)
  target_include_directories(${name} PRIVATE src)
  target_link_libraries(${name} PRIVATE PSI::PSI Threads::Threads)
  target_compile_options(${name} PRIVATE -O3)
endforeach()

# psi_seq: sve u jednoj niti (i Parlay unutar biblioteke)
target_compile_definitions(psi_seq PRIVATE P07_SERIAL PARLAY_SEQUENTIAL)

# psi_omp: nasa OpenMP petlja; Parlay iskljucen da se dva rasporedjivaca ne mijesaju
target_compile_definitions(psi_omp PRIVATE P07_OPENMP PARLAY_SEQUENTIAL)
target_link_libraries(psi_omp PRIVATE OpenMP::OpenMP_CXX)
