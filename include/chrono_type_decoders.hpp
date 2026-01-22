#pragma once

#include <chrono>
#include "type_decoder.hpp"
#include <spanstream>

using namespace std::literals;

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

template<typename Period>
requires (Period::num == 1) // seconds or smaller granularity
class type_decoder<std::chrono::duration<int64_t, Period>> {
public:
  using Type = std::chrono::duration<int64_t, Period>;
  using Underlying = uint8_t[16];
  using pre_decoder_t = type_decoder<Underlying, void>;
  static constexpr std::size_t min_size = sizeof(Underlying);
  static constexpr std::size_t max_size = sizeof(Underlying);
  static constexpr bool nullable = false;

public:
  Type from_binary(const char* data, std::size_t length) {
    // only interpret the lower 8 bytes of the full 16 bytes postgresql supports
    using namespace boost::endian;
    auto Dur = endian_load<int64_t, sizeof(int64_t), order::big>(reinterpret_cast<unsigned const char*>(data));

    using namespace std::chrono;
    return duration_cast<Type>(microseconds{Dur});
  }
};

}

namespace pqxx
{

template<typename Period>
requires (Period::num == 1) // seconds or smaller granularity
struct string_traits<std::chrono::duration<int64_t, Period>>
{
  using Dur = std::chrono::duration<int64_t, Period>;
  static constexpr bool converts_to_string{false};
  static constexpr bool converts_from_string{true};

  static Dur from_string(std::string_view text)
  {
    Dur d{};
    std::ispanstream iss(text);
    std::tm tm{.tm_mday = 1, .tm_year = 1970 - 1900}; // epoch, tm_year is years since 1900
    if (!(iss >> std::get_time(&tm, "%H:%M:%S")))
      throw std::invalid_argument(std::format("{} is not parsable as a duration", text));
    d += std::chrono::seconds(std::mktime(&tm));
    if (iss.eof()) return d;
    if (iss.get() != '.') return d;

    std::string zz;
    if (!(iss >> zz)) throw std::invalid_argument(std::format("{} is not parsable as a duration (sub-second)", text));

    zz.resize(log10(Dur::period::den),'0');
    size_t zeconds = 0;
    try { 
      zeconds = std::stoul(zz); 
    } catch (const std::exception&) {
      return d;
    }
    d += Dur(zeconds);
    return d;
  }

  // static char *into_buf(char *begin, char *end, Dur const &value)
  // {
  //   if (internal::cmp_greater_equal(std::size(value), end - begin))
  //     throw conversion_overrun{
  //       "Could not convert string to string: too long for buffer."};
  //   // Include the trailing zero.
  //   value.copy(begin, std::size(value));
  //   begin[std::size(value)] = '\0';
  //   return begin + std::size(value) + 1;
  // }

  // static zview to_buf(char *begin, char *end, Dur const &value)
  // {
  //   return generic_to_buf(begin, end, value);
  // }

  // static std::size_t size_buffer(Dur const &value) noexcept
  // {
  //   return std::size(value) + 1;
  // }
};

template<>
struct string_traits<std::chrono::system_clock::time_point>
{
  using TP = std::chrono::system_clock::time_point;
  static constexpr bool converts_to_string{false};
  static constexpr bool converts_from_string{true};

  static TP from_string(std::string_view text)
  {
    TP tp{};
    std::ispanstream iss(text);
    std::tm tm{};
    if (!(iss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S")))
      return tp;
    tp += std::chrono::seconds(std::mktime(&tm));
    if (iss.eof()) return tp;

    if (iss.get() != '.') return tp;
    std::string zz;
    if (!(iss >> zz)) throw std::invalid_argument(std::format("{} is not parsable as a timepoint (sub-second)", text));

    zz.resize(log10(TP::period::den),'0');
    size_t zeconds = 0;
    try { 
      zeconds = std::stoul(zz); 
    } catch (const std::exception&) {
      return tp;
    }
    tp += TP::duration(zeconds);
    return tp;
  }

  // static char *into_buf(char *begin, char *end, Dur const &value)
  // {
  //   if (internal::cmp_greater_equal(std::size(value), end - begin))
  //     throw conversion_overrun{
  //       "Could not convert string to string: too long for buffer."};
  //   // Include the trailing zero.
  //   value.copy(begin, std::size(value));
  //   begin[std::size(value)] = '\0';
  //   return begin + std::size(value) + 1;
  // }

  // static zview to_buf(char *begin, char *end, Dur const &value)
  // {
  //   return generic_to_buf(begin, end, value);
  // }

  // static std::size_t size_buffer(Dur const &value) noexcept
  // {
  //   return std::size(value) + 1;
  // }
};

template<>
struct string_traits<std::chrono::utc_clock::time_point>
{
  using TP = std::chrono::utc_clock::time_point;
  static constexpr bool converts_to_string{false};
  static constexpr bool converts_from_string{true};

  static TP from_string(std::string_view text)
  {
    auto sysTime = string_traits<std::chrono::system_clock::time_point>::from_string(text);

    if (sysTime >= std::chrono::system_clock::time_point::max() - 1h) return TP::max(); // reason 1h: the check should be 1us + amount of leap seconds at max, but that may change in the future, so rounding to 1h which is safe (supporting times within 1h of max is also not a use case)
    return std::chrono::utc_clock::from_sys(sysTime);
  }

  // static char *into_buf(char *begin, char *end, Dur const &value)
  // {
  //   if (internal::cmp_greater_equal(std::size(value), end - begin))
  //     throw conversion_overrun{
  //       "Could not convert string to string: too long for buffer."};
  //   // Include the trailing zero.
  //   value.copy(begin, std::size(value));
  //   begin[std::size(value)] = '\0';
  //   return begin + std::size(value) + 1;
  // }

  // static zview to_buf(char *begin, char *end, Dur const &value)
  // {
  //   return generic_to_buf(begin, end, value);
  // }

  // static std::size_t size_buffer(Dur const &value) noexcept
  // {
  //   return std::size(value) + 1;
  // }
};

}
