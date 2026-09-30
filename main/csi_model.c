#include "csi_model.h"
#include <math.h>
#include <string.h>

bool csi_extract(const int8_t *iq, size_t length, csi_features_t *out)
{
    if (!iq || !out || length != 128) return false;
    float power = 0;
    float sum = 0;
    unsigned clipped = 0;
    for (unsigned i = 0; i < CSI_BINS; ++i) {
        unsigned bin = i < 21 ? 38 + i : 6 + i - 21;
        float im = iq[2 * bin], re = iq[2 * bin + 1];
        if (fabsf(im) >= 127 || fabsf(re) >= 127) ++clipped;
        float p = im * im + re * re;
        out->amplitude[i] = sqrtf(p);
        power += p;
        sum += out->amplitude[i];
    }
    if (power < CSI_BINS || clipped > CSI_BINS / 4) return false;
    float rms = sqrtf(power / CSI_BINS);
    for (unsigned i = 0; i < CSI_BINS; ++i) out->normalized[i] = out->amplitude[i] / rms;
    out->mean_amplitude = sum / CSI_BINS;
    return true;
}

void csi_model_reset(csi_model_t *m) { memset(m, 0, sizeof(*m)); }

bool csi_model_fresh(const csi_model_t *m, uint32_t now)
{
    return m->have_frame && (uint32_t)(now - m->last_ms) < CSI_STALE_MS;
}

void csi_model_feed(csi_model_t *m, const csi_features_t *f, uint32_t now)
{
    if (!csi_model_fresh(m, now)) {
        csi_model_reset(m);
        memcpy(m->reference, f->normalized, sizeof(m->reference));
        m->calibration_start_ms = now;
        m->last_ms = now;
        m->have_frame = true;
        return;
    }
    uint32_t elapsed = now - m->last_ms;
    if (!elapsed) return;  // Do not let same-timestamp bursts weight the model.
    float dt = elapsed / 1000.0f;
    float slow = 1.0f - expf(-dt / 0.6f);
    float fast = 1.0f - expf(-dt / 0.12f);
    float difference = 0;
    for (unsigned i = 0; i < CSI_BINS; ++i) {
        float d = f->normalized[i] - m->reference[i];
        difference += d * d;
        m->reference[i] += slow * d;
    }
    m->motion += fast * (sqrtf(difference / CSI_BINS) - m->motion);
    m->last_ms = now;
    if (!m->calibrated) {
        // Let the filter settle before learning the empty-room noise floor.
        if (now - m->calibration_start_ms >= 1000) {
            ++m->calibration_samples;
            float delta = m->motion - m->noise_mean;
            m->noise_mean += delta / m->calibration_samples;
            m->noise_m2 += delta * (m->motion - m->noise_mean);
        }
        if (now - m->calibration_start_ms >= CSI_CALIBRATION_MS && m->calibration_samples >= 100) {
            m->noise_sigma = sqrtf(fmaxf(0, m->noise_m2 / (m->calibration_samples - 1)));
            m->calibrated = true;
        }
        m->score = 0;
    } else {
        float floor = m->noise_mean + 2 * m->noise_sigma;
        float span = fmaxf(0.035f, 6 * m->noise_sigma);
        m->score = fminf(100, fmaxf(0, 100 * (m->motion - floor) / span));
    }
}

unsigned csi_model_progress(const csi_model_t *m, uint32_t now)
{
    if (!csi_model_fresh(m, now)) return 0;
    if (m->calibrated) return 100;
    unsigned time_progress = (now - m->calibration_start_ms) * 100u / CSI_CALIBRATION_MS;
    unsigned sample_progress = m->calibration_samples;
    unsigned p = time_progress < sample_progress ? time_progress : sample_progress;
    return p < 99 ? p : 99;
}

bool csi_credentials_valid(const char *ssid, const char *password)
{
    if (!ssid || !password) return false;
    size_t sl = strlen(ssid), pl = strlen(password);
    if (sl < 1 || sl > 32) return false;
    if (pl == 0 || (pl >= 8 && pl <= 63)) return true;
    if (pl != 64) return false;
    for (size_t i = 0; i < pl; ++i) {
        char c = password[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}
