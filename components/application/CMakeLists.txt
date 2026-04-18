idf_component_register(
    SRCS "src/app_controller.c" "src/use_cases.c"
    INCLUDE_DIRS "include"
    REQUIRES domain infrastructure as608 platform esp_driver_gpio
)
