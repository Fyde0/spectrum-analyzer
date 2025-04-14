#include "catmullRom.hpp"

// function to interpolate points with the Catmull–Rom spline
// I have no idea what any of this means but it looks very nice
// needs four points and t, call like this:
// for (float t = 0; t <= 1.0f; t += 0.1f) {
//   sf::Vertex v;
//   v.position = catmullRom(p0, p1, p2, p3, t);
//   v.color = sf::Color::White;
//   line.append(v);
// }
sf::Vector2f catmullRom(const sf::Vector2f &p0, const sf::Vector2f &p1,
                        const sf::Vector2f &p2, const sf::Vector2f &p3,
                        float t) {
  float t2 = t * t;
  float t3 = t2 * t;

  return 0.5f * ((2.f * p1) + (-p0 + p2) * t +
                 (2.f * p0 - 5.f * p1 + 4.f * p2 - p3) * t2 +
                 (-p0 + 3.f * p1 - 3.f * p2 + p3) * t3);
}