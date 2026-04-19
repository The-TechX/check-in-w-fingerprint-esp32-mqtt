idf_component_register(
    SRCS "src/config_repository_nvs.c" "src/offline_queue_nvs.c" "src/websocket_transport.c" "src/network_manager_stub.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_event esp_netif esp_wifi esp_websocket_client esp_timer domain application
)
