#include <catch2/catch_all.hpp> // For Catch2 v3.x

// Let's assume you have a simple function you want to test.
// For a real project, this function would likely be part of your Waffle library/executable
// and you would #include its header here.
int add_numbers(int a, int b) {
    return a + b;
}

TEST_CASE("Addition is computed", "[math]") {
    REQUIRE(add_numbers(1, 1) == 2);
    REQUIRE(add_numbers(-1, 1) == 0);
    REQUIRE(add_numbers(0, 0) == 0);
}

TEST_CASE("Edge case for addition", "[edge]") {
    REQUIRE(add_numbers(1000000, 2000000) == 3000000);
    REQUIRE(add_numbers(-1000000, -2000000) == -3000000);
    REQUIRE(add_numbers(0, 1000000) == 1000000);
}
