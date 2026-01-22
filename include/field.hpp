#pragma once

#include "type_decoder.hpp"

#include <libpq-fe.h>

#include <stdexcept>
#include <pqxx/strconv>
#include "field_type.hpp"

namespace postgrespp {

class field {
public:
  using size_type = std::size_t;

public:
  field(const PGresult* res, size_type row, size_type col)
    : res_{res}
    , row_{row}
    , col_{col} {
  }

  field_type format() const { return field_type{PQfformat(res_, col_)}; }
  
  template <class T>
  T as() const {
    using decoder_t = type_decoder<T>;

    if (!decoder_t::nullable && is_null())
      throw std::length_error{std::format("field {} is null", PQfname(res_, col_))};

    const auto field_length = PQgetlength(res_, row_, col_);
    if (   (format() == field_type::BINARY)
        && !(field_length == 0 && decoder_t::nullable) 
        && (field_length < decoder_t::min_size || field_length > decoder_t::max_size))
      throw std::length_error{"field length " + std::to_string(field_length) + " not in range " +
        std::to_string(decoder_t::min_size) + "-" +
        std::to_string(decoder_t::max_size) + " for field " + PQfname(res_, col_)};

    return unsafe_as<T>();
  }

  template <class T>
  T as(T&& default_value) const {
    if (is_null())
      return std::forward<T>(default_value);
    else
      return as<T>();
  }

  template <class T>
  T unsafe_as() const {
    type_decoder<T> decoder{};
    if (format() == field_type::TEXT)
      return pqxx::from_string<T>(std::string_view(PQgetvalue(res_, row_, col_), PQgetlength(res_, row_, col_)));
    else
      return decoder.from_binary(PQgetvalue(res_, row_, col_), PQgetlength(res_, row_, col_));
  }

  template <class T>
  T unsafe_as(T&& default_value) const {
    if (is_null())
      return std::forward<T>(default_value);
    else
      return unsafe_as<T>();
  }

  bool is_null() const {
    return PQgetisnull(res_, row_, col_);
  }

private:
  const PGresult* const res_;
  const size_type row_;
  const size_type col_;
};

}
