#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CSI_BINS 42
#define CSI_CALIBRATION_MS 8000u
#define CSI_STALE_MS 1500u

typedef struct {
    float amplitude[CSI_BINS];
    float normalized[CSI_BINS];
    float mean_amplitude;
} csi_features_t;

typedef struct {
    float reference[CSI_BINS];
    float motion;
    float noise_mean;
    float noise_m2;
    float noise_sigma;
    float score;
    uint32_t last_ms;
    uint32_t calibration_start_ms;
    uint32_t calibration_samples;
    bool have_frame;
    bool calibrated;
} csi_model_t;

// HT20 LLTF: signed imaginary, real pairs. Avoid DC, guard carriers, and
// the first four bytes (which can be invalid on ESP32-C3).
bool csi_extract(const int8_t *iq, size_t length, csi_features_t *features);
void csi_model_reset(csi_model_t *model);
void csi_model_feed(csi_model_t *model, const csi_features_t *features, uint32_t now_ms);
bool csi_model_fresh(const csi_model_t *model, uint32_t now_ms);
unsigned csi_model_progress(const csi_model_t *model, uint32_t now_ms);
bool csi_credentials_valid(const char *ssid, const char *password);
