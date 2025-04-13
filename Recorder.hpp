#pragma once

#include <SFML/Audio.hpp>
#include <cstdint>

class Recorder : public sf::SoundRecorder {
public:
  Recorder() {}
  ~Recorder() {}

  bool onProcessSamples(const int16_t *samples,
                        std::size_t sampleCount) override {
    // store samples as they come in
    for (size_t i = 0; i < sampleCount; ++i) {
      samples_.assign(samples, samples + sampleCount);
    }
    return true;
  }

  // return current samples
  const std::vector<int16_t> &getSamples() const { return samples_; }

private:
  std::vector<int16_t> samples_;
};