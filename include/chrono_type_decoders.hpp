#pragma once

#include <chrono>
#include "type_decoder.hpp"

namespace postgrespp {

template <>
class type_decoder<std::chrono::system_clock::time_point> {
public:
  using Underlying = std::chrono::system_clock::time_point::rep;
  using underlying_decoder_t = type_decoder<Underlying, void>;
  static constexpr std::size_t min_size = sizeof(Underlying);
  static constexpr std::size_t max_size = sizeof(Underlying);
  static constexpr bool nullable = false;

public:
  std::chrono::system_clock::time_point from_binary(const char* data, std::size_t length) {
    auto rep = underlying_decoder_t{}.from_binary(data, length);
    using namespace std::chrono;
    using namespace std::literals;

    return system_clock::time_point{microseconds{rep}} + sys_days(2000y/1/1).time_since_epoch();
  }
};

template <>
class type_decoder<std::chrono::utc_clock::time_point> {
public:
  using Underlying = std::chrono::utc_clock::time_point::rep;
  using pre_decoder_t = type_decoder<std::chrono::system_clock::time_point, void>;
  static constexpr std::size_t min_size = sizeof(Underlying);
  static constexpr std::size_t max_size = sizeof(Underlying);
  static constexpr bool nullable = false;

public:
  std::chrono::utc_clock::time_point from_binary(const char* data, std::size_t length) {
    auto SystemTime = pre_decoder_t{}.from_binary(data, length);
    using namespace std::chrono;
    return utc_clock::from_sys(SystemTime);
  }
};

}
