#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "csi_model.h"
#include "bsp_button.h"

typedef struct {
    bool connected, provisioning, calibrated, fresh;
    uint8_t calibration_percent, channel;
    int rssi, battery;
    float amplitude, score;
    float spectrum[CSI_BINS];
    uint32_t frames, dropped, rejected, rate, epoch;
    char status[72];
    char ap_name[24];
    char ap_password[12];
} csi_view_t;

void csi_scope_run(void);
void csi_scope_view(csi_view_t *out);
void csi_scope_key(bsp_btn_t button, bsp_btn_ev_t event, void *arg);
bool csi_scope_ui_init(void);
