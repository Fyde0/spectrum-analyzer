#include "FFT.hpp"
#include "Recorder.hpp"
#include <SFML/Graphics.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/Window.hpp>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

#define DEFAULT_WINDOW_WIDTH 1024
#define DEFAULT_WINDOW_HEIGHT 768
#define FPS 30

// the sample size depends on sf::SoundRecorder::onProcessSamples
// and I don't think you can change it
// with a sample rate of 44100 you get 1102 samples every loop
// you probably shouldn't change these
#define SAMPLE_RATE 44100
#define SAMPLE_SIZE 1024

int main() {
  // set up window
  sf::RenderWindow window(
      sf::VideoMode({DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT}),
      "Spectrum Analyzer");
  // fps limit
  window.setFramerateLimit(FPS);
  // set up view, this is to handle window resizing properly
  sf::FloatRect viewArea(
      sf::Vector2f(0, 0),
      sf::Vector2f(DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT));
  window.setView(sf::View(viewArea));

  // check if audio input exists
  if (!Recorder::isAvailable()) {
    std::cerr << "Audio capture not available.\n";
    return 1;
  }

  // set up and run audio input
  Recorder recorder;
  if (!recorder.start(SAMPLE_RATE)) {
    std::cerr << "Error starting audio input.";
  }

  std::vector<std::string> devices = recorder.getAvailableDevices();
  std::string currentDevice = recorder.getDevice();
  int16_t currentDeviceIndex =
      find(devices.begin(), devices.end(), currentDevice) - devices.begin();

  // fft
  FFT fft(SAMPLE_SIZE);

  // variables
  const double minFrequency = 20.0;
  const double maxFrequency = SAMPLE_RATE / 2.0; // nyquist
  const double minDb = -80.0;
  const double maxDb = 0.0;

  while (window.isOpen()) {
    while (const std::optional event = window.pollEvent()) {
      // close window when close button is pressed
      if (event->is<sf::Event::Closed>()) {
        window.close();
      }

      // update the view when resizing the window
      if (const auto *resized = event->getIf<sf::Event::Resized>()) {
        viewArea.position = sf::Vector2f(0, 0);
        viewArea.size = sf::Vector2f(resized->size.x, resized->size.y);
        window.setView(sf::View(viewArea));
      }

      if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Tab)) {
        currentDeviceIndex = (currentDeviceIndex + 1) % devices.size();
        // you should be able to change device without stopping but it doesn't
        // really work
        recorder.stop();
        bool deviceSet = recorder.setDevice(devices[currentDeviceIndex]);
        bool recorderStarted = recorder.start(SAMPLE_RATE);
        if (deviceSet && recorderStarted) {
          std::cout << "Switched to input device: "
                    << devices[currentDeviceIndex] << "\n";
        } else {
          std::cerr << "Something's wrong.\n";
        }
        // sleep for basic debouncing
        sf::sleep(sf::milliseconds(200));
      }
    }

    // get current audio samples from audio input
    const std::vector<int16_t> &samples = recorder.getSamples();

    if (!samples.empty()) {
      // calculate fft of current samples, and get magnitudes
      fft.process(samples);
      const std::vector<double> &magnitudes = fft.getMagnitudes();

      // clear the window
      window.clear(sf::Color::Black);

      // Render bars based on magnitudes
      int barWidth = 2; // TODO wider for lower frequencies?
      int numberOfBars = magnitudes.size();

      // calculate bar height based on magnitudes
      std::vector<double> barHeights;
      for (auto mag : magnitudes) {
        // this normalizes to 1 more or less
        // also removes noise as a side effect, kind of
        mag /= 1.15e+07;
        // convert magnitudes to dB
        // add small number to avoid log(0)
        double db = 20.0 * std::log10(mag + 1e-12);
        // clamp
        db = std::clamp(db, minDb, maxDb);
        // scale to window size
        double height = ((db - minDb) / (maxDb - minDb)) * window.getSize().y;
        barHeights.push_back(height);
      }

      // calculate x position of bars based on frequency (logarithmic)
      for (size_t i = 0; i < numberOfBars; ++i) {

        // frequency of bar
        double frequency = static_cast<double>(i * SAMPLE_RATE) / SAMPLE_SIZE;
        // x position based on frequency range and window size
        float xPosition = window.getSize().x *
                          (std::log10(frequency) - std::log10(minFrequency)) /
                          (std::log10(maxFrequency) - std::log10(minFrequency));

        // create bar and set properties
        sf::RectangleShape bar;
        bar.setSize(sf::Vector2f(barWidth, barHeights[i]));
        bar.setPosition(
            sf::Vector2f(xPosition, window.getSize().y - bar.getSize().y));

        // draw on window
        window.draw(bar);
      }

      // update screen
      window.display();
    }
  }

  // clean up
  recorder.stop();
}