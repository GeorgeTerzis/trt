#pragma once

struct uncopyable {
  uncopyable() = default;
  ~uncopyable() = default;
  uncopyable(const uncopyable&) = delete;
  uncopyable& operator=(const uncopyable&) = delete;
};
