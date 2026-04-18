idf_component_register(
    SRCS "src/webui_server.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server application domain
)
