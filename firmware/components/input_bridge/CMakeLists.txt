idf_component_register(SRCS "input_core.c" "input_ble.c" "input_tx_queue.c" INCLUDE_DIRS "include"
                       REQUIRES bt esp_hosted nvs_flash esp_timer)

# Opt-in hardware diagnostic; it prints return codes, never pairing keys.
if(P4_BLE_CRYPTO_PROBE)
    target_sources(${COMPONENT_LIB} PRIVATE "${PROJECT_DIR}/../tests/on_device/ble_crypto_probe.c")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE P4_BLE_CRYPTO_PROBE=1)
    target_link_options(${COMPONENT_LIB} INTERFACE
        "-Wl,--wrap=ble_sm_alg_gen_key_pair" "-Wl,--wrap=ble_sm_alg_gen_dhkey"
        "-Wl,--wrap=ble_hs_hci_util_rand")
endif()
