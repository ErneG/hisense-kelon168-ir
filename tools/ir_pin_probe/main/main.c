#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"

#define RES_HZ 1000000

static const int pins[] = {0,1,2,3,4,5,6,7,8,14,15,16,17,18,19,20,21,22,23};

static void put(rmt_symbol_word_t *s, int *n, int on, int off)
{
    s[*n].level0 = 1;
    s[*n].duration0 = on;
    s[*n].level1 = 0;
    s[*n].duration1 = off;
    (*n)++;
}

static int build(rmt_symbol_word_t *s, uint8_t addr, uint8_t cmd)
{
    int n = 0;
    put(s, &n, 9000, 4500);
    uint32_t d = addr | ((uint32_t)(uint8_t)~addr << 8) | ((uint32_t)cmd << 16) | ((uint32_t)(uint8_t)~cmd << 24);
    for (int i = 0; i < 32; i++) {
        put(s, &n, 560, (d >> i) & 1 ? 1690 : 560);
    }
    put(s, &n, 560, 0x7fff);
    return n;
}

static void probe(int pin, int inv, uint8_t cmd)
{
    rmt_channel_handle_t ch = NULL;
    rmt_tx_channel_config_t cfg = {
        .gpio_num = pin,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RES_HZ,
        .mem_block_symbols = 48,
        .trans_queue_depth = 2,
        .flags.invert_out = inv,
    };
    if (rmt_new_tx_channel(&cfg, &ch) != ESP_OK) {
        printf("PROBE gpio=%d inv=%d CHANNEL_FAIL\n", pin, inv);
        return;
    }
    rmt_carrier_config_t car = {
        .frequency_hz = 38000,
        .duty_cycle = 0.33f,
    };
    rmt_apply_carrier(ch, &car);
    rmt_encoder_handle_t enc = NULL;
    rmt_copy_encoder_config_t ec = {};
    rmt_new_copy_encoder(&ec, &enc);
    rmt_enable(ch);

    static rmt_symbol_word_t syms[40];
    int n = build(syms, (uint8_t)pin, cmd);
    rmt_transmit_config_t tc = { .loop_count = 0 };
    for (int i = 0; i < 3; i++) {
        printf("PROBE gpio=%d inv=%d\n", pin, inv);
        rmt_transmit(ch, enc, syms, n * sizeof(rmt_symbol_word_t), &tc);
        rmt_tx_wait_all_done(ch, 1000);
        vTaskDelay(pdMS_TO_TICKS(110));
    }
    rmt_disable(ch);
    rmt_del_encoder(enc);
    rmt_del_channel(ch);
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    vTaskDelay(pdMS_TO_TICKS(300));
}

void app_main(void)
{
    printf("PROBE_START\n");
    while (1) {
        for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
            probe(pins[i], 0, 0x5A);
            probe(pins[i], 1, 0xA5);
        }
        printf("PROBE_CYCLE_DONE\n");
    }
}
