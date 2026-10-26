// Zigbee2MQTT 2.x external converter for the ESP32-C6 Hisense IR AC bridge (zigbee/ firmware).
// Install: copy to <z2m data>/external_converters/hisense-ir-ac.js and restart Zigbee2MQTT.
//
// Endpoint 1 clusters:
//   hvacThermostat (0x0201): systemMode, occupiedHeatingSetpoint, occupiedCoolingSetpoint, localTemp (always invalid)
//   hvacFanCtrl    (0x0202): fanMode (auto/low/medium/high mirror of the 6-speed value)
//   hisenseAc      (0xFC20): fanSpeed enum8 0..5, turbo/economy/sleep/vswing/hswing/display bool, resend enum8,
//                            heatSetpoint/coolSetpoint int16 (0.01 C, write path for both setpoints)
//
// Setpoint writes go to hisenseAc, not hvacThermostat. The esp-zigbee stack rejects a thermostat write when
// occupiedCoolingSetpoint < occupiedHeatingSetpoint (ZCL dead band rule, even with dead band 0). The firmware
// still reports the hvacThermostat setpoints, so state and display come from there.
//
// The climate entity has one setpoint (`current_heating_setpoint`). It writes the heating setpoint in heat mode
// and the cooling setpoint in every other mode. Both real setpoints are also exposed as numbers.
import {binary, deviceAddCustomCluster, enumLookup} from 'zigbee-herdsman-converters/lib/modernExtend';
import * as exposes from 'zigbee-herdsman-converters/lib/exposes';

const e = exposes.presets;
const ea = exposes.access;

const ZCL_BOOLEAN = 0x10;
const ZCL_ENUM8 = 0x30;
const ZCL_INT16 = 0x29;
const TEMP_MIN = 16;
const TEMP_MAX = 30;
const INVALID_TEMP = -32768;

const SYSTEM_MODES = {off: 0x00, cool: 0x03, heat: 0x04, fan_only: 0x07, dry: 0x08};
const FAN_SPEEDS = {auto: 0, low: 1, mid_low: 2, medium: 3, mid_high: 4, high: 5};

const lookupKey = (map, value) => Object.keys(map).find((k) => map[k] === value);
const clampTemp = (value) => Math.min(TEMP_MAX, Math.max(TEMP_MIN, Math.round(Number(value))));

// Off has no active mode in Z2M state, so the heating setpoint is shown.
const usesHeatingSetpoint = (mode) => mode === 'heat' || mode === 'off' || mode === undefined;
const activeSetpoint = (state) =>
    usesHeatingSetpoint(state.system_mode) ? state.occupied_heating_setpoint : state.occupied_cooling_setpoint;

const fzThermostat = {
    cluster: 'hvacThermostat',
    type: ['attributeReport', 'readResponse'],
    convert: (model, msg, publish, options, meta) => {
        const data = msg.data;
        const result = {};
        if (data.systemMode !== undefined) {
            const mode = lookupKey(SYSTEM_MODES, data.systemMode);
            if (mode !== undefined) result.system_mode = mode;
        }
        if (data.occupiedHeatingSetpoint !== undefined) result.occupied_heating_setpoint = data.occupiedHeatingSetpoint / 100;
        if (data.occupiedCoolingSetpoint !== undefined) result.occupied_cooling_setpoint = data.occupiedCoolingSetpoint / 100;
        if (data.localTemp !== undefined && data.localTemp !== INVALID_TEMP) result.local_temperature = data.localTemp / 100;
        if (result.system_mode !== undefined || result.occupied_heating_setpoint !== undefined ||
            result.occupied_cooling_setpoint !== undefined) {
            const setpoint = activeSetpoint({...meta.state, ...result});
            if (setpoint !== undefined) result.current_heating_setpoint = setpoint;
        }
        return result;
    },
};

const fzFanSpeed = {
    cluster: 'hisenseAc',
    type: ['attributeReport', 'readResponse'],
    convert: (model, msg) => {
        if (msg.data.fanSpeed === undefined) return;
        const speed = lookupKey(FAN_SPEEDS, msg.data.fanSpeed);
        return speed === undefined ? undefined : {fan_mode: speed};
    },
};

const tzSystemMode = {
    key: ['system_mode'],
    convertSet: async (entity, key, value, meta) => {
        const zcl = SYSTEM_MODES[value];
        if (zcl === undefined) throw new Error(`Unsupported system_mode '${value}'`);
        await entity.write('hvacThermostat', {systemMode: zcl});
        const state = {system_mode: value};
        const setpoint = activeSetpoint({...meta.state, ...state});
        if (setpoint !== undefined) state.current_heating_setpoint = setpoint;
        return {state};
    },
    convertGet: async (entity) => {
        await entity.read('hvacThermostat', ['systemMode', 'occupiedHeatingSetpoint', 'occupiedCoolingSetpoint']);
    },
};

const tzSetpoints = {
    key: ['current_heating_setpoint', 'occupied_heating_setpoint', 'occupied_cooling_setpoint'],
    convertSet: async (entity, key, value, meta) => {
        const temp = clampTemp(value);
        const heating = key === 'occupied_heating_setpoint' ||
            (key === 'current_heating_setpoint' &&
                usesHeatingSetpoint(meta.message.system_mode ?? meta.state.system_mode));
        await entity.write('hisenseAc', heating ? {heatSetpoint: temp * 100} : {coolSetpoint: temp * 100});
        const state = heating ? {occupied_heating_setpoint: temp} : {occupied_cooling_setpoint: temp};
        const setpoint = activeSetpoint({...meta.state, ...state});
        if (setpoint !== undefined) state.current_heating_setpoint = setpoint;
        return {state};
    },
    convertGet: async (entity) => {
        await entity.read('hvacThermostat', ['occupiedHeatingSetpoint', 'occupiedCoolingSetpoint']);
    },
};

const tzFanMode = {
    key: ['fan_mode'],
    convertSet: async (entity, key, value) => {
        const speed = FAN_SPEEDS[value];
        if (speed === undefined) throw new Error(`Unsupported fan_mode '${value}'`);
        await entity.write('hisenseAc', {fanSpeed: speed});
        return {state: {fan_mode: value}};
    },
    convertGet: async (entity) => {
        await entity.read('hisenseAc', ['fanSpeed']);
    },
};

const onOff = (name, attribute, description) =>
    binary({
        name,
        cluster: 'hisenseAc',
        attribute,
        valueOn: ['ON', 1],
        valueOff: ['OFF', 0],
        description,
        access: 'ALL',
        reporting: {min: 0, max: 3600, change: 0},
    });

const definition = {
    zigbeeModel: ['hisense-ir-ac'],
    model: 'hisense-ir-ac',
    vendor: 'ErnestsDIY',
    description: 'Hisense heat pump IR bridge (KELON168), ESP32-C6 router',
    fromZigbee: [fzThermostat, fzFanSpeed],
    toZigbee: [tzSystemMode, tzSetpoints, tzFanMode],
    exposes: [
        e.climate()
            .withSystemMode(Object.keys(SYSTEM_MODES))
            .withSetpoint('current_heating_setpoint', TEMP_MIN, TEMP_MAX, 1)
            .withLocalTemperature(ea.STATE)
            .withFanMode(Object.keys(FAN_SPEEDS)),
        e.numeric('occupied_heating_setpoint', ea.ALL).withUnit('°C').withValueMin(TEMP_MIN).withValueMax(TEMP_MAX)
            .withValueStep(1).withDescription('Heating setpoint (used in heat mode)'),
        e.numeric('occupied_cooling_setpoint', ea.ALL).withUnit('°C').withValueMin(TEMP_MIN).withValueMax(TEMP_MAX)
            .withValueStep(1).withDescription('Cooling setpoint (used in cool, dry and fan modes)'),
    ],
    extend: [
        deviceAddCustomCluster('hisenseAc', {
            ID: 0xfc20,
            attributes: {
                fanSpeed: {name: 'fanSpeed', ID: 0x0000, type: ZCL_ENUM8, write: true},
                turbo: {name: 'turbo', ID: 0x0001, type: ZCL_BOOLEAN, write: true},
                economy: {name: 'economy', ID: 0x0002, type: ZCL_BOOLEAN, write: true},
                sleep: {name: 'sleep', ID: 0x0003, type: ZCL_BOOLEAN, write: true},
                vswing: {name: 'vswing', ID: 0x0004, type: ZCL_BOOLEAN, write: true},
                hswing: {name: 'hswing', ID: 0x0005, type: ZCL_BOOLEAN, write: true},
                display: {name: 'display', ID: 0x0006, type: ZCL_BOOLEAN, write: true},
                resend: {name: 'resend', ID: 0x0010, type: ZCL_ENUM8, write: true},
                heatSetpoint: {name: 'heatSetpoint', ID: 0x0020, type: ZCL_INT16, write: true},
                coolSetpoint: {name: 'coolSetpoint', ID: 0x0021, type: ZCL_INT16, write: true},
            },
            commands: {},
            commandsResponse: {},
        }),
        onOff('turbo', 'turbo', 'Turbo. In heat mode the unit also sets 30 °C; off restores the old setpoint.'),
        onOff('economy', 'economy', 'Economy. Forces fan low; off restores the old fan speed.'),
        onOff('sleep', 'sleep', 'Sleep. Forces fan low; off restores the old fan speed.'),
        onOff('vswing', 'vswing', 'Vertical swing (IR toggle, state tracked by the bridge)'),
        onOff('hswing', 'hswing', 'Horizontal swing (IR toggle, state tracked by the bridge)'),
        onOff('display', 'display', 'Unit display (IR toggle, state tracked by the bridge)'),
        enumLookup({
            name: 'resend',
            cluster: 'hisenseAc',
            attribute: 'resend',
            lookup: {state: 1, vswing_pulse: 2, hswing_pulse: 3, display_pulse: 4},
            description:
                'state: send the full state again. *_pulse: send one toggle frame without changing the tracked state ' +
                '(use it when the unit and Home Assistant disagree).',
            access: 'SET',
            reporting: false,
            entityCategory: 'config',
        }),
    ],
    configure: async (device, coordinatorEndpoint) => {
        const endpoint = device.getEndpoint(1);
        await endpoint.bind('hvacThermostat', coordinatorEndpoint);
        await endpoint.bind('hisenseAc', coordinatorEndpoint);
        const reportCfg = (attribute) => ({attribute, minimumReportInterval: 0, maximumReportInterval: 3600, reportableChange: 0});
        await endpoint.configureReporting('hvacThermostat', [
            reportCfg('systemMode'),
            reportCfg('occupiedHeatingSetpoint'),
            reportCfg('occupiedCoolingSetpoint'),
        ]);
        await endpoint.configureReporting('hisenseAc', [reportCfg('fanSpeed')]);
        await endpoint.read('hvacThermostat', ['systemMode', 'occupiedHeatingSetpoint', 'occupiedCoolingSetpoint']);
        await endpoint.read('hisenseAc', ['fanSpeed', 'turbo', 'economy', 'sleep', 'vswing', 'hswing', 'display']);
    },
};

export default [definition];
