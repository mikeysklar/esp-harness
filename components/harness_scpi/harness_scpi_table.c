#include "scpi/scpi.h"
#include "harness_scpi_commands.h"
#include "harness_gpio_scpi.h"
#include "harness_bus_scpi.h"
#include "harness_dut_scpi.h"

static scpi_result_t syst_chip_q(scpi_t *ctx)    { SCPI_ResultMnemonic(ctx, "ESP32-P4"); return SCPI_RES_OK; }
static scpi_result_t syst_serial_q(scpi_t *ctx);
static scpi_result_t syst_free_q(scpi_t *ctx);
static scpi_result_t syst_uptime_q(scpi_t *ctx);
static scpi_result_t syst_reboot(scpi_t *ctx);
static scpi_result_t self_test_q(scpi_t *ctx)    { SCPI_ResultInt32(ctx, 0); return SCPI_RES_OK; }

#include <stdio.h>
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_system.h"

static scpi_result_t syst_serial_q(scpi_t *ctx)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BASE);
    char s[13];
    snprintf(s, sizeof(s), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    SCPI_ResultText(ctx, s);
    return SCPI_RES_OK;
}

static scpi_result_t syst_free_q(scpi_t *ctx)
{
    SCPI_ResultUInt32(ctx, (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    SCPI_ResultUInt32(ctx, (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return SCPI_RES_OK;
}

static scpi_result_t syst_uptime_q(scpi_t *ctx)
{
    SCPI_ResultUInt64(ctx, (uint64_t)(esp_timer_get_time() / 1000));
    return SCPI_RES_OK;
}

static scpi_result_t syst_reboot(scpi_t *ctx)
{
    (void)ctx;
    esp_restart();
    return SCPI_RES_OK;
}

static const scpi_command_t kCommands[] = {
    /* IEEE 488.2 mandatory */
    { .pattern = "*CLS",            .callback = SCPI_CoreCls,    },
    { .pattern = "*ESE",            .callback = SCPI_CoreEse,    },
    { .pattern = "*ESE?",           .callback = SCPI_CoreEseQ,   },
    { .pattern = "*ESR?",           .callback = SCPI_CoreEsrQ,   },
    { .pattern = "*IDN?",           .callback = SCPI_CoreIdnQ,   },
    { .pattern = "*OPC",            .callback = SCPI_CoreOpc,    },
    { .pattern = "*OPC?",           .callback = SCPI_CoreOpcQ,   },
    { .pattern = "*RST",            .callback = SCPI_CoreRst,    },
    { .pattern = "*SRE",            .callback = SCPI_CoreSre,    },
    { .pattern = "*SRE?",           .callback = SCPI_CoreSreQ,   },
    { .pattern = "*STB?",           .callback = SCPI_CoreStbQ,   },
    { .pattern = "*TST?",           .callback = self_test_q,     },
    { .pattern = "*WAI",            .callback = SCPI_CoreWai,    },

    /* SCPI-required */
    { .pattern = "SYSTem:ERRor[:NEXT]?",        .callback = SCPI_SystemErrorNextQ,  },
    { .pattern = "SYSTem:ERRor:COUNt?",         .callback = SCPI_SystemErrorCountQ, },
    { .pattern = "SYSTem:VERSion?",             .callback = SCPI_SystemVersionQ,    },
    { .pattern = "STATus:QUEStionable[:EVENt]?",.callback = SCPI_StatusQuestionableEventQ, },
    { .pattern = "STATus:QUEStionable:ENABle",  .callback = SCPI_StatusQuestionableEnable, },
    { .pattern = "STATus:QUEStionable:ENABle?", .callback = SCPI_StatusQuestionableEnableQ, },
    { .pattern = "STATus:PRESet",               .callback = SCPI_StatusPreset, },

    /* Vendor: SYSTem extensions */
    { .pattern = "SYSTem:CHIP?",    .callback = syst_chip_q,     },
    { .pattern = "SYSTem:SERial?",  .callback = syst_serial_q,   },
    { .pattern = "SYSTem:FREE?",    .callback = syst_free_q,     },
    { .pattern = "SYSTem:UPTime?",  .callback = syst_uptime_q,   },
    { .pattern = "SYSTem:REBoot",   .callback = syst_reboot,     },

    /* Vendor: GPIO subsystem */
    { .pattern = "GPIO:DIR",        .callback = harness_gpio_scpi_dir,     },
    { .pattern = "GPIO:DIR?",       .callback = harness_gpio_scpi_dir_q,   },
    { .pattern = "GPIO:PULL",       .callback = harness_gpio_scpi_pull,    },
    { .pattern = "GPIO:WRITe",      .callback = harness_gpio_scpi_write,   },
    { .pattern = "GPIO:READ?",      .callback = harness_gpio_scpi_read_q,  },
    { .pattern = "GPIO:TOGGle",     .callback = harness_gpio_scpi_toggle,  },
    { .pattern = "GPIO:PULse",      .callback = harness_gpio_scpi_pulse,   },

    /* Vendor: BUS:I2C controller */
    { .pattern = "BUS:I2C:CONT:INIT",   .callback = harness_bus_scpi_i2c_cont_init,   },
    { .pattern = "BUS:I2C:CONT:DEINit", .callback = harness_bus_scpi_i2c_cont_deinit, },
    { .pattern = "BUS:I2C:CONT:SCAN?",  .callback = harness_bus_scpi_i2c_cont_scan_q, },
    { .pattern = "BUS:I2C:CONT:WRITe",  .callback = harness_bus_scpi_i2c_cont_write,  },
    { .pattern = "BUS:I2C:CONT:READ?",  .callback = harness_bus_scpi_i2c_cont_read_q, },
    { .pattern = "BUS:I2C:CONT:XFER?",  .callback = harness_bus_scpi_i2c_cont_xfer_q, },

    /* Vendor: BUS:I2C target */
    { .pattern = "BUS:I2C:TARG:INIT",   .callback = harness_bus_scpi_i2c_targ_init,   },
    { .pattern = "BUS:I2C:TARG:DEINit", .callback = harness_bus_scpi_i2c_targ_deinit, },
    { .pattern = "BUS:I2C:TARG:WRITe",  .callback = harness_bus_scpi_i2c_targ_write,  },
    { .pattern = "BUS:I2C:TARG:READ?",  .callback = harness_bus_scpi_i2c_targ_read_q, },
    { .pattern = "BUS:I2C:TARG:STATe?", .callback = harness_bus_scpi_i2c_targ_state_q,},

    /* Vendor: BUS:SPI controller */
    { .pattern = "BUS:SPI:CONT:INIT",   .callback = harness_bus_scpi_spi_cont_init,   },
    { .pattern = "BUS:SPI:CONT:DEINit", .callback = harness_bus_scpi_spi_cont_deinit, },
    { .pattern = "BUS:SPI:CONT:XFER?",  .callback = harness_bus_scpi_spi_cont_xfer_q, },
    { .pattern = "BUS:SPI:CONT:WRITe",  .callback = harness_bus_scpi_spi_cont_write,  },
    { .pattern = "BUS:SPI:CONT:CS",     .callback = harness_bus_scpi_spi_cont_cs,     },

    /* Vendor: BUS:SPI target */
    { .pattern = "BUS:SPI:TARG:INIT",   .callback = harness_bus_scpi_spi_targ_init,   },
    { .pattern = "BUS:SPI:TARG:DEINit", .callback = harness_bus_scpi_spi_targ_deinit, },
    { .pattern = "BUS:SPI:TARG:XFER?",  .callback = harness_bus_scpi_spi_targ_xfer_q, },

    /* Vendor: BUS:UART (target-side) */
    { .pattern = "BUS:UART:INIT",   .callback = harness_bus_scpi_uart_init,   },
    { .pattern = "BUS:UART:DEINit", .callback = harness_bus_scpi_uart_deinit, },
    { .pattern = "BUS:UART:WRITe",  .callback = harness_bus_scpi_uart_write,  },
    { .pattern = "BUS:UART:READ?",  .callback = harness_bus_scpi_uart_read_q, },
    { .pattern = "BUS:UART:DRAin",  .callback = harness_bus_scpi_uart_drain,  },

    /* Vendor: DUT metadata (NVS-backed) */
    { .pattern = "DUT:NAME",        .callback = harness_dut_scpi_name,       },
    { .pattern = "DUT:NAME?",       .callback = harness_dut_scpi_name_q,     },
    { .pattern = "DUT:NOTE",        .callback = harness_dut_scpi_note,       },
    { .pattern = "DUT:NOTE?",       .callback = harness_dut_scpi_note_q,     },
    { .pattern = "DUT:PIN",         .callback = harness_dut_scpi_pin,        },
    { .pattern = "DUT:PIN?",        .callback = harness_dut_scpi_pin_q,      },
    { .pattern = "DUT:PIN:DELete",  .callback = harness_dut_scpi_pin_del,    },
    { .pattern = "DUT:PIN:LIST?",   .callback = harness_dut_scpi_pin_list_q, },
    { .pattern = "DUT:WIRE",        .callback = harness_dut_scpi_wire,       },
    { .pattern = "DUT:WIRE:DELete", .callback = harness_dut_scpi_wire_del,   },
    { .pattern = "DUT:WIRE:LIST?",  .callback = harness_dut_scpi_wire_list_q,},
    { .pattern = "DUT:CLEar",       .callback = harness_dut_scpi_clear,      },

    SCPI_CMD_LIST_END
};

const scpi_command_t *harness_scpi_command_table(void)
{
    return kCommands;
}

void harness_scpi_handle_reset(void)
{
    harness_gpio_scpi_reset();
    harness_bus_scpi_reset();
    /* DUT state is intentionally persistent across *RST. */
}
