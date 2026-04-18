idf_component_register(
    SRCS "src/as608_driver_stub.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_timer domain
)
