#include <iostream>
#include <string_view>

#include "poolfactory/result.hpp"

namespace poolfactory {

// =============================================================================
// Pure Functions - No side effects, same input always produces same output
// =============================================================================

/**
 * @brief Pure function: creates a greeting message
 */
auto greet(std::string_view name) -> std::string {
    return std::string("Hello, ") + std::string(name) + "!";
}

/**
 * @brief Pure function: adds two integers
 */
auto add(int a, int b) -> int {
    return a + b;
}

/**
 * @brief Pure function with Result: parses string to integer
 * Returns Result<int> instead of throwing exceptions
 */
auto parse_int(std::string_view input) -> Result<int> {
    try {
        size_t pos = 0;
        int result = std::stoi(std::string(input), &pos);
        if (pos != input.size()) {
            return Result<int>::err("Invalid integer: trailing characters in '" +
                                    std::string(input) + "'");
        }
        return Result<int>::ok(result);
    } catch (const std::exception&) {
        return Result<int>::err("Invalid integer: '" + std::string(input) + "'");
    }
}

} // namespace poolfactory

// =============================================================================
// Main - Side Effect Boundary (all IO happens here)
// =============================================================================

int main() {
    // Call pure functions
    auto message = poolfactory::greet("poolfactory");
    std::cout << message << std::endl;

    // Demonstrate pure arithmetic
    std::cout << "2 + 3 = " << poolfactory::add(2, 3) << std::endl;

    // Use Result for error handling (no exceptions)
    auto result = poolfactory::parse_int("42");
    if (result.is_ok()) {
        std::cout << "Parsed: " << result.value() << std::endl;
    }

    // Demonstrate error case
    auto bad_result = poolfactory::parse_int("not_a_number");
    if (bad_result.is_err()) {
        std::cout << "Error: " << bad_result.error() << std::endl;
    }

    return 0;
}
