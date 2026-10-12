#include "device_data.h"
#include "pc_time.h"
#include <stdio.h>
q2_result_t q2_pc_device_data_register(q2_pc_device_data_t *a, q2_data_t *data) {
    if (!a || !data)
        return Q2_ERR_ARGUMENT;
    a->data = data;
    a->battery = (q2_pc_battery_sim_t){.percent = 75, .voltage_mv = 3900};
    a->wifi = (q2_pc_radio_sim_t){.connected = true, .rssi_dbm = -65};
    a->ble = (q2_pc_radio_sim_t){.connected = true, .rssi_dbm = -60};
    const q2_data_spec_t specs[] = {{.name = "system.clock.time",
                                     .capability = "clock.read",
                                     .unit = "HH:MM",
                                     .type = Q2_STRING,
                                     .ttl_ms = 5000},
                                    {.name = "power.battery.percent",
                                     .capability = "power.read",
                                     .unit = "percent",
                                     .type = Q2_INTEGER,
                                     .bounded = true,
                                     .maximum = 100,
                                     .ttl_ms = 30000},
                                    {.name = "power.battery.voltage_mv",
                                     .capability = "power.read",
                                     .unit = "mV",
                                     .type = Q2_INTEGER,
                                     .bounded = true,
                                     .maximum = 20000,
                                     .ttl_ms = 30000},
                                    {.name = "power.battery.charging",
                                     .capability = "power.read",
                                     .unit = "",
                                     .type = Q2_BOOLEAN,
                                     .ttl_ms = 30000},
                                    {.name = "wireless.wifi.connected",
                                     .capability = "wireless.read",
                                     .unit = "",
                                     .type = Q2_BOOLEAN,
                                     .ttl_ms = 5000},
                                    {.name = "wireless.wifi.rssi_dbm",
                                     .capability = "wireless.read",
                                     .unit = "dBm",
                                     .type = Q2_INTEGER,
                                     .bounded = true,
                                     .minimum = -127,
                                     .maximum = 0,
                                     .ttl_ms = 5000},
                                    {.name = "wireless.bt.connected",
                                     .capability = "wireless.read",
                                     .unit = "",
                                     .type = Q2_BOOLEAN,
                                     .ttl_ms = 5000},
                                    {.name = "wireless.ble.connected",
                                     .capability = "wireless.read",
                                     .unit = "",
                                     .type = Q2_BOOLEAN,
                                     .ttl_ms = 5000},
                                    {.name = "wireless.ble.rssi_dbm",
                                     .capability = "wireless.read",
                                     .unit = "dBm",
                                     .type = Q2_INTEGER,
                                     .bounded = true,
                                     .minimum = -127,
                                     .maximum = 0,
                                     .ttl_ms = 5000}};
    for (unsigned i = 0; i < 9; i++) {
        q2_result_t e = q2_data_register(data, a, &specs[i], &a->handles[i]);
        if (e != Q2_OK)
            return e;
    }
    return Q2_OK;
}
static q2_result_t mode_status(q2_pc_sim_mode_t mode) {
    if (mode == Q2_PC_SIM_VALUE)
        return Q2_OK;
    if (mode == Q2_PC_SIM_ERROR)
        return Q2_ERR_IO;
    if (mode == Q2_PC_SIM_TIMEOUT)
        return Q2_ERR_TIMEOUT;
    return Q2_ERR_UNSUPPORTED;
}
static q2_result_t publish(q2_pc_device_data_t *a, unsigned index, int value, bool boolean,
                           q2_result_t status) {
    q2_value_t sample = {.kind = boolean ? Q2_BOOLEAN : Q2_INTEGER};
    if (boolean)
        sample.as.boolean = value != 0;
    else
        sample.as.integer = value;
    return q2_data_publish(a->data, a, a->handles[index], &sample, status);
}
static q2_result_t publish_radio(q2_pc_device_data_t *a, unsigned index, const q2_pc_radio_sim_t *r) {
    q2_result_t status = mode_status(r->mode);
    q2_result_t e = publish(a, index, r->connected, true, status);
    if (e != Q2_OK)
        return e;
    // A remembered input value is not a live RSSI after disconnection.
    return publish(a, index + 1, r->rssi_dbm, false,
                   status == Q2_OK && !r->connected ? Q2_ERR_STATE : status);
}
q2_result_t q2_pc_device_data_poll(q2_pc_device_data_t *a) {
    if (!a || !a->data)
        return Q2_ERR_ARGUMENT;
    unsigned hour, minute;
    if (!q2_pc_local_time(&hour, &minute))
        return Q2_ERR_IO;
    q2_value_t value = {.kind = Q2_STRING};
    snprintf(value.as.string, sizeof(value.as.string), "%02u:%02u", hour, minute);
    q2_result_t e = q2_data_publish(a->data, a, a->handles[0], &value, Q2_OK);
    q2_result_t status = mode_status(a->battery.mode);
    if (e == Q2_OK)
        e = publish(a, 1, a->battery.percent, false, status);
    if (e == Q2_OK)
        e = publish(a, 2, a->battery.voltage_mv, false, status);
    if (e == Q2_OK)
        e = publish(a, 3, a->battery.charging, true, status);
    if (e == Q2_OK)
        e = publish_radio(a, 4, &a->wifi);
    // Classic BT stays separate from BLE and remains unavailable on this PC backend.
    if (e == Q2_OK)
        e = publish(a, 6, 0, true, Q2_ERR_UNSUPPORTED);
    if (e == Q2_OK)
        e = publish_radio(a, 7, &a->ble);
    return e;
}
q2_result_t q2_pc_device_data_battery(q2_pc_device_data_t *a, const q2_pc_battery_sim_t *s) {
    if (!a || !a->data)
        return Q2_ERR_UNSUPPORTED;
    if (!s || s->mode < Q2_PC_SIM_OFF || s->mode > Q2_PC_SIM_TIMEOUT || s->percent < 0 || s->percent > 100 ||
        s->voltage_mv < 0 || s->voltage_mv > 20000)
        return Q2_ERR_ARGUMENT;
    a->battery = *s;
    return q2_pc_device_data_poll(a);
}
q2_result_t q2_pc_device_data_radio(q2_pc_device_data_t *a, bool ble, const q2_pc_radio_sim_t *s) {
    if (!a || !a->data)
        return Q2_ERR_UNSUPPORTED;
    if (!s || s->mode < Q2_PC_SIM_OFF || s->mode > Q2_PC_SIM_TIMEOUT || s->rssi_dbm < -127 || s->rssi_dbm > 0)
        return Q2_ERR_ARGUMENT;
    if (ble)
        a->ble = *s;
    else
        a->wifi = *s;
    return q2_pc_device_data_poll(a);
}
