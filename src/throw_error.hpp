#pragma once

#include "source_location_print.hpp"
#include <iostream>
#include <print>
#include <source_location>

[[noreturn]] inline auto
throw_error(std::source_location loc = std::source_location::current()) {
    std::println(std::cerr,
                 "{} :: Throw without message",
                 source_location_to_string(loc));
    std::abort();
}

[[noreturn]] inline auto
throw_error(std::string_view msg,
            std::source_location loc = std::source_location::current()) {
    std::println(std::cerr, "{} :: {}", source_location_to_string(loc), msg);
    std::abort();
}
