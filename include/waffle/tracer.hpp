#pragma once

#include <iostream> // For placeholder
#include <string>

namespace Waffle {

class Tracer {
public:
  void log(const std::string &message) {
    std::cout << "Tracer: " << message << std::endl;
  } // Placeholder
};

} // namespace Waffle