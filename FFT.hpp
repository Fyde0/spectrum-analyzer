#pragma once

#include <cmath>
#include <cstdint>
#include <fftw3.h>
#include <vector>

class FFT {
public:
  FFT(int16_t sampleSize) {
    sampleSize_ = sampleSize;
    // set size of in_, important apparently
    in_.resize(sampleSize_);
    // tell fftw what to do
    plan_ = fftw_plan_dft_r2c_1d(sampleSize_, in_.data(), out_, FFTW_ESTIMATE);
  }

  ~FFT() {
    // clean up
    fftw_destroy_plan(plan_);
  }

  void process(const std::vector<int16_t> &samples) {
    // put the samples in the input array as doubles
    for (size_t i = 0; i < sampleSize_; ++i) {
      if (i < samples.size()) {
        // apply windowing
        double hannWindow =
            0.5 * (1 - std::cos(2 * M_PI * i / (sampleSize_ - 1)));
        in_[i] = static_cast<double>(samples[i]) * hannWindow;
      } else {
        // padding
        in_[i] = 0.0;
      }
    }

    // run the FFT
    fftw_execute(plan_);

    // calculate magnitudes
    magnitudes_.clear();
    // I don't remember why I divide by 2?
    for (size_t i = 0; i < sampleSize_ / 2 + 1; ++i) {
      double magnitude =
          std::sqrt(out_[i][0] * out_[i][0] + out_[i][1] * out_[i][1]);
      magnitudes_.push_back(magnitude);
    }
  }

  const std::vector<double> &getMagnitudes() const { return magnitudes_; }

private:
  int16_t sampleSize_;
  std::vector<double> in_;
  fftw_complex out_[8192]; // must be an array for fftw_plan_dft_r2c_1d
                           // apparently. yes, it's annoying.
  fftw_plan plan_;
  std::vector<double> magnitudes_;
};