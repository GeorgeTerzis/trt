#include "meta_variants.hpp"
#include "overloaded.hpp"
#include <print>
#include <vector>

int main() {
  using variant = variants<int, float, std::string>;
  variant::t val = "booa";

  using cat = category<variant::t, float, int>;

  // for (auto& val : cat::index_map)
  //   std::println("{}", val);



  std::println("{}", belongs<cat>(val));

  cvisit<cat>(val,
              overloaded{[](int& val) {
                           std::println("int");
                         },
                         [](float& val) {
                           std::println("float");
                         },
                         [](std::string& val) {
                           std::println("string");
                         },
                         [](auto&) {
                           std::println("anything else");
                         }});
}
