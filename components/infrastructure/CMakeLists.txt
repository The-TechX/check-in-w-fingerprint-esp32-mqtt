idf_component_register(
    SRCS "src/config_repository_nvs.c" "src/offline_queue_nvs.c" "src/mqtt_adapter_stub.c" "src/network_manager_stub.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_event mqtt domain
)
