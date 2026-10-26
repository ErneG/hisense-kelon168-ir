#pragma once

#include "esp_zigbee_core.h"
#include "sdkconfig.h"

#define HAC_IR_GPIO                          CONFIG_HAC_IR_GPIO
#ifdef CONFIG_HAC_IR_INVERT
#define HAC_IR_INVERT                        1
#else
#define HAC_IR_INVERT                        0
#endif

#define HAC_ENDPOINT                         1
#define HAC_SETPOINT_MIN_CENTI               1600
#define HAC_SETPOINT_MAX_CENTI               3000
#define HAC_IR_RESOLUTION_HZ                 1000000U
#define HAC_IR_FRAME_GAP_MS                  400U
#define HAC_IR_QUEUE_LEN                     24U

#define HAC_PRIMARY_CHANNEL_MASK             ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK
#define HAC_MIN_JOIN_LQI                     0U
#define HAC_MAX_CHILDREN                     10U

#define HAC_ENABLE_ACTIVE_SCAN_DIAGNOSTIC    1
#define HAC_ACTIVE_SCAN_DURATION             4U
#define HAC_ENABLE_DOUBLE_RESET_FACTORY_RESET 1
#define HAC_DOUBLE_RESET_WINDOW_MS           4000U

/* BOOT button. Strapping pin: runtime long press only, no boot-time sample. */
#define HAC_FACTORY_RESET_GPIO               9
#define HAC_FACTORY_RESET_ACTIVE_LEVEL       0
#define HAC_FACTORY_RESET_HOLD_MS            5000U
#define HAC_FACTORY_RESET_POLL_MS            100U

#define HAC_MANUFACTURER_NAME "\x0A""ErnestsDIY"
#define HAC_MODEL_IDENTIFIER  "\x0D""hisense-ir-ac"

/* Manufacturer-specific cluster for the extra AC controls. No manufacturer code on writes. */
#define HAC_CLUSTER_ID                0xFC20U
#define HAC_ATTR_FAN_SPEED            0x0000U
#define HAC_ATTR_TURBO                0x0001U
#define HAC_ATTR_ECONOMY              0x0002U
#define HAC_ATTR_SLEEP                0x0003U
#define HAC_ATTR_VSWING               0x0004U
#define HAC_ATTR_HSWING               0x0005U
#define HAC_ATTR_DISPLAY              0x0006U
#define HAC_ATTR_RESEND               0x0010U
/* Setpoint write path (int16, 0.01 C). The stack's thermostat check rejects a cooling setpoint below the heating
 * setpoint (cool >= heat + dead band, even with dead band 0), so Z2M writes setpoints here instead. */
#define HAC_ATTR_HEAT_SETPOINT        0x0020U
#define HAC_ATTR_COOL_SETPOINT        0x0021U

#define HAC_ZR_CONFIG()                                           \
    {                                                             \
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ROUTER,                 \
        .install_code_policy = false,                             \
        .nwk_cfg.zczr_cfg = {                                     \
            .max_children = HAC_MAX_CHILDREN,                     \
        },                                                        \
    }

#define HAC_DEFAULT_RADIO_CONFIG()                \
    {                                             \
        .radio_mode = ZB_RADIO_MODE_NATIVE,       \
    }

#define HAC_DEFAULT_HOST_CONFIG()                 \
    {                                             \
        .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE, \
    }
