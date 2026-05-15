#include "FFT.hpp"
#include "Recorder.hpp"
#include "catmullRom.hpp"
#include <SFML/Graphics.hpp>
#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/Vertex.hpp>
#include <SFML/System.hpp>
#include <SFML/Window.hpp>
#include <algorithm>
#include <iostream>
#include <map>
#include <sys/types.h>

#define WINDOW_TITLE "Spectrum Analyzer"

// Debug
#define PRINT_FPS false

// Audio
#define SAMPLE_RATE 44100
#define SAMPLE_SIZE 8192

// Spectrum analyzer
#define SMOOTHING_FACTOR 0.5 // lower value = more smoothing
#define TILT 4.5             // dB/oct
#define TILT_REF_FREQ 1000   // Hz

enum Mode { spectrum, spectrogram, oscilloscope, modeCount };
std::map<Mode, std::string> modeToString = {{spectrum, "spectrum"},
                                            {spectrogram, "spectrogram"},
                                            {oscilloscope, "oscilloscope"},
                                            {modeCount, ""}};
Mode mode = spectrogram;

enum SpectrumMode { bars, line };
SpectrumMode spectrumMode = line;

u_int8_t fps = 60;

bool fillEnabled = true;

// horizontal zoom is kinda bad
uint8_t oscilloscopeHZoom = 1;
uint8_t oscilloscopeVZoom = 1;

bool fullscreen = false;

int main(int argc, char *argv[]) {

  // arguments
  for (int i = 1; i < argc; ++i) {

    std::string arg = argv[i];
    if (arg == "--fullscreen" || arg == "--fs") {
      fullscreen = true;
    } else if (arg == "bars") {
      spectrumMode = bars;
    } else if (arg == "line") {
      spectrumMode = line;
    } else {
      std::cerr << "Unknown option: " << arg << "\n";
      std::cerr << "Usage: ./analyzer [bars|line] [--fullscreen]\n";
      return 1;
    }
  }

  // set up window
  sf::State windowState = sf::State::Windowed;
  sf::VideoMode windowMode = sf::VideoMode::getDesktopMode();
  if (fullscreen) {
    windowState = sf::State::Fullscreen;
    windowMode = sf::VideoMode::getFullscreenModes()[0];
  }
  sf::RenderWindow window(sf::VideoMode(windowMode), WINDOW_TITLE,
                          sf::Style::Default, windowState);
  // fps limit
  window.setFramerateLimit(fps);
  // set up view, this is to handle window resizing properly
  sf::FloatRect viewArea(sf::Vector2f(0, 0),
                         sf::Vector2f(window.getSize().x, window.getSize().y));
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
      std::find(devices.begin(), devices.end(), currentDevice) -
      devices.begin();

  // fft
  FFT fft(SAMPLE_SIZE);

  // variables
  const double minFrequency = 20.0;
  const double maxFrequency = SAMPLE_RATE / 2.0; // nyquist
  const double logMin = std::log2(minFrequency);
  const double logMax = std::log2(maxFrequency);
  const double logRange = logMax - logMin;
  //
  const double minDb = 55.0;
  const double maxDb = 150.0;
  //
  const float minBarWidth = 2.0;
  // needs to be outside the loop because it will average out for time smoothing
  std::vector<float> yPositions(SAMPLE_SIZE, 0.0);
  //
  float spectrogramColumnSpacing = 2.0f;
  size_t maxSpectrogramColumns =
      static_cast<size_t>(window.getSize().x / spectrogramColumnSpacing);
  std::deque<sf::VertexArray> spectrogramColumns;

  sf::Image spectrogramImage;
  sf::Texture spectrogramTexture;
  sf::Sprite spectrogramSprite(spectrogramTexture);
  int spectrogramCursor = 0; // which column to write next
  int spectrogramWidth = 0;  // set once on init

  if (!spectrogramTexture.resize(
          sf::Vector2u(window.getSize().x, window.getSize().y))) {
    std::cout << "Failed resizing the spectrogram texture!" << std::endl;
  }

  spectrogramTexture.setRepeated(false); // we handle the split manually now
  spectrogramSprite.setTexture(spectrogramTexture);
  spectrogramCursor = 0;

  // TODO precalculate stuff here

  // FPS counter
  using clock = std::chrono::high_resolution_clock;
  int frameCount = 0;
  double fps = 0.0;
  auto start = clock::now();

  while (window.isOpen()) {

    // for FPS counter
    if (PRINT_FPS) {
      frameCount++;
    }

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

        if (mode == spectrogram) {
          if (!spectrogramTexture.resize(
                  sf::Vector2u(resized->size.x, resized->size.y))) {
            std::cout << "Failed resizing the spectrogram texture!"
                      << std::endl;
          }
          // TODO clear spectrogram here?
        }
      }

      // press Q or Esc to quit
      if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Q) ||
          sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Escape)) {
        window.close();
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

      // press F to toggle fill
      if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F)) {
        fillEnabled = !fillEnabled;
        if (fillEnabled) {
          std::cout << "Fill: enabled" << "\n";
        } else {
          std::cout << "Fill: disabled" << "\n";
        }
        sf::sleep(sf::milliseconds(200));
      }

      // press M to switch mode
      if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::M)) {
        mode = static_cast<Mode>((static_cast<int>(mode) + 1) % modeCount);
        std::cout << "Mode: " << modeToString[mode] << "\n";
        sf::sleep(sf::milliseconds(200));
      }

      // press arrows for zoom
      if (mode == oscilloscope) {
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Right)) {
          if (oscilloscopeHZoom < 20) {
            oscilloscopeHZoom += 2;
          }
          std::cout << "Horizontal zoom: " << std::to_string(oscilloscopeHZoom)
                    << "\n";
          sf::sleep(sf::milliseconds(200));
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Left)) {
          if (oscilloscopeHZoom > 1) {
            oscilloscopeHZoom -= 2;
          }
          std::cout << "Horizontal zoom: " << std::to_string(oscilloscopeHZoom)
                    << "\n";
          sf::sleep(sf::milliseconds(200));
        }

        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Up)) {
          if (oscilloscopeVZoom < 10) {
            oscilloscopeVZoom++;
          }
          std::cout << "Vertical zoom: " << std::to_string(oscilloscopeVZoom)
                    << "\n";
          sf::sleep(sf::milliseconds(200));
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Down)) {
          if (oscilloscopeVZoom > 1) {
            oscilloscopeVZoom--;
          }
          std::cout << "Vertical zoom: " << std::to_string(oscilloscopeVZoom)
                    << "\n";
          sf::sleep(sf::milliseconds(200));
        }
      }

      // press . and , for fps
      if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Period)) {
        if (fps < 60) {
          fps += 30;
          window.setFramerateLimit(fps);
        }
        std::cout << "FPS limit: " << std::to_string(fps) << "\n";
        sf::sleep(sf::milliseconds(200));
      }
      if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Comma)) {
        if (fps > 30) {
          fps -= 30;
          window.setFramerateLimit(fps);
        }
        std::cout << "FPS limit: " << std::to_string(fps) << "\n";
        sf::sleep(sf::milliseconds(200));
      }
    }

    // get current audio samples from audio input
    const std::deque<int16_t> samples = recorder.getSamples();

    // check if all the samples are zero
    // we don't need to calculate and draw if there's only silence
    bool allZero = true;
    for (int sample : samples) {
      if (sample != 0) {
        allZero = false;
        break;
      }
    }

    // clear the window
    // do this before the if or the last frame gets stuck when there's no sound
    window.clear(sf::Color::Black);

    if (!samples.empty() && !allZero) {
      if (mode == spectrum) {
        // calculate fft of current samples, and get magnitudes
        fft.process(samples);
        std::vector<double> magnitudes = fft.getMagnitudes();

        // calculate position based on magnitudes
        // each iteration draws the previous item (bar or vertex)
        // adding dummy value at the end for extra iteration to draw the last
        // item
        magnitudes.push_back(0.0);
        // (yPositions is above, outside of loop)
        std::vector<float> xPositions(magnitudes.size(), 0.0);

        for (size_t i = 1; i < magnitudes.size(); ++i) {
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
          double freqLog = (std::log2(frequency) - logMin) / logRange;
          // x position based on frequency range and window size
          xPositions[i] = window.getSize().x * freqLog;
        }

        if (spectrumMode == bars) {
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

        if (spectrumMode == line) {
          // line
          sf::VertexArray lineVert(sf::PrimitiveType::LineStrip,
                                   magnitudes.size());
          // filled part
          sf::VertexArray fill(sf::PrimitiveType::TriangleStrip);

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

            // interpolate and make vertex for line and fill
            for (float t = 0; t <= 1.0f; t += 0.1f) {
              sf::Vertex v;
              v.position = catmullRom(p0, p1, p2, p3, t);
              v.color = sf::Color(255, 255, 255, 200);
              lineVert.append(v);

              if (fillEnabled) {
                v.color.a = 63;
                fill.append(v);
                v.position.y = window.getSize().y;
                fill.append(v);
              }
            }
          }
          window.draw(lineVert);

          if (fillEnabled) {
            window.draw(fill);
          }
        }
      }

      if (mode == spectrogram) {

        fft.process(samples);
        std::vector<double> magnitudes = fft.getMagnitudes();

        // new column
        sf::Image columnImage(sf::Vector2u(1, window.getSize().y),
                              sf::Color::Black);

        for (size_t i = 1; i < magnitudes.size(); ++i) {
          double frequency = static_cast<double>(i * SAMPLE_RATE) / SAMPLE_SIZE;
          double freqLog = (std::log2(frequency) - logMin) / logRange;
          double db = 20.0 * std::log10(magnitudes[i] + 1e-12);
          double tiltDb = TILT * std::log2(frequency / TILT_REF_FREQ);
          db += tiltDb;
          db = std::max(minDb, db);
          float normDb =
              std::clamp((float)((db - minDb) / (maxDb - minDb)), 0.f, 1.f);

          uint8_t r = static_cast<uint8_t>(255 * normDb);
          uint8_t g = static_cast<uint8_t>(255 * std::sqrt(normDb));
          uint8_t b = static_cast<uint8_t>(255 * (1.0f - normDb));

          // low frequencies at the bottom
          unsigned int y =
              window.getSize().y -
              static_cast<unsigned int>(window.getSize().y * freqLog);
          // don't draw out of the window
          y = std::min(y, window.getSize().y - 1);

          columnImage.setPixel(sf::Vector2u(0, y), sf::Color(r, g, b, 255));
        }

        // update the texture with the new column at the cursor's position
        spectrogramTexture.update(columnImage,
                                  sf::Vector2u(spectrogramCursor, 0));

        // advance the cursor and wrap around at the end (the texture is a ring
        // buffer)
        spectrogramCursor = (spectrogramCursor + 1) % window.getSize().x;

        // since the texture is a ring buffer it needs to be drawn in two parts
        // the old portion is from the cursor to the last column
        // the new portion is from the first column to the cursor

        // old portion, to draw on the left
        int leftWidth = window.getSize().x - spectrogramCursor;
        if (leftWidth > 0) {
          spectrogramSprite.setTextureRect(
              sf::IntRect({spectrogramCursor, 0},
                          {leftWidth, static_cast<int>(window.getSize().y)}));
          spectrogramSprite.setPosition(sf::Vector2f(0.f, 0.f));
          window.draw(spectrogramSprite);
        }

        // new portion, to draw on the right
        int rightWidth = spectrogramCursor;
        if (rightWidth > 0) {
          spectrogramSprite.setTextureRect(sf::IntRect(
              {0, 0}, {rightWidth, static_cast<int>(window.getSize().y)}));
          spectrogramSprite.setPosition(sf::Vector2f((float)leftWidth, 0.f));
          window.draw(spectrogramSprite);
        }
      }

      if (mode == oscilloscope) {

        // line
        sf::VertexArray line(sf::PrimitiveType::LineStrip);
        // fill
        sf::VertexArray fill(sf::PrimitiveType::TriangleStrip);

        // total samples to draw, divide by horizontal zoom
        const size_t numSamplesToDraw =
            std::min(samples.size(), static_cast<ulong>(window.getSize().x) /
                                         oscilloscopeHZoom);

        for (size_t i = 0; i < numSamplesToDraw; i++) {
          // map samples to "pixels"
          size_t sampleIndex = i * samples.size() / numSamplesToDraw;
          int16_t sample = samples[sampleIndex];

          // apply vertical zoom
          sample = sample * oscilloscopeVZoom;

          // map sample from -32768 32767 to 0 windowHeight
          float y = ((sample + 32768.f) / 65535.f) * window.getSize().y;

          // flip Y
          y = window.getSize().y - y;

          sf::Vertex v;
          // spread out based on hzoom
          v.position = sf::Vector2f(i * oscilloscopeHZoom, y);
          v.color = sf::Color(255, 255, 255, 200);
          line.append(v);

          if (fillEnabled) {
            v.color.a = 63;
            fill.append(v);
            v.position.y = window.getSize().y / 2.0;
            fill.append(v);
          }
        }

        window.draw(line);

        if (fillEnabled) {
          window.draw(fill);
        }
      }
    }
    // update screen
    window.display();

    // print FPS
    if (PRINT_FPS) {
      auto now = clock::now();
      std::chrono::duration<double> elapsed = now - start;
      if (elapsed.count() >= 1.0) {
        fps = frameCount / elapsed.count();
        std::cout << "FPS: " << fps << std::endl;
        frameCount = 0;
        start = now;
      }
    }
  }

  // clean up
  recorder.stop();
}