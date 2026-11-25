idf_component_register(SRCS "main.c" "app_network.c" "native_ui.c"
                       INCLUDE_DIRS "."
                       REQUIRES display_board video_decoder airplay_receiver input_bridge
                                esp_wifi esp_netif esp_http_server nvs_flash esp_hosted esp_timer)
