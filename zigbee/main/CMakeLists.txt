idf_component_register(
    SRCS
        "hisense_ac_zigbee.c"
        "ac_logic.c"
        "hisense_codec.c"
        "commissioning.c"
        "commissioning_policy.c"
        "double_reset.c"
        "ir_tx.c"
        "state_storage.c"
        "zcl_utility.c"
        "zigbee_device.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_driver_gpio esp_driver_rmt
)
