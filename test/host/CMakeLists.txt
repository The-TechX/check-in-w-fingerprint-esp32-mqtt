cmake_minimum_required(VERSION 3.16)
project(host_tests C)

enable_testing()

add_executable(run_host_tests
    src/test_main.c
    src/test_use_cases.c
    ../../components/application/src/use_cases.c
    ../../components/domain/src/domain_models.c
)

target_include_directories(run_host_tests PRIVATE
    include
    ../../components/application/include
    ../../components/domain/include
)

add_test(NAME run_host_tests COMMAND run_host_tests)
