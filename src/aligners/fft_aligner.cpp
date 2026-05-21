#include "ffsubsync/aligner.h"
#include "ffsubsync/constants.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <vector>

#include <kiss_fft.h>

namespace ffsubsync {

FFTAligner::FFTAligner(std::optional<int> max_offset_samples)
    : max_offset_samples_(max_offset_samples) {}

FFTAligner::Result FFTAligner::align(const std::vector<float>& refstring,
                                     const std::vector<float>& substring) {
    if (refstring.empty() || substring.empty()) {
        return {0, 0.0};
    }

    const size_t ref_len = refstring.size();
    const size_t sub_len  = substring.size();

    // Pad to the next power of two so that circular convolution equals
    // linear convolution over the region of interest.
    size_t total_length = 1;
    while (total_length < ref_len + sub_len) {
        total_length <<= 1;
    }

    // Bipolar conversion: 0 -> -1, 1 -> +1.
    // Build the substring FFT input: [zeros(extra + ref_len), substring].
    std::vector<kiss_fft_cpx> fft_sub_in(total_length);
    const size_t extra_zeros = total_length - sub_len - ref_len;
    const size_t sub_start   = extra_zeros + ref_len;

    for (size_t i = 0; i < sub_len; ++i) {
        float val = 2.0f * substring[i] - 1.0f;
        fft_sub_in[sub_start + i].r = val;
        fft_sub_in[sub_start + i].i = 0.0f;
    }
    // Leading zeros are already present because vector<> value-initialises.

    // Build the reference FFT input: flip([refstring, zeros(sub + extra)]).
    std::vector<kiss_fft_cpx> ref_padded(total_length);
    for (size_t i = 0; i < ref_len; ++i) {
        float val = 2.0f * refstring[i] - 1.0f;
        ref_padded[i].r = val;
        ref_padded[i].i = 0.0f;
    }

    std::vector<kiss_fft_cpx> fft_ref_in(total_length);
    for (size_t i = 0; i < total_length; ++i) {
        fft_ref_in[i] = ref_padded[total_length - 1 - i];
    }

    // Allocate forward and inverse FFT configurations.
    kiss_fft_cfg fwd_cfg = kiss_fft_alloc(static_cast<int>(total_length), 0, nullptr, nullptr);
    kiss_fft_cfg inv_cfg = kiss_fft_alloc(static_cast<int>(total_length), 1, nullptr, nullptr);

    std::vector<kiss_fft_cpx> fft_sub_out(total_length);
    std::vector<kiss_fft_cpx> fft_ref_out(total_length);

    kiss_fft(fwd_cfg, fft_sub_in.data(), fft_sub_out.data());
    kiss_fft(fwd_cfg, fft_ref_in.data(), fft_ref_out.data());

    // Pointwise multiply in the frequency domain.
    std::vector<kiss_fft_cpx> ifft_in(total_length);
    for (size_t i = 0; i < total_length; ++i) {
        float a = fft_sub_out[i].r;
        float b = fft_sub_out[i].i;
        float c = fft_ref_out[i].r;
        float d = fft_ref_out[i].i;
        ifft_in[i].r = a * c - b * d;
        ifft_in[i].i = a * d + b * c;
    }

    // Inverse FFT to obtain the (circular) cross-correlation.
    std::vector<kiss_fft_cpx> convolve_cpx(total_length);
    kiss_fft(inv_cfg, ifft_in.data(), convolve_cpx.data());

    // kiss_fft's inverse does *not* normalise by 1/N (numpy.ifft does).
    // Normalising makes scores comparable across different transform sizes.
    float norm = 1.0f / static_cast<float>(total_length);

    std::vector<std::complex<float>> convolve(total_length);
    for (size_t i = 0; i < total_length; ++i) {
        convolve[i] = std::complex<float>(convolve_cpx[i].r * norm,
                                          convolve_cpx[i].i * norm);
    }

    kiss_fft_free(fwd_cfg);
    kiss_fft_free(inv_cfg);

    eliminate_extreme_offsets(convolve, sub_len);

    compute_argmax(convolve, sub_len);

    return {best_offset_, best_score_};
}

void FFTAligner::eliminate_extreme_offsets(std::vector<std::complex<float>>& convolve,
                                             size_t substring_len) {
    if (!max_offset_samples_.has_value()) {
        return;
    }

    const int max_offset     = *max_offset_samples_;
    const size_t total_length = convolve.size();

    for (size_t i = 0; i < total_length; ++i) {
        int offset = static_cast<int>(total_length - 1 - i - substring_len);
        // Match Python's half-open interval: keep (-max_offset, max_offset]
        if (offset <= -max_offset || offset > max_offset) {
            convolve[i].real(-std::numeric_limits<float>::infinity());
        }
    }
}

void FFTAligner::compute_argmax(const std::vector<std::complex<float>>& convolve,
                                size_t substring_len) {
    const size_t total_length = convolve.size();
    size_t best_idx = 0;
    float best_val  = -std::numeric_limits<float>::infinity();

    for (size_t i = 0; i < total_length; ++i) {
        float real_val = convolve[i].real();
        if (real_val > best_val) {
            best_val = real_val;
            best_idx = i;
        }
    }

    best_offset_ = static_cast<int>(total_length - 1 - best_idx - substring_len);
    best_score_  = static_cast<double>(best_val);
}

} // namespace ffsubsync
