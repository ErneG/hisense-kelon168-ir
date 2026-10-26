#include "ir_tx.h"

#include <string.h>

#include "ac_logic.h"
#include "app_config.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hisense_codec.h"
#include "state_storage.h"

static const char *TAG = "ir_tx";

#define HAC_IR_SYMBOLS ((HS_TIMINGS_COUNT + 1U) / 2U)
#define HAC_IR_TAIL_SPACE_US 1000U

typedef enum {
    HAC_IR_ITEM_FRAME = 0,
    HAC_IR_ITEM_SAVE,
} hac_ir_item_kind_t;

typedef struct {
    hac_ir_item_kind_t kind;
    union {
        struct {
            uint8_t cmd;
            uint8_t bytes[HS_FRAME_BYTES];
        } frame;
        struct {
            ac_state_t want;
            ac_state_t sent;
        } save;
    };
} hac_ir_item_t;

static QueueHandle_t s_queue;
static rmt_channel_handle_t s_channel;
static rmt_encoder_handle_t s_encoder;
static uint32_t s_timings[HS_TIMINGS_COUNT];
static rmt_symbol_word_t s_symbols[HAC_IR_SYMBOLS];

static esp_err_t transmit_frame(const uint8_t frame[HS_FRAME_BYTES])
{
    size_t n = hs_frame_to_timings(frame, s_timings, HS_TIMINGS_COUNT);
    size_t sym = 0U;
    rmt_transmit_config_t tx_config = {.loop_count = 0};

    ESP_RETURN_ON_FALSE(n == HS_TIMINGS_COUNT, ESP_FAIL, TAG, "Timing build failed");
    for (size_t i = 0; i < n; i += 2U) {
        uint32_t mark = s_timings[i];
        uint32_t space = (i + 1U < n) ? s_timings[i + 1U] : HAC_IR_TAIL_SPACE_US;

        s_symbols[sym++] = (rmt_symbol_word_t){
            .level0 = 1,
            .duration0 = mark,
            .level1 = 0,
            .duration1 = space,
        };
    }
    ESP_RETURN_ON_ERROR(rmt_transmit(s_channel, s_encoder, s_symbols, sym * sizeof(s_symbols[0]), &tx_config), TAG,
                        "rmt_transmit failed");
    return rmt_tx_wait_all_done(s_channel, 1000);
}

static void ir_task(void *arg)
{
    hac_ir_item_t item;

    (void)arg;
    for (;;) {
        if (xQueueReceive(s_queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (item.kind == HAC_IR_ITEM_SAVE) {
            esp_err_t err = hac_state_storage_save(&item.save.want, &item.save.sent);

            if (err != ESP_OK) {
                ESP_LOGW(TAG, "State save failed: %s", esp_err_to_name(err));
            }
            continue;
        }
        char hex[AC_FRAME_HEX_SIZE];

        ac_bytes_to_hex(item.frame.bytes, HS_FRAME_BYTES, hex, sizeof(hex));
        ESP_LOGI(TAG, "TX cmd 0x%02X frame=%s", item.frame.cmd, hex);
        if (transmit_frame(item.frame.bytes) != ESP_OK) {
            ESP_LOGW(TAG, "TX cmd 0x%02x failed", item.frame.cmd);
        }
        vTaskDelay(pdMS_TO_TICKS(HAC_IR_FRAME_GAP_MS));
    }
}

esp_err_t hac_ir_init(void)
{
    rmt_tx_channel_config_t channel_config = {
        .gpio_num = HAC_IR_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = HAC_IR_RESOLUTION_HZ,
        .mem_block_symbols = 48,
        .trans_queue_depth = 2,
        .flags.invert_out = HAC_IR_INVERT,
    };
    rmt_carrier_config_t carrier_config = {
        .frequency_hz = HS_CARRIER_HZ,
        .duty_cycle = HS_DUTY_PERCENT / 100.0f,
    };
    rmt_copy_encoder_config_t encoder_config = {};

    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&channel_config, &s_channel), TAG, "RMT channel");
    ESP_RETURN_ON_ERROR(rmt_apply_carrier(s_channel, &carrier_config), TAG, "RMT carrier");
    ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&encoder_config, &s_encoder), TAG, "RMT encoder");
    ESP_RETURN_ON_ERROR(rmt_enable(s_channel), TAG, "RMT enable");

    s_queue = xQueueCreate(HAC_IR_QUEUE_LEN, sizeof(hac_ir_item_t));
    ESP_RETURN_ON_FALSE(s_queue != NULL, ESP_ERR_NO_MEM, TAG, "Queue");
    ESP_RETURN_ON_FALSE(xTaskCreate(ir_task, "ir_tx", 4096, NULL, 4, NULL) == pdPASS, ESP_FAIL, TAG, "Task");
    ESP_LOGI(TAG, "IR TX ready on GPIO%d, %u Hz carrier, idle (nothing sent at boot)", HAC_IR_GPIO,
             (unsigned)HS_CARRIER_HZ);
    return ESP_OK;
}

esp_err_t hac_ir_enqueue(const ac_state_t *want, const uint8_t *cmds, size_t count)
{
    hs_state_t hs;
    hac_ir_item_t item;

    ESP_RETURN_ON_FALSE(s_queue != NULL, ESP_ERR_INVALID_STATE, TAG, "Not ready");
    ac_to_hs(want, &hs);
    for (size_t i = 0; i < count; ++i) {
        memset(&item, 0, sizeof(item));
        item.kind = HAC_IR_ITEM_FRAME;
        item.frame.cmd = cmds[i];
        ESP_RETURN_ON_FALSE(hs_build_frame(&hs, cmds[i], item.frame.bytes), ESP_ERR_INVALID_ARG, TAG,
                            "Invalid state for cmd 0x%02x", cmds[i]);
        ESP_RETURN_ON_FALSE(xQueueSend(s_queue, &item, 0) == pdTRUE, ESP_ERR_NO_MEM, TAG, "IR queue full");
    }
    return ESP_OK;
}

esp_err_t hac_ir_enqueue_save(const ac_state_t *want, const ac_state_t *sent)
{
    hac_ir_item_t item;

    ESP_RETURN_ON_FALSE(s_queue != NULL, ESP_ERR_INVALID_STATE, TAG, "Not ready");
    memset(&item, 0, sizeof(item));
    item.kind = HAC_IR_ITEM_SAVE;
    item.save.want = *want;
    item.save.sent = *sent;
    ESP_RETURN_ON_FALSE(xQueueSend(s_queue, &item, 0) == pdTRUE, ESP_ERR_NO_MEM, TAG, "IR queue full");
    return ESP_OK;
}
