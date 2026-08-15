<!-- AUTO-GENERATED - DO NOT EDIT MANUALLY -->
# ESP Telemetry Protocol V2

Protocol v2 keeps the existing `SPDP` magic and CRC16 protection while allowing grouped semantic telemetry fields.

## Frame Header

| Offset | Size | Field | Description |
| --- | --- | --- | --- |
| 0 | 4 | magic | `SPDP` |
| 4 | 1 | version | `1` for legacy snapshot, `2` for v2 |
| 5 | 1 | message_type | semantic message type |
| 6 | 2 | payload_length | little-endian bytes |
| 8 | 2 | sequence | little-endian, independent per transmitted stream |
| 10 | 4 | gateway_timestamp_ms | little-endian milliseconds |
| 14 | N | payload | message-specific |
| 14+N | 2 | crc16 | Modbus CRC16 over header + payload |

## Message Types

| ID | Name | Purpose |
| --- | --- | --- |
| 1 | V1_SNAPSHOT | Existing fixed 48-byte frame, decoded for backward compatibility. |
| 16 | FAST_TELEMETRY | Live PV and battery values, about every 1 second. |
| 17 | ENERGY_TOTALS | Energy counters, about every 30 seconds. |
| 18 | STATUS | Read-only status flags, about every 5 seconds. |
| 19 | DIAGNOSTICS | Gateway/protocol diagnostics plus lower-cadence production fields. |
| 32 | COMMAND | Controller -> Gateway typed command request. |
| 33 | COMMAND_RESULT | Gateway -> Controller typed command result. |
| 34-63 | Future command range | Reserved for future typed commands. |

## V2 Telemetry Payload

No floats are sent on the wire. Values are signed 32-bit scaled integers.

| Offset | Size | Field | Description |
| --- | --- | --- | --- |
| 0 | 1 | device_id | Source identity: `1` inverter 1, `2` inverter 2, `3` Controller/system, `4` Garage Sensor, `5` Display |
| 1 | 1 | group_id | 1 fast, 2 energy, 3 status, 4 diagnostics |
| 2 | 1 | field_count | number of following field records |
| 3 | 2 | group_flags | bit 0 = group values valid, bit 1 = cache valid |
| 5 | 6 * field_count | records | `field_id:u16` + `value:i32`, little-endian |

Unknown field IDs and unknown message types are skipped after CRC validation.

## Field IDs

Stage 28 policy: all fields listed here are production (status `PRODUCTION`, `UNDER_REVIEW`, or `KNOWN_ISSUE` -- see the Status column). There is no separate non-production field ID range any more; a catalog entity either has a `protocol_field_id` and is generated here, or it has none and stays documentation-only in `candidates[]`.

| Field ID | Key | Name | Group | Update class | Wire type | Wire scale | Unit | Status |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1001 | pv1_power | PV1 Power | FAST_TELEMETRY | FAST | int32 | 1 | W | PRODUCTION |
| 1002 | pv2_power | PV2 Power | FAST_TELEMETRY | FAST | int32 | 1 | W | PRODUCTION |
| 2001 | battery_soc | Battery SOC | FAST_TELEMETRY | FAST | int32 | 10 | % | PRODUCTION |
| 2002 | battery_charge_power | Battery Charge Power | FAST_TELEMETRY | FAST | int32 | 1 | W | PRODUCTION |
| 2003 | battery_discharge_power | Battery Discharge Power | FAST_TELEMETRY | FAST | int32 | 1 | W | PRODUCTION |
| 5001 | pv1_energy_total | PV1 Energy Total | ENERGY_TOTALS | SLOW | int32 | 1000 | kWh | PRODUCTION |
| 7001 | feed_in_grid_enabled | Feed-In Grid Enabled | STATUS | NORMAL | bool | 1 |  | UNDER_REVIEW |
| 7002 | max_backflow_power | Max Backflow Power | STATUS | NORMAL | int32 | 1 | % | PRODUCTION |
| 7003 | zero_export_enabled | Zero Export | STATUS | NORMAL | bool | 1 |  | PRODUCTION |
| 9001 | battery_capacity | Battery Capacity | DIAGNOSTICS | SLOW | int32 | 1 | Ah | PRODUCTION |
| 9002 | bms_max_cell_temperature | BMS Max Cell Temperature | DIAGNOSTICS | NORMAL | int32 | 10 | deg C | PRODUCTION |
| 9003 | power_to_grid | Power To Grid | FAST_TELEMETRY | FAST | int32 | 1 | W | PRODUCTION |
| 9004 | power_from_grid | Power From Grid | FAST_TELEMETRY | FAST | int32 | 1 | W | PRODUCTION |
| 9005 | grid_flow | Grid Flow | DIAGNOSTICS | NORMAL | int32 | 1 | W | PRODUCTION |
| 9006 | pv1_voltage | PV1 Voltage | DIAGNOSTICS | NORMAL | int32 | 10 | V | PRODUCTION |
| 9007 | pv2_voltage | PV2 Voltage | DIAGNOSTICS | NORMAL | int32 | 10 | V | PRODUCTION |
| 9008 | pv3_voltage | PV3 Voltage | DIAGNOSTICS | NORMAL | int32 | 10 | V | PRODUCTION |
| 9009 | pv3_power | PV3 Power | DIAGNOSTICS | NORMAL | int32 | 1 | W | PRODUCTION |
| 9010 | pv1_energy_today | PV1 Energy Today | DIAGNOSTICS | SLOW | int32 | 10 | kWh | PRODUCTION |
| 9011 | pv2_energy_today | PV2 Energy Today | DIAGNOSTICS | SLOW | int32 | 10 | kWh | PRODUCTION |
| 9012 | pv3_energy_today | PV3 Energy Today | DIAGNOSTICS | SLOW | int32 | 10 | kWh | PRODUCTION |
| 9013 | pv2_energy_total | PV2 Energy Total | DIAGNOSTICS | SLOW | int32 | 10 | kWh | PRODUCTION |
| 9014 | pv3_energy_total | PV3 Energy Total | DIAGNOSTICS | SLOW | int32 | 10 | kWh | PRODUCTION |
| 9015 | battery_voltage | Battery Voltage | DIAGNOSTICS | NORMAL | int32 | 10 | V | PRODUCTION |
| 9016 | battery_soh | Battery SOH | DIAGNOSTICS | SLOW | int32 | 1 | % | PRODUCTION |
| 9017 | battery_temperature | Battery Temperature | DIAGNOSTICS | NORMAL | int32 | 1 | deg C | PRODUCTION |
| 9018 | battery_parallel_number | Battery Parallel Number | DIAGNOSTICS | SLOW | int32 | 1 |  | PRODUCTION |
| 9019 | battery_type_and_brand | Battery Type And Brand | DIAGNOSTICS | SLOW | int32 | 1 |  | PRODUCTION |
| 9020 | bms_max_charge_current | BMS Max Charge Current | DIAGNOSTICS | SLOW | int32 | 10 | A | PRODUCTION |
| 9021 | bms_max_discharge_current | BMS Max Discharge Current | DIAGNOSTICS | SLOW | int32 | 10 | A | PRODUCTION |
| 9022 | bms_charge_voltage_reference | BMS Charge Voltage Reference | DIAGNOSTICS | SLOW | int32 | 10 | V | PRODUCTION |
| 9023 | bms_discharge_cutoff_voltage | BMS Discharge Cutoff Voltage | DIAGNOSTICS | SLOW | int32 | 10 | V | PRODUCTION |
| 9024 | bms_battery_current | BMS Battery Current | DIAGNOSTICS | NORMAL | int32 | 10 | A | PRODUCTION |
| 9025 | bms_fault_code | BMS Fault Code | DIAGNOSTICS | NORMAL | int32 | 1 |  | PRODUCTION |
| 9026 | bms_warning_code | BMS Warning Code | DIAGNOSTICS | NORMAL | int32 | 1 |  | PRODUCTION |
| 9027 | bms_max_cell_voltage | BMS Max Cell Voltage | DIAGNOSTICS | NORMAL | int32 | 1000 | V | PRODUCTION |
| 9028 | bms_min_cell_voltage | BMS Min Cell Voltage | DIAGNOSTICS | NORMAL | int32 | 1000 | V | PRODUCTION |
| 9029 | bms_min_cell_temperature | BMS Min Cell Temperature | DIAGNOSTICS | NORMAL | int32 | 10 | deg C | PRODUCTION |
| 9030 | bms_firmware_update_state | BMS Firmware Update State | DIAGNOSTICS | SLOW | int32 | 1 |  | PRODUCTION |
| 9031 | bms_cycle_count | BMS Cycle Count | DIAGNOSTICS | SLOW | int32 | 1 | cycles | PRODUCTION |
| 9032 | inverter_battery_voltage_sample | Inverter Battery Voltage Sample | DIAGNOSTICS | NORMAL | int32 | 10 | V | PRODUCTION |
| 9033 | charge_energy_today | Charge Energy Today | DIAGNOSTICS | SLOW | int32 | 10 | kWh | PRODUCTION |
| 9034 | discharge_energy_today | Discharge Energy Today | DIAGNOSTICS | SLOW | int32 | 10 | kWh | PRODUCTION |
| 9035 | charge_energy_total | Charge Energy Total | DIAGNOSTICS | SLOW | int32 | 10 | kWh | PRODUCTION |
| 9036 | discharge_energy_total | Discharge Energy Total | DIAGNOSTICS | SLOW | int32 | 10 | kWh | PRODUCTION |
| 9037 | grid_voltage | Grid Voltage | DIAGNOSTICS | NORMAL | int32 | 10 | V | PRODUCTION |
| 9038 | grid_voltage_s | Grid Voltage S-Phase | DIAGNOSTICS | NORMAL | int32 | 10 | V | PRODUCTION |
| 9039 | grid_voltage_t | Grid Voltage T-Phase | DIAGNOSTICS | NORMAL | int32 | 10 | V | PRODUCTION |
| 9040 | grid_frequency | Grid Frequency | DIAGNOSTICS | NORMAL | int32 | 100 | Hz | PRODUCTION |
| 9041 | inverter_power | Inverter Power | DIAGNOSTICS | NORMAL | int32 | 1 | W | PRODUCTION |
| 9042 | ac_charging_rectification_power | AC Charging Rectification Power | DIAGNOSTICS | NORMAL | int32 | 1 | W | PRODUCTION |
| 9043 | inverter_current | Inverter Current | DIAGNOSTICS | NORMAL | int32 | 100 | A | PRODUCTION |
| 9044 | power_factor | Power Factor | DIAGNOSTICS | NORMAL | int32 | 1000 | % | PRODUCTION |
| 9045 | energy_to_grid_today | Energy To Grid Today | DIAGNOSTICS | SLOW | int32 | 10 | kWh | PRODUCTION |
| 9046 | energy_from_grid_today | Energy From Grid Today | DIAGNOSTICS | SLOW | int32 | 10 | kWh | PRODUCTION |
| 9047 | energy_to_grid_total | Energy To Grid Total | DIAGNOSTICS | SLOW | int32 | 10 | kWh | PRODUCTION |
| 9048 | energy_from_grid_total | Energy From Grid Total | DIAGNOSTICS | SLOW | int32 | 10 | kWh | PRODUCTION |
| 9049 | ongrid_load_power | On-Grid Load Power | DIAGNOSTICS | NORMAL | int32 | 1 | W | PRODUCTION |
| 9050 | bus1_voltage | Bus 1 Voltage | DIAGNOSTICS | NORMAL | int32 | 10 | V | PRODUCTION |
| 9051 | bus2_voltage | Bus 2 Voltage | DIAGNOSTICS | NORMAL | int32 | 10 | V | PRODUCTION |
| 9052 | half_bus_voltage | Half Bus Voltage | DIAGNOSTICS | NORMAL | int32 | 10 | V | PRODUCTION |
| 9053 | eps_voltage | EPS Voltage | DIAGNOSTICS | NORMAL | int32 | 10 | V | PRODUCTION |
| 9054 | eps_voltage_s | EPS Voltage S-Phase | DIAGNOSTICS | NORMAL | int32 | 10 | V | PRODUCTION |
| 9055 | eps_voltage_t | EPS Voltage T-Phase | DIAGNOSTICS | NORMAL | int32 | 10 | V | PRODUCTION |
| 9056 | eps_frequency | EPS Frequency | DIAGNOSTICS | NORMAL | int32 | 100 | Hz | PRODUCTION |
| 9057 | eps_power | EPS Power | DIAGNOSTICS | NORMAL | int32 | 1 | W | PRODUCTION |
| 9058 | eps_apparent_power | EPS Apparent Power | DIAGNOSTICS | NORMAL | int32 | 1 | VA | PRODUCTION |
| 9059 | eps_energy_today | EPS Energy Today | DIAGNOSTICS | SLOW | int32 | 10 | kWh | PRODUCTION |
| 9060 | eps_energy_total | EPS Energy Total | DIAGNOSTICS | SLOW | int32 | 10 | kWh | PRODUCTION |
| 9061 | internal_temperature | Internal Temperature | DIAGNOSTICS | NORMAL | int32 | 1 | deg C | PRODUCTION |
| 9062 | radiator_temperature | Radiator Temperature | DIAGNOSTICS | NORMAL | int32 | 1 | deg C | PRODUCTION |
| 9063 | radiator_temperature_2 | Radiator Temperature 2 | DIAGNOSTICS | NORMAL | int32 | 1 | deg C | PRODUCTION |
| 9064 | inverter_state | Inverter State | DIAGNOSTICS | NORMAL | int32 | 1 |  | PRODUCTION |
| 9065 | internal_fault_code | Internal Fault Code | DIAGNOSTICS | NORMAL | int32 | 1 |  | PRODUCTION |
| 9066 | ac_input_type | AC Input Type | DIAGNOSTICS | NORMAL | int32 | 1 |  | PRODUCTION |
| 9067 | auto_test_status | Auto Test Status | DIAGNOSTICS | NORMAL | int32 | 1 |  | PRODUCTION |
| 9068 | inverter_energy_today | Inverter Energy Today | DIAGNOSTICS | SLOW | int32 | 10 | kWh | PRODUCTION |
| 9069 | ac_charge_energy_today | AC Charge Energy Today | DIAGNOSTICS | SLOW | int32 | 10 | kWh | PRODUCTION |
| 9070 | inverter_energy_total | Inverter Energy Total (Raw) | DIAGNOSTICS | SLOW | int32 | 10 | kWh | PRODUCTION |
| 9071 | ac_charge_energy_total | AC Charge Energy Total | DIAGNOSTICS | SLOW | int32 | 10 | kWh | PRODUCTION |
| 9072 | total_running_time | Total Running Time | DIAGNOSTICS | SLOW | int32 | 1 | s | PRODUCTION |
| 9073 | generator_voltage | Generator Voltage | DIAGNOSTICS | NORMAL | int32 | 10 | V | PRODUCTION |
| 9074 | generator_frequency | Generator Frequency | DIAGNOSTICS | NORMAL | int32 | 100 | Hz | PRODUCTION |
| 9075 | generator_power | Generator Power | DIAGNOSTICS | NORMAL | int32 | 1 | W | PRODUCTION |
| 9076 | generator_energy_today | Generator Energy Today | DIAGNOSTICS | SLOW | int32 | 10 | kWh | PRODUCTION |
| 9077 | pv1_current | PV1 Current | DIAGNOSTICS | NORMAL | int32 | 100 | A | PRODUCTION |
| 9078 | pv2_current | PV2 Current | DIAGNOSTICS | NORMAL | int32 | 100 | A | PRODUCTION |
| 9079 | pv3_current | PV3 Current | DIAGNOSTICS | NORMAL | int32 | 100 | A | PRODUCTION |
| 9080 | battery_flow | Battery Flow | DIAGNOSTICS | NORMAL | int32 | 1 | W | PRODUCTION |
| 9081 | bms_cell_difference | BMS Cell Difference | DIAGNOSTICS | NORMAL | int32 | 1000 | V | PRODUCTION |
| 9082 | grid_connected | Grid Connected | DIAGNOSTICS | NORMAL | bool | 1 |  | PRODUCTION |
| 9083 | active_fault_code | Active Fault Code | DIAGNOSTICS | NORMAL | int32 | 1 |  | PRODUCTION |
| 9084 | active_warning_code | Active Warning Code | DIAGNOSTICS | NORMAL | int32 | 1 |  | PRODUCTION |
| 9085 | load_power | Load Power | FAST_TELEMETRY | FAST | int32 | 1 | W | PRODUCTION |
| 10001 | system_output_voltage | System Output Voltage | FAST_TELEMETRY | FAST | int32 | 10 | V | PRODUCTION |
| 10002 | system_output_current | System Output Current | FAST_TELEMETRY | FAST | int32 | 100 | A | PRODUCTION |
| 10003 | system_output_power | System Output Power | FAST_TELEMETRY | FAST | int32 | 1 | W | PRODUCTION |
| 10004 | system_output_energy | System Output Energy | ENERGY_TOTALS | SLOW | int32 | 100 | kWh | PRODUCTION |
| 10005 | system_output_frequency | System Output Frequency | FAST_TELEMETRY | FAST | int32 | 10 | Hz | PRODUCTION |
| 10006 | system_output_power_factor | System Output Power Factor | FAST_TELEMETRY | FAST | int32 | 100 |  | PRODUCTION |
| 10007 | grid_input_voltage | Grid Input Voltage | FAST_TELEMETRY | FAST | int32 | 10 | V | PRODUCTION |
| 10008 | grid_input_current | Grid Input Current | FAST_TELEMETRY | FAST | int32 | 100 | A | PRODUCTION |
| 10009 | grid_input_power | Grid Input Power | FAST_TELEMETRY | FAST | int32 | 1 | W | PRODUCTION |
| 10010 | grid_input_energy | Grid Input Energy | ENERGY_TOTALS | SLOW | int32 | 100 | kWh | PRODUCTION |
| 10011 | grid_input_frequency | Grid Input Frequency | FAST_TELEMETRY | FAST | int32 | 10 | Hz | PRODUCTION |
| 10012 | grid_input_power_factor | Grid Input Power Factor | FAST_TELEMETRY | FAST | int32 | 100 |  | PRODUCTION |
| 10013 | grid_raw_voltage | Grid Raw Voltage | FAST_TELEMETRY | FAST | int32 | 10 | V | PRODUCTION |
| 10014 | display_enabled | Display Enabled | FAST_TELEMETRY | FAST | bool | 1 |  | PRODUCTION |
| 11001 | garage_presence | Garage Presence | STATUS | FAST | bool | 1 |  | PRODUCTION |
| 12001 | display_maintenance_wifi_actual | Display Maintenance Wi-Fi Actual | STATUS | FAST | bool | 1 |  | PRODUCTION |

## V1 Compatibility

The decoder accepts both version 1 snapshot frames and version 2 telemetry frames. Existing v1 receivers continue to work while the gateway is in `V1_AND_V2` mode.

## Cadence And Bandwidth

With the current catalog, typical v2 traffic is one FAST frame per second, one STATUS frame every 5 seconds, one ENERGY frame every 30 seconds, and one DIAGNOSTICS frame every 10 seconds.

- FAST payload: 34 bytes, frame: 50 bytes.
- STATUS payload: 10 bytes, frame: 26 bytes.
- ENERGY payload: 10 bytes, frame: 26 bytes.
- DIAGNOSTICS payload: 34 bytes, frame: 50 bytes.
- Typical v2 bandwidth: about 60 bytes/s.
- Migration `V1_AND_V2` with one v1 frame/s adds 48 bytes/s, total about 108 bytes/s.
- 230400 baud is about 23040 bytes/s at 10 bits/byte, so migration traffic is below 0.5% of line capacity.
