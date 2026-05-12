#include <string.h>
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"

#include "scpi/scpi.h"
#include "harness_gpio_scpi.h"
#include "harness_dut.h"

static const char *TAG = "gpio";

#define GPIO_MIN 0
#define GPIO_MAX 54

/* Track which pins we've claimed so *RST can release them. */
static uint64_t s_owned_mask;

static void mark_owned(int p)   { s_owned_mask |=  (1ULL << p); }
static void mark_unowned(int p) { s_owned_mask &= ~(1ULL << p); }

/* GPIO operations require an actual GPIO; -1 ("unused") is rejected here even
 * though harness_dut_resolve_pin would pass it through. */
static bool resolve_real_pin(scpi_t *ctx, int *out)
{
    if (!harness_dut_resolve_pin(ctx, out)) return false;
    if (*out < GPIO_MIN || *out > GPIO_MAX) {
        SCPI_ErrorPush(ctx, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return false;
    }
    return true;
}

static const scpi_choice_def_t k_dir_opts[] = {
    {"IN",     0},
    {"OUT",    1},
    {"INOUT",  2},
    {"OFF",    3},
    SCPI_CHOICE_LIST_END
};

static const scpi_choice_def_t k_pull_opts[] = {
    {"NONE",   0},
    {"UP",     1},
    {"DOWN",   2},
    {"UPDOWN", 3},
    SCPI_CHOICE_LIST_END
};

static gpio_mode_t dir_to_mode(int32_t dir)
{
    switch (dir) {
        case 0: return GPIO_MODE_INPUT;
        case 1: return GPIO_MODE_OUTPUT;
        case 2: return GPIO_MODE_INPUT_OUTPUT;
        default: return GPIO_MODE_DISABLE;
    }
}

scpi_result_t harness_gpio_scpi_dir(scpi_t *ctx)
{
    int gpio;
    int32_t dir;
    if (!resolve_real_pin(ctx, &gpio)) return SCPI_RES_ERR;
    if (!SCPI_ParamChoice(ctx, k_dir_opts, &dir, TRUE)) return SCPI_RES_ERR;

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = dir_to_mode(dir),
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&cfg) != ESP_OK) {
        SCPI_ErrorPush(ctx, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }
    if (dir == 3) mark_unowned(gpio); else mark_owned(gpio);
    return SCPI_RES_OK;
}

scpi_result_t harness_gpio_scpi_dir_q(scpi_t *ctx)
{
    int gpio;
    if (!resolve_real_pin(ctx, &gpio)) return SCPI_RES_ERR;
    /* ESP-IDF doesn't expose a portable getter; report owned/unowned only. */
    if (s_owned_mask & (1ULL << gpio)) SCPI_ResultMnemonic(ctx, "OWNED");
    else                                SCPI_ResultMnemonic(ctx, "OFF");
    return SCPI_RES_OK;
}

scpi_result_t harness_gpio_scpi_pull(scpi_t *ctx)
{
    int gpio;
    int32_t pull;
    if (!resolve_real_pin(ctx, &gpio)) return SCPI_RES_ERR;
    if (!SCPI_ParamChoice(ctx, k_pull_opts, &pull, TRUE)) return SCPI_RES_ERR;
    gpio_pull_mode_t mode;
    switch (pull) {
        case 0: mode = GPIO_FLOATING;    break;
        case 1: mode = GPIO_PULLUP_ONLY; break;
        case 2: mode = GPIO_PULLDOWN_ONLY; break;
        case 3: mode = GPIO_PULLUP_PULLDOWN; break;
        default: return SCPI_RES_ERR;
    }
    if (gpio_set_pull_mode(gpio, mode) != ESP_OK) {
        SCPI_ErrorPush(ctx, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

scpi_result_t harness_gpio_scpi_write(scpi_t *ctx)
{
    int gpio;
    scpi_bool_t level;
    if (!resolve_real_pin(ctx, &gpio)) return SCPI_RES_ERR;
    if (!SCPI_ParamBool(ctx, &level, TRUE)) return SCPI_RES_ERR;
    if (gpio_set_level(gpio, level ? 1 : 0) != ESP_OK) {
        SCPI_ErrorPush(ctx, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

scpi_result_t harness_gpio_scpi_read_q(scpi_t *ctx)
{
    int gpio;
    if (!resolve_real_pin(ctx, &gpio)) return SCPI_RES_ERR;
    SCPI_ResultInt32(ctx, gpio_get_level(gpio));
    return SCPI_RES_OK;
}

scpi_result_t harness_gpio_scpi_toggle(scpi_t *ctx)
{
    int gpio;
    if (!resolve_real_pin(ctx, &gpio)) return SCPI_RES_ERR;
    int cur = gpio_get_level(gpio);
    gpio_set_level(gpio, cur ? 0 : 1);
    return SCPI_RES_OK;
}

scpi_result_t harness_gpio_scpi_pulse(scpi_t *ctx)
{
    int gpio;
    uint32_t us;
    if (!resolve_real_pin(ctx, &gpio)) return SCPI_RES_ERR;
    if (!SCPI_ParamUInt32(ctx, &us, TRUE)) return SCPI_RES_ERR;
    gpio_set_level(gpio, 1);
    esp_rom_delay_us(us);
    gpio_set_level(gpio, 0);
    return SCPI_RES_OK;
}

void harness_gpio_scpi_reset(void)
{
    for (int p = GPIO_MIN; p <= GPIO_MAX; p++) {
        if (s_owned_mask & (1ULL << p)) {
            gpio_reset_pin(p);
        }
    }
    s_owned_mask = 0;
    ESP_LOGI(TAG, "*RST: released all GPIOs");
}
