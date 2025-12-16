#pragma once

#include "type_encoder.hpp"
#include <chrono>

#include "field_type.hpp"
#include "builtin_type_encoders.hpp"
#include <boost/endian/conversion.hpp>
#include <pqxx/strconv>

namespace pqxx
{
template<> struct string_traits<std::chrono::system_clock::time_point>
{
  using TP = std::chrono::system_clock::time_point;
  using impl_type = int64_t;
  using impl_traits = string_traits<impl_type>;

  static constexpr bool converts_to_string{true};
  static constexpr bool converts_from_string{true};
};

template<>
struct nullness<std::chrono::system_clock::time_point>
        : no_null<std::chrono::system_clock::time_point>
{};

template<>
struct nullness<std::chrono::utc_clock::time_point>
        : no_null<std::chrono::utc_clock::time_point>
{};

inline std::string to_string(std::chrono::system_clock::time_point const value)
{
  if (is_null(value))
    throw conversion_error{
      "Attempt to convert null " + std::string{type_name<std::chrono::system_clock::time_point>} +
      " to a string."};

  return std::format("{:%F %T}", value);
}

inline std::string to_string(std::chrono::utc_clock::time_point const value)
{
  if (is_null(value))
    throw conversion_error{
      "Attempt to convert null " + std::string{type_name<std::chrono::system_clock::time_point>} +
      " to a string."};

  return std::format("{:%F %T}", value);
}

}
