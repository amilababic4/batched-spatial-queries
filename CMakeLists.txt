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

option(P07_PROFILE_MARKERS "Oznaci glavne faze benchmarka funkcijama za profiler" OFF)

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
  if(P07_PROFILE_MARKERS)
    target_compile_definitions(${name} PRIVATE P07_PROFILE_MARKERS)
  endif()
endforeach()

# psi_seq: sve u jednoj niti (i Parlay unutar biblioteke)
target_compile_definitions(psi_seq PRIVATE P07_SERIAL PARLAY_SEQUENTIAL)

# Sekvencijalna varijanta sa ponovnom upotrebom bafera za PSI rezultate.
add_executable(psi_seq_opt src/psi_benchmark.cpp)
target_include_directories(psi_seq_opt PRIVATE src)
target_link_libraries(psi_seq_opt PRIVATE PSI::PSI Threads::Threads)
target_compile_options(psi_seq_opt PRIVATE -O3)
target_compile_definitions(psi_seq_opt PRIVATE P07_SERIAL PARLAY_SEQUENTIAL P07_SEQ_BUFFER)
if(P07_PROFILE_MARKERS)
  target_compile_definitions(psi_seq_opt PRIVATE P07_PROFILE_MARKERS)
endif()

# psi_omp: nasa OpenMP petlja; Parlay iskljucen da se dva rasporedjivaca ne mijesaju
target_compile_definitions(psi_omp PRIVATE P07_OPENMP PARLAY_SEQUENTIAL)
target_link_libraries(psi_omp PRIVATE OpenMP::OpenMP_CXX)

# Testovi ispravnosti (docs/plan-testiranja.md): ./build/testovi ili ctest --test-dir build
add_executable(testovi src/testovi.cpp)
target_include_directories(testovi PRIVATE src)
target_link_libraries(testovi PRIVATE PSI::PSI Threads::Threads OpenMP::OpenMP_CXX)
target_compile_options(testovi PRIVATE -O2)
enable_testing()
add_test(NAME testovi COMMAND testovi)

add_executable(testovi_psi_seq_opt src/testovi_psi_seq_opt.cpp)
target_include_directories(testovi_psi_seq_opt PRIVATE src)
target_link_libraries(testovi_psi_seq_opt PRIVATE PSI::PSI Threads::Threads)
target_compile_options(testovi_psi_seq_opt PRIVATE -O2)
target_compile_definitions(testovi_psi_seq_opt PRIVATE PARLAY_SEQUENTIAL)
add_test(NAME psi_seq_opt_helper COMMAND testovi_psi_seq_opt)
add_test(NAME psi_seq_opt_cli COMMAND ${CMAKE_COMMAND}
  -DPROGRAM=$<TARGET_FILE:psi_seq_opt> -DN=1000 -DM=30
  -P ${CMAKE_CURRENT_SOURCE_DIR}/src/test_psi_seq_cli.cmake)
add_test(NAME psi_seq_opt_prazno_cli COMMAND ${CMAKE_COMMAND}
  -DPROGRAM=$<TARGET_FILE:psi_seq_opt> -DN=0 -DM=0
  -P ${CMAKE_CURRENT_SOURCE_DIR}/src/test_psi_seq_cli.cmake)
