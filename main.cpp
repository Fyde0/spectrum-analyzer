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

// lower value = more smoothing
#define SMOOTHING_FACTOR 0.5
#define TILT 4.5           // dB/oct
#define TILT_REF_FREQ 1000 // Hz

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
  //
  const double minDb = 45.0;
  const double maxDb = 150.0;
  //
  const float minBarWidth = 2.0;
  // needs to be outside the loop because it will average out for smoothing
  std::vector<double> barHeights(SAMPLE_SIZE, 0.0);

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

      // press Tab to cycle between input devices
      if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Tab)) {
        currentDeviceIndex = (currentDeviceIndex + 1) % devices.size();
        // you should be able to change device without stopping but it doesn't
        // really work so stop, change and start again
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
      std::vector<double> magnitudes = fft.getMagnitudes();

      // clear the window
      window.clear(sf::Color::Black);

      // calculate bar height based on magnitudes
      // each iteration draws the previous bar
      // so we can calculate the width after knowing where the next bar will be
      // variables to save info about previous bar
      float prevX = 0.0f;
      float prevHeight = 0.0f;
      // adding dummy value at the end for extra iteration to draw the last bar
      magnitudes.push_back(0.0);
      // 
      for (size_t i = 0; i < magnitudes.size(); ++i) {
        // convert to log scale
        double db = 20.0 * std::log10(magnitudes[i] + 1e-12);
        // calculate frequency of bar
        double frequency = static_cast<double>(i * SAMPLE_RATE) / SAMPLE_SIZE;
        // calculate tilt for this frequency
        double tiltDb = TILT * std::log2(frequency / TILT_REF_FREQ);
        // apply tilt
        db += tiltDb;
        // remove everything below minDb
        db = std::max(minDb, db);
        // scale to window size
        double height = ((db - minDb) / (maxDb - minDb)) * window.getSize().y;
        // smooth (EMA)
        barHeights[i] =
            SMOOTHING_FACTOR * height + (1 - SMOOTHING_FACTOR) * barHeights[i];

        // frequency to log scale, for positioning and width
        double freqLog = (std::log2(frequency) - std::log2(minFrequency)) /
                         (std::log2(maxFrequency) - std::log2(minFrequency));
        // x position based on frequency range and window size
        float xPosition = window.getSize().x * freqLog;

        // don't draw the first iteration
        // the second iteration draws the first bar, etc.
        if (i > 0) {
          float width = std::max(minBarWidth, xPosition - prevX);
          float centerX = ((xPosition + prevX) / 2.0f) - width / 2.0f;
          // create bar and set properties
          sf::RectangleShape bar;
          bar.setSize(sf::Vector2f(width, prevHeight));
          bar.setOrigin(sf::Vector2f(width / 2.0f, 0));
          bar.setPosition(
              sf::Vector2f(centerX, window.getSize().y - prevHeight));
          // draw on window
          window.draw(bar);
        }

        //
        prevX = xPosition;
        prevHeight = barHeights[i];
      }

      // update screen
      window.display();
    }
  }

  // clean up
  recorder.stop();
}