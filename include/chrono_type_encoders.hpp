#pragma once

#include "type_encoder.hpp"
#include <chrono>

#include "field_type.hpp"
#include "builtin_type_encoders.hpp"
#include <boost/endian/conversion.hpp>
#include <pqxx/strconv>

namespace pqxx
{

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
      "Attempt to convert null " + std::string{type_name<std::chrono::utc_clock::time_point>} +
      " to a string."};

  return std::format("{:%F %T}", value);
}


template<typename Period>
requires (Period::num == 1) // seconds or smaller granularity
std::string toISO8601(std::chrono::duration<int64_t, Period> v)
{
    using Dur = std::chrono::duration<int64_t, Period>;
    std::stringstream ss;
    ss << 'P';
    auto writePart = [&](auto ratio, const char* unit, bool mandatory = false) {
        auto part = std::chrono::duration_cast<std::chrono::duration<int64_t, decltype(ratio)>>(v);
        if (!mandatory && part.count() == 0)
            return;
        ss << part.count() << unit;
        v -= std::chrono::duration_cast<Dur>(part);
    };
    writePart(std::ratio<86400>{}, "D");
    ss << 'T';
    writePart(std::ratio<3600>{}, "H");
    writePart(std::ratio<60>{}, "M");
    writePart(std::ratio<1>{}, "", true);
    if (v.count() > 0) ss << '.';
    for (int64_t denom = Period::den / 10; v.count(); v = Dur(v.count() % denom), denom /= 10) {
        ss << v.count() / denom;
    }
    ss << 'S';
    return ss.str();
}

template<typename Period>
struct nullness<std::chrono::duration<int64_t, Period>>
        : no_null<std::chrono::duration<int64_t, Period>>
{};

template<typename Period>
inline std::string to_string(std::chrono::duration<int64_t, Period> const value)
{
  if (is_null(value))
    throw conversion_error{
      "Attempt to convert null " + std::string{type_name<std::chrono::utc_clock::duration>} +
      " to a string."};
  
  std::cout << toISO8601(value) << '\n';
  return std::format("{}", toISO8601(value));
}

}
