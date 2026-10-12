#ifndef Q2_PC_DEVICE_DATA_H
#define Q2_PC_DEVICE_DATA_H
#include "q2/data.h"
/** Explicit, session-only simulator overrides. OFF leaves hardware data unsupported. */
typedef enum { Q2_PC_SIM_OFF, Q2_PC_SIM_VALUE, Q2_PC_SIM_ERROR, Q2_PC_SIM_TIMEOUT } q2_pc_sim_mode_t;
typedef struct {
    q2_pc_sim_mode_t mode;
    int percent, voltage_mv;
    bool charging;
} q2_pc_battery_sim_t;
typedef struct {
    q2_pc_sim_mode_t mode;
    bool connected;
    int rssi_dbm;
} q2_pc_radio_sim_t;
/** PC status adapter. Address remains stable as the data publisher identity. */
typedef struct {
    q2_data_t *data;
    unsigned handles[9];
    q2_pc_battery_sim_t battery;
    q2_pc_radio_sim_t wifi, ble;
} q2_pc_device_data_t;
/** Register schema before boot. All overrides start OFF and are never persisted. */
q2_result_t q2_pc_device_data_register(q2_pc_device_data_t *adapter, q2_data_t *data);
/** Publish clock and configured samples on the owner. Disconnected radios invalidate RSSI. */
q2_result_t q2_pc_device_data_poll(q2_pc_device_data_t *adapter);
/** Validate and apply a battery debug sample. Accept percent 0..100 and voltage 0..20000 mV. */
q2_result_t q2_pc_device_data_battery(q2_pc_device_data_t *adapter, const q2_pc_battery_sim_t *sample);
/** Validate and apply Wi-Fi or BLE debug state. RSSI is -127..0 dBm; never controls a physical radio. */
q2_result_t q2_pc_device_data_radio(q2_pc_device_data_t *adapter, bool ble, const q2_pc_radio_sim_t *sample);
#endif
