#pragma once

#include <cstdlib>
#include <iostream>
#include <string_view>

inline void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "expectation failed: " << message << '\n';
        std::exit(1);
    }
}
