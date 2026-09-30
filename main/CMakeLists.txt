idf_component_register(
    SRCS "main.c" "csi_scope.c" "csi_model.c" "csi_ui.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_event esp_hw_support esp_netif esp_timer esp_wifi nvs_flash esp_http_server json lwip
    EMBED_TXTFILES "provision.html"
)
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/csi_han_12.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/csi_han_14.c"
)
