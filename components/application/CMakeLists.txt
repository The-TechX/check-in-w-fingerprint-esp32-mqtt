idf_component_register(
    SRCS "src/app_controller.c" "src/use_cases.c"
    INCLUDE_DIRS "include"
    REQUIRES domain infrastructure drivers_as608 platform
)
