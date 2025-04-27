#pragma once

#include <SFML/Audio.hpp>
#include <cstdint>
#include <deque>
#include <mutex>

class Recorder : public sf::SoundRecorder {
public:
  Recorder(int16_t sampleSize) {
    sampleSize_ = sampleSize;
    buffer_ = std::deque<int16_t>(sampleSize_, 0);
  }
  ~Recorder() {}

  bool onProcessSamples(const int16_t *samples,
                        std::size_t sampleCount) override {

    // onProcessSamples needs a mutex
    // https://www.sfml-dev.org/tutorials/3.0/audio/recording/#custom-recording
    std::lock_guard<std::mutex> lock(bufferMutex_);
    // rotate buffer, insert at the end, delete at the beginning if too big
    buffer_.insert(buffer_.end(), samples, samples + sampleCount);
    if (buffer_.size() > sampleSize_) {
      buffer_.erase(buffer_.begin(),
                    buffer_.begin() + (buffer_.size() - sampleSize_));
    }
    return true;
  }

  // return current samples
  const std::deque<int16_t> getSamples() const {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    return buffer_;
  }

private:
  int16_t sampleSize_;
  std::deque<int16_t> buffer_;
  mutable std::mutex bufferMutex_;
};