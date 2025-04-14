#pragma once

#include <SFML/System.hpp>

sf::Vector2f catmullRom(const sf::Vector2f &p0, const sf::Vector2f &p1,
                        const sf::Vector2f &p2, const sf::Vector2f &p3,
                        float t);