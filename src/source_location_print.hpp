#pragma once

#include <source_location>
#include <string>

inline std::string source_location_to_string(const std::source_location& loc) {
    char buffer[1024];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%s:%u in %s",
                  loc.file_name(),
                  loc.line(),
                  loc.function_name());
    return std::string(buffer);
}
