#include "csi_model.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void make_iq(int8_t *iq, int gain, bool movement)
{
    for (unsigned i = 0; i < 64; ++i) {
        iq[2 * i] = 0;
        iq[2 * i + 1] = (int8_t)(gain * (10 + i % 7 + (movement && i % 3 == 0 ? 8 : 0)));
    }
}

static void calibrate(csi_model_t *model, const csi_features_t *f, uint32_t start)
{
    csi_model_reset(model);
    for (unsigned i = 0; i <= 900; ++i) csi_model_feed(model, f, start + i * 10u);
    assert(model->calibrated);
    assert(csi_model_progress(model, start + 9000) == 100);
    assert(model->score == 0);
}

int main(void)
{
    int8_t iq[128], doubled[128];
    csi_features_t a, b, moving;
    make_iq(iq, 1, false);
    make_iq(doubled, 2, false);
    assert(csi_extract(iq, 128, &a));
    assert(csi_extract(doubled, 128, &b));
    assert(fabsf(b.mean_amplitude - 2 * a.mean_amplitude) < 0.0001f);
    for (unsigned i = 0; i < CSI_BINS; ++i) assert(fabsf(a.normalized[i] - b.normalized[i]) < 0.00001f);
    assert(!csi_extract(NULL, 128, &a));
    assert(!csi_extract(iq, 127, &a));
    assert(!csi_extract(iq, 129, &a));
    memset(doubled, 0, 128);
    assert(!csi_extract(doubled, 128, &b));
    memset(doubled, 127, 128);
    assert(!csi_extract(doubled, 128, &b));
    // Hardware-invalid first word and DC must have no effect on selected bins.
    memcpy(doubled, iq, 128);
    memset(doubled, 127, 4);
    assert(csi_extract(doubled, 128, &b));
    assert(memcmp(&a, &b, sizeof(a)) == 0);
    make_iq(doubled, 1, true);
    assert(csi_extract(doubled, 128, &moving));
    csi_model_t model;
    calibrate(&model, &a, 100);
    float peak = 0;
    for (unsigned i = 1; i <= 80; ++i) {
        csi_model_feed(&model, (i / 10) % 2 ? &moving : &a, 9100 + i * 10);
        if (model.score > peak) peak = model.score;
        assert(isfinite(model.score) && model.score >= 0 && model.score <= 100);
    }
    assert(peak > 50);
    assert(!csi_model_fresh(&model, 12000));
    assert(csi_model_progress(&model, 12000) == 0);
    csi_model_feed(&model, &a, 12000);
    assert(!model.calibrated && model.score == 0);
    csi_model_reset(&model);
    assert(!csi_model_fresh(&model, 0));
    // Low packet rate cannot falsely complete calibration on elapsed time alone.
    for (unsigned i = 0; i < 12; ++i) csi_model_feed(&model, &a, i * 1000);
    assert(!model.calibrated);
    assert(csi_model_progress(&model, 11000) < 100);
    // uint32 timer wrap must preserve elapsed-time behavior.
    calibrate(&model, &a, UINT32_MAX - 4000);
    assert(csi_model_fresh(&model, 5000));
    assert(csi_credentials_valid("router", "12345678"));
    assert(csi_credentials_valid("router", ""));
    assert(!csi_credentials_valid("", "12345678"));
    assert(!csi_credentials_valid("router", "1234567"));
    assert(csi_credentials_valid("12345678901234567890123456789012", "12345678"));
    assert(!csi_credentials_valid("12345678901234567890123456789012345", "12345678"));
    char psk[65]; memset(psk, 'a', 64); psk[64] = 0;
    assert(csi_credentials_valid("router", psk));
    psk[0] = 'x';
    assert(!csi_credentials_valid("router", psk));
    puts("CSI model: PASS (amplitude, gain invariance, movement, calibration, stale data, clock wrap, input limits)");
    return 0;
}
