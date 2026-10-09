cmake_minimum_required(VERSION 3.16)
project(inteiros_gigantes VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(bigint STATIC src/bigint.c)
target_include_directories(bigint PUBLIC include)

add_library(calc STATIC src/calc.c)
target_include_directories(calc PUBLIC src)
target_link_libraries(calc PUBLIC bigint)

add_executable(calc-gigante src/main.c)
target_link_libraries(calc-gigante PRIVATE calc)

add_executable(bench_karatsuba tests/bench_karatsuba.c)
target_link_libraries(bench_karatsuba PRIVATE bigint)

enable_testing()

add_executable(test_bigint tests/test_bigint.c)
target_link_libraries(test_bigint PRIVATE calc)
add_test(NAME unidade COMMAND test_bigint)

add_executable(oraculo tests/oraculo.c)
target_link_libraries(oraculo PRIVATE calc)
find_program(PYTHON3 python3)
if(PYTHON3)
  add_test(NAME diferencial
    COMMAND ${PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/tests/diferencial.py $<TARGET_FILE:oraculo>)
endif()

add_test(NAME linha_de_comando
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli.sh $<TARGET_FILE:calc-gigante>)
