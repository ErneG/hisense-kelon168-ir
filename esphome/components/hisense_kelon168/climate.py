import esphome.codegen as cg
from esphome.components import button, climate_ir, time as time_
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TIME_ID, ENTITY_CATEGORY_CONFIG

CODEOWNERS = ["@ernestsdane"]
DEPENDENCIES = ["remote_transmitter"]
AUTO_LOAD = ["climate_ir", "button", "time"]

hisense_ns = cg.esphome_ns.namespace("hisense_kelon168")
HisenseKelon168Climate = hisense_ns.class_(
    "HisenseKelon168Climate", climate_ir.ClimateIR
)
HisenseButton = hisense_ns.class_("HisenseButton", button.Button, cg.Parented.template(HisenseKelon168Climate))

CONF_TOGGLE_V_SWING = "toggle_v_swing_button"
CONF_TOGGLE_H_SWING = "toggle_h_swing_button"
CONF_TOGGLE_DISPLAY = "toggle_display_button"
CONF_RESEND_STATE = "resend_state_button"

BUTTONS = {
    CONF_TOGGLE_V_SWING: (0, "mdi:swap-vertical"),
    CONF_TOGGLE_H_SWING: (1, "mdi:swap-horizontal"),
    CONF_TOGGLE_DISPLAY: (2, "mdi:monitor"),
    CONF_RESEND_STATE: (3, "mdi:send"),
}


def _button_schema(icon):
    return button.button_schema(
        HisenseButton, icon=icon, entity_category=ENTITY_CATEGORY_CONFIG
    )


CONFIG_SCHEMA = (
    climate_ir.climate_ir_with_receiver_schema(HisenseKelon168Climate)
    .extend(
        {
            cv.Optional(climate_ir.CONF_SUPPORTS_HEAT_COOL, default=False): cv.boolean,
            cv.Optional(CONF_TIME_ID): cv.use_id(time_.RealTimeClock),
            **{cv.Optional(key): _button_schema(icon) for key, (_, icon) in BUTTONS.items()},
        }
    )
)


async def to_code(config):
    var = await climate_ir.new_climate_ir(config)
    if CONF_TIME_ID in config:
        clock = await cg.get_variable(config[CONF_TIME_ID])
        cg.add(var.set_time(clock))
    for key, (action, _) in BUTTONS.items():
        if key in config:
            btn = await button.new_button(config[key])
            await cg.register_parented(btn, var)
            cg.add(btn.set_action(action))
