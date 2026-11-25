idf_component_register(SRCS "input_core.c" "input_ble.c" "input_tx_queue.c" INCLUDE_DIRS "include"
                       REQUIRES bt esp_hosted nvs_flash esp_timer)
