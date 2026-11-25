idf_component_register(
    SRCS
        "airplay_receiver.c"
        "core/rtsp.c"
        "core/handlers.c"
        "core/pairing.c"
        "core/fairplay.c"
        "core/mdns.c"
        "core/mirror.c"
        "core/audio.c"
        "core/keystream.c"
        "core/h264_packet.c"
        "core/playfair/playfair.c"
        "core/playfair/omg_hax.c"
        "core/playfair/sap_hash.c"
        "core/playfair/hand_garble.c"
        "core/playfair/modified_md5.c"
        "bplist/bplist.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "core" "bplist"
    REQUIRES esp_common
    PRIV_REQUIRES esp_timer esp_hw_support heap lwip freertos mbedtls nvs_flash libsodium mdns
)

# PlayFair is inherited C code containing intentional word reinterpretation.
set_source_files_properties(
    "core/playfair/playfair.c" "core/playfair/omg_hax.c"
    "core/playfair/sap_hash.c" "core/playfair/hand_garble.c"
    "core/playfair/modified_md5.c"
    PROPERTIES COMPILE_OPTIONS "-fno-strict-aliasing;-Wno-unused-variable"
)
