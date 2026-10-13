from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import esp32, esp32_ble_tracker, spi
from esphome.const import CONF_ID

CODEOWNERS = ["@herbertmt978"]
DEPENDENCIES = ["spi"]

CONF_CE_PIN = "ce_pin"
CONF_BLE_TRACKER_ID = "ble_tracker_id"

norman_rf_monitor_ns = cg.esphome_ns.namespace("norman_rf_monitor")
NormanRfMonitor = norman_rf_monitor_ns.class_(
    "NormanRfMonitor", cg.Component, spi.SPIDevice
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(NormanRfMonitor),
            cv.Required(CONF_CE_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_BLE_TRACKER_ID): cv.use_id(esp32_ble_tracker.ESP32BLETracker),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(
        spi.spi_device_schema(
            default_data_rate="4MHz",
            default_mode="MODE0",
        )
    ),
)

FINAL_VALIDATE_SCHEMA = spi.final_validate_device_schema(
    "norman_rf_monitor", require_mosi=True, require_miso=True
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)
    ce_pin = await cg.gpio_pin_expression(config[CONF_CE_PIN])
    cg.add(var.set_ce_pin(ce_pin))
    if CONF_BLE_TRACKER_ID in config:
        cg.add_define("USE_NORMAN_RF_BLE_SCAN_PAUSE")
        esp32.add_extra_script("pre", "norman_ble_tracker_compat.py",
                               Path(__file__).with_name("ble_tracker_compat.py"))
        tracker = await cg.get_variable(config[CONF_BLE_TRACKER_ID])
        cg.add(var.set_ble_tracker(tracker))
