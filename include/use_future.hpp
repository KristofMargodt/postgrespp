#pragma once

#include <boost/asio/use_future.hpp>

namespace postgrespp {

inline
constexpr auto& use_future = boost::asio::use_future;

}
