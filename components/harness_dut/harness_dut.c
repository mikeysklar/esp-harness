#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "scpi/scpi.h"
#include "harness_dut.h"
#include "harness_dut_scpi.h"
#include "board_pins.h"

static const char *TAG = "dut";
static const char *NVS_NS = "harness_dut";

/*
 * Pin mappings are compile-time constants set per board in board_pins.h.
 * Wire mappings are dynamic and stored in NVS so the harness wiring can be
 * documented / reconfigured without a firmware rebuild.
 */

/* ------------------------------------------------------------------
 * Wire record (NVS-backed)
 * ------------------------------------------------------------------ */

typedef struct {
    char dut[HARNESS_DUT_MAX_LABEL_LEN + 1];
    char host[HARNESS_DUT_MAX_LABEL_LEN + 1];
} wire_t;

/* ------------------------------------------------------------------
 * Static state
 * ------------------------------------------------------------------ */

static struct {
    char        name[HARNESS_DUT_MAX_NAME_LEN + 1];
    char        note[HARNESS_DUT_MAX_NOTE_LEN + 1];
    wire_t      wires[HARNESS_DUT_MAX_WIRES];
    size_t      wire_count;
} s;

/* The compile-time pin table, loaded once at init. */
static const board_pin_t *s_board_pins = NULL;
static size_t             s_board_pin_count = 0;

/* ------------------------------------------------------------------
 * NVS helpers
 * ------------------------------------------------------------------ */

static esp_err_t nvs_open_rw(nvs_handle_t *h)
{
    return nvs_open(NVS_NS, NVS_READWRITE, h);
}

/* NVS keys are limited to 15 chars. */
static void wire_key(char out[16], size_t i)
{
    snprintf(out, 16, "w%u", (unsigned)i);
}

static esp_err_t save_str(const char *key, const char *value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open_rw(&h);
    if (err) return err;
    err = nvs_set_str(h, key, value);
    if (!err) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static esp_err_t save_blob(const char *key, const void *data, size_t len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open_rw(&h);
    if (err) return err;
    err = nvs_set_blob(h, key, data, len);
    if (!err) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static esp_err_t erase_key(const char *key)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open_rw(&h);
    if (err) return err;
    err = nvs_erase_key(h, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (!err) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static void load_wires(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;

    size_t len = sizeof(s.name);
    if (nvs_get_str(h, "name", s.name, &len) != ESP_OK) s.name[0] = '\0';
    len = sizeof(s.note);
    if (nvs_get_str(h, "note", s.note, &len) != ESP_OK) s.note[0] = '\0';

    uint8_t count = 0;
    if (nvs_get_u8(h, "wire_n", &count) == ESP_OK && count <= HARNESS_DUT_MAX_WIRES) {
        for (size_t i = 0; i < count; i++) {
            char k[16]; wire_key(k, i);
            size_t bl = sizeof(wire_t);
            if (nvs_get_blob(h, k, &s.wires[s.wire_count], &bl) == ESP_OK) {
                s.wire_count++;
            }
        }
    }
    nvs_close(h);
    ESP_LOGI(TAG, "board='%s', name='%s', %u wires, %u board pins",
             board_get_name(), s.name,
             (unsigned)s.wire_count, (unsigned)s_board_pin_count);
}

static esp_err_t save_wires_count(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open_rw(&h);
    if (err) return err;
    err = nvs_set_u8(h, "wire_n", (uint8_t)s.wire_count);
    if (!err) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

/* ------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------ */

esp_err_t harness_dut_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err) return err;

    /* Load the compile-time pin table. */
    s_board_pins = board_get_pins(&s_board_pin_count);

    /* Load wires and metadata from NVS. */
    load_wires();
    return ESP_OK;
}

bool harness_dut_pin_lookup(const char *label, int *gpio_out)
{
    for (size_t i = 0; i < s_board_pin_count; i++) {
        if (strcmp(s_board_pins[i].label, label) == 0) {
            if (gpio_out) *gpio_out = s_board_pins[i].gpio;
            return true;
        }
    }
    return false;
}

bool harness_dut_resolve_pin(scpi_t *ctx, int *gpio_out)
{
    scpi_parameter_t p;
    if (!SCPI_Parameter(ctx, &p, TRUE)) return false;

    if (SCPI_ParamIsNumber(&p, FALSE)) {
        int32_t v;
        if (!SCPI_ParamToInt32(ctx, &p, &v)) return false;
        *gpio_out = (int)v;
        return true;
    }

    /* Non-numeric: treat as a string label. The parser keeps the surrounding
     * quotes around STRING tokens, so strip one pair before lookup. */
    char label[HARNESS_DUT_MAX_LABEL_LEN + 1];
    size_t n = p.len < sizeof(label) - 1 ? p.len : sizeof(label) - 1;
    const char *src = p.ptr;
    if (n >= 2 && src[0] == '"' && src[n - 1] == '"') { src++; n -= 2; }
    memcpy(label, src, n);
    label[n] = '\0';

    int gpio;
    if (!harness_dut_pin_lookup(label, &gpio)) {
        SCPI_ErrorPush(ctx, SCPI_ERROR_HEADER_SUFFIX_OUTOFRANGE);
        return false;
    }
    *gpio_out = gpio;
    return true;
}

/* ---- internal helpers used by SCPI handlers ---- */

static ssize_t wire_index(const char *dut_label)
{
    for (size_t i = 0; i < s.wire_count; i++) {
        if (strcmp(s.wires[i].dut, dut_label) == 0) return (ssize_t)i;
    }
    return -1;
}

static bool read_string(scpi_t *ctx, char *out, size_t out_len)
{
    size_t copied = 0;
    if (!SCPI_ParamCopyText(ctx, out, out_len, &copied, TRUE)) return false;
    if (copied >= out_len) {
        SCPI_ErrorPush(ctx, SCPI_ERROR_STRING_DATA_ERROR);
        return false;
    }
    return true;
}

/* ---- SCPI handlers: DUT name / note / wires ---- */

scpi_result_t harness_dut_scpi_name(scpi_t *ctx)
{
    char tmp[HARNESS_DUT_MAX_NAME_LEN + 1];
    if (!read_string(ctx, tmp, sizeof(tmp))) return SCPI_RES_ERR;
    strcpy(s.name, tmp);
    if (save_str("name", s.name) != ESP_OK) {
        SCPI_ErrorPush(ctx, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

scpi_result_t harness_dut_scpi_name_q(scpi_t *ctx)
{
    SCPI_ResultText(ctx, s.name);
    return SCPI_RES_OK;
}

scpi_result_t harness_dut_scpi_note(scpi_t *ctx)
{
    char tmp[HARNESS_DUT_MAX_NOTE_LEN + 1];
    if (!read_string(ctx, tmp, sizeof(tmp))) return SCPI_RES_ERR;
    strcpy(s.note, tmp);
    if (save_str("note", s.note) != ESP_OK) {
        SCPI_ErrorPush(ctx, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

scpi_result_t harness_dut_scpi_note_q(scpi_t *ctx)
{
    SCPI_ResultText(ctx, s.note);
    return SCPI_RES_OK;
}

scpi_result_t harness_dut_scpi_board_q(scpi_t *ctx)
{
    SCPI_ResultText(ctx, board_get_name());
    return SCPI_RES_OK;
}

scpi_result_t harness_dut_scpi_wire(scpi_t *ctx)
{
    char dut[HARNESS_DUT_MAX_LABEL_LEN + 1];
    char host[HARNESS_DUT_MAX_LABEL_LEN + 1];
    if (!read_string(ctx, dut, sizeof(dut))) return SCPI_RES_ERR;
    if (!read_string(ctx, host, sizeof(host))) return SCPI_RES_ERR;

    ssize_t i = wire_index(dut);
    if (i < 0) {
        if (s.wire_count >= HARNESS_DUT_MAX_WIRES) {
            SCPI_ErrorPush(ctx, SCPI_ERROR_QUEUE_OVERFLOW);
            return SCPI_RES_ERR;
        }
        i = (ssize_t)s.wire_count++;
    }
    strncpy(s.wires[i].dut, dut, sizeof(s.wires[i].dut) - 1);
    s.wires[i].dut[sizeof(s.wires[i].dut) - 1] = '\0';
    strncpy(s.wires[i].host, host, sizeof(s.wires[i].host) - 1);
    s.wires[i].host[sizeof(s.wires[i].host) - 1] = '\0';

    char k[16]; wire_key(k, (size_t)i);
    if (save_blob(k, &s.wires[i], sizeof(wire_t)) != ESP_OK ||
        save_wires_count() != ESP_OK) {
        SCPI_ErrorPush(ctx, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

scpi_result_t harness_dut_scpi_wire_del(scpi_t *ctx)
{
    char dut[HARNESS_DUT_MAX_LABEL_LEN + 1];
    if (!read_string(ctx, dut, sizeof(dut))) return SCPI_RES_ERR;
    ssize_t i = wire_index(dut);
    if (i < 0) return SCPI_RES_OK;

    s.wire_count--;
    if ((size_t)i != s.wire_count) s.wires[i] = s.wires[s.wire_count];

    char k[16];
    wire_key(k, s.wire_count);
    erase_key(k);
    if ((size_t)i != s.wire_count) {
        wire_key(k, (size_t)i);
        save_blob(k, &s.wires[i], sizeof(wire_t));
    }
    if (save_wires_count() != ESP_OK) {
        SCPI_ErrorPush(ctx, SCPI_ERROR_SYSTEM_ERROR);
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

scpi_result_t harness_dut_scpi_wire_list_q(scpi_t *ctx)
{
    for (size_t i = 0; i < s.wire_count; i++) {
        SCPI_ResultText(ctx, s.wires[i].dut);
        SCPI_ResultText(ctx, s.wires[i].host);
    }
    return SCPI_RES_OK;
}

scpi_result_t harness_dut_scpi_clear(scpi_t *ctx)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open_rw(&h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    memset(&s, 0, sizeof(s));
    return SCPI_RES_OK;
}
