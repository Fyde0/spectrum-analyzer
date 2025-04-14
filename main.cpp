#include "FFT.hpp"
#include "Recorder.hpp"
#include "catmullRom.hpp"
#include <SFML/Graphics.hpp>
#include <SFML/Graphics/Vertex.hpp>
#include <SFML/System.hpp>
#include <SFML/Window.hpp>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

#define DEFAULT_WINDOW_WIDTH 1024
#define DEFAULT_WINDOW_HEIGHT 768
#define FPS 30

#define SMOOTHING_FACTOR 0.5 // lower value = more smoothing
#define TILT 4.5             // dB/oct
#define TILT_REF_FREQ 1000   // Hz

#define SAMPLE_RATE 44100
#define SAMPLE_SIZE 8192

enum Mode { bars, line };
Mode mode = line;

int main(int argc, char *argv[]) {
  // arguments
  if (argc >= 2) {
    std::string arg = argv[1];
    if (arg == "bars") {
      mode = bars;
    } else if (arg == "line") {
      mode = line;
    } else {
      std::cerr << "Unknown mode: " << arg << "\n";
      std::cerr << "Usage: ./analyzer [bars|line]\n";
      return 1;
    }
  }
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
  Recorder recorder(SAMPLE_SIZE);
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
  const double minDb = 55.0;
  const double maxDb = 150.0;
  //
  const float minBarWidth = 2.0;
  // needs to be outside the loop because it will average out for time smoothing
  std::vector<float> yPositions(SAMPLE_SIZE, 0.0);

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
    const std::deque<int16_t> &samples = recorder.getSamples();

    if (!samples.empty()) {
      // calculate fft of current samples, and get magnitudes
      fft.process(samples);
      std::vector<double> magnitudes = fft.getMagnitudes();

      // clear the window
      window.clear(sf::Color::Black);

      // calculate position based on magnitudes
      // each iteration draws the previous item (bar or vertex)
      // adding dummy value at the end for extra iteration to draw the last item
      magnitudes.push_back(0.0);
      // (yPositions is above, outside of loop)
      std::vector<float> xPositions(magnitudes.size(), 0.0);

      for (size_t i = 0; i < magnitudes.size(); ++i) {
        // convert to log scale
        double db = 20.0 * std::log10(magnitudes[i] + 1e-12);
        // calculate frequency of item
        double frequency = static_cast<double>(i * SAMPLE_RATE) / SAMPLE_SIZE;
        // calculate tilt for this frequency
        double tiltDb = TILT * std::log2(frequency / TILT_REF_FREQ);
        // apply tilt
        db += tiltDb;
        // remove everything below minDb
        db = std::max(minDb, db);
        // scale to window size
        double y = ((db - minDb) / (maxDb - minDb)) * window.getSize().y;
        // smooth over time (EMA)
        yPositions[i] =
            SMOOTHING_FACTOR * y + (1 - SMOOTHING_FACTOR) * yPositions[i];

        // frequency to log scale, for positioning and width
        double freqLog = (std::log2(frequency) - std::log2(minFrequency)) /
                         (std::log2(maxFrequency) - std::log2(minFrequency));
        // x position based on frequency range and window size
        xPositions[i] = window.getSize().x * freqLog;
      }

      if (mode == bars) {
        // start from 1 because each iteration we draw the previous item
        for (size_t i = 1; i < magnitudes.size(); ++i) {
          // width based on the position of the previous bar
          float width =
              std::max(minBarWidth, xPositions[i] - xPositions[i - 1]);
          // x is slightly left because of bar width
          float barX =
              ((xPositions[i] + xPositions[i - 1]) / 2.0f) - width / 2.0f;
          // create bar, set properties and draw
          sf::RectangleShape bar;
          bar.setSize(sf::Vector2f(width, yPositions[i - 1]));
          bar.setPosition(
              sf::Vector2f(barX, window.getSize().y - yPositions[i - 1]));

          window.draw(bar);
        }
      }

      if (mode == line) {
        sf::VertexArray lineVert(sf::PrimitiveType::LineStrip,
                                 magnitudes.size());
        for (size_t i = 1; i < magnitudes.size() - 2; ++i) {
          // get four consecutive vectors for Catmull–Rom
          const sf::Vector2f p0 = sf::Vector2f(
              xPositions[i - 1], window.getSize().y - yPositions[i - 1]);
          const sf::Vector2f p1 =
              sf::Vector2f(xPositions[i], window.getSize().y - yPositions[i]);
          const sf::Vector2f p2 = sf::Vector2f(
              xPositions[i + 1], window.getSize().y - yPositions[i + 1]);
          const sf::Vector2f p3 = sf::Vector2f(
              xPositions[i + 2], window.getSize().y - yPositions[i + 2]);

          for (float t = 0; t <= 1.0f; t += 0.1f) {
            sf::Vertex v;
            v.position = catmullRom(p0, p1, p2, p3, t);
            v.color = sf::Color::White;
            lineVert.append(v);
          }
        }
        window.draw(lineVert);
      }

      // update screen
      window.display();
    }
  }

  // clean up
  recorder.stop();
}