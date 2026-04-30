#pragma once

#include "basic_transaction.hpp"
#include "query.hpp"
#include "socket_operations.hpp"

#include <libpq-fe.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/cancel_after.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <iostream>

using namespace std::literals;
using boost::asio::use_awaitable;

namespace postgrespp {

namespace asio = boost::asio;
template<typename T>
using awaitable = asio::awaitable<T, asio::any_io_executor>;

template <class, class>
class basic_transaction;

class result;

struct notification
{
  std::string_view channel;
  std::string_view payload;
  int backend_pid;
};

class basic_connection : public socket_operations<basic_connection> {
  friend class socket_operations<basic_connection>;
  using socket_ops = socket_operations<basic_connection>;
public:
  using io_context_t = boost::asio::io_context;
  using result_t = result;
  using socket_t = boost::asio::ip::tcp::socket;
  using query_t = query;
  using statement_name_t = std::string;

public:
  template <class ExecutorT>
  basic_connection(ExecutorT& exc)
    : socket_{exc}, c_{nullptr} {}

  template <class ExecutorT>
  basic_connection(ExecutorT& exc, const char* const& pgconninfo)
    : socket_{exc} {
    connect(pgconninfo);
  }

  void connect(const char* const pgconninfo) {
    c_ = PQconnectdb(pgconninfo);

    if (status() != CONNECTION_OK)
      throw std::runtime_error{"could not connect: " + std::string{PQerrorMessage(c_)}};

    if (PQsetnonblocking(c_, 1) != 0)
      throw std::runtime_error{"could not set non-blocking: " + std::string{PQerrorMessage(c_)}};

    const auto socket = PQsocket(c_);

    if (socket < 0)
      throw std::runtime_error{"could not get a valid descriptor"};

    socket_.assign(boost::asio::ip::tcp::v4(), socket);
  }

  void disconnect() {
    if (socket_.is_open())
    {
      socket_.cancel();
      socket_.release(); // unassign socket that PQfinish will cleanup
    }
    if (c_)
    {
      PQfinish(c_);
      c_ = nullptr;
    }
  }
  template <class CompletionTokenT>
  auto async_connect(const char* const pgconninfo, CompletionTokenT&& handler)
  {
    auto initiation = [this, pgconninfo](CompletionTokenT&& handler) -> void {
      PGconn* const conn = PQconnectStart(pgconninfo);
      if (!conn) {
          std::cerr << "PQconnectStart failed: out of memory?\n";
          return;
      }
    };

    return boost::asio::async_initiate<CompletionTokenT, void(boost::system::error_code)>(
          initiation, std::forward<CompletionTokenT>(handler));
  }

  ~basic_connection() {
    if (c_)
      PQfinish(c_);
  }

  basic_connection(basic_connection const&) = delete;

  basic_connection(basic_connection&& rhs) noexcept
    : socket_{std::move(rhs.socket_)}
    , c_{std::move(rhs.c_)} {
    rhs.c_ = nullptr;
  }

  basic_connection& operator=(basic_connection const&) = delete;

  basic_connection& operator=(basic_connection&& rhs) noexcept {
    using std::swap;

    swap(socket_, rhs.socket_);
    swap(c_, rhs.c_);

    return *this;
  }

  template <class CompletionTokenT>
  auto async_prepare(
      const statement_name_t& statement_name,
      const query_t& query,
      CompletionTokenT&& handler) {
    const auto res = PQsendPrepare(connection().underlying_handle(),
        statement_name.c_str(),
        query.c_str(),
        0,
        nullptr);

    if (res != 1) {
      throw std::runtime_error{
        "error preparing statement '" + statement_name + "': " + std::string{connection().last_error_message()}};
    }

    return handle_exec(std::forward<CompletionTokenT>(handler));
  }

  /**
   * Creates a read/write transaction. Make sure the created transaction
   * object lives until you are done with it.
   */
  template <
    class Unused_RWT = void,
    class Unused_IsolationT = void,
    class TransactionHandlerT>
  auto async_transaction(TransactionHandlerT&& handler) {
    using txn_t = basic_transaction<Unused_RWT, Unused_IsolationT>;

    auto initiation = [this](auto&& handler) {
      auto w = std::make_shared<txn_t>(*this);
      return w->begin([handler = std::move(handler), w](error_code_t e, auto&& res) mutable { handler(e, std::move(*w)); } );
    };

    return boost::asio::async_initiate<
      TransactionHandlerT, void(error_code_t, txn_t)>(
          initiation, handler);
  }

  template <typename CallableT>
  auto listen(std::string_view channelName, CallableT&& handler) {

    auto initiation = [this, channelName](auto&& completion_handler)
    {
      // LOGF_INFO("listen {}", channelName);
      auto query = std::format("LISTEN {};", channelName);
      const auto res = PQsendQuery(connection().underlying_handle(),
          query.c_str());

      if (res != PGRES_COMMAND_OK) {
        throw std::runtime_error{
          "error executing query: " + std::string{connection().last_error_message()}};
      }

      socket_ops::handle_exec([handler = std::move(completion_handler)](error_code_t ec, result res) mutable
      {
        return handler(ec);
      });
    };
    return boost::asio::async_initiate<CallableT, void(error_code_t)>(initiation, handler);
  }

  template <typename NotifCallableT>
  auto await_notification(NotifCallableT&& handler)
  {
    using notify_ptr = std::unique_ptr<PGnotify, void (*)(void *)>;
    auto initiation = [this](auto&& handler) {
      auto notif = notify_ptr{PQnotifies(underlying_handle()), PQfreemem};
      if (notif) { 
        notification n{notif->relname, notif->extra, notif->be_pid};
        handler(error_code_t{}, n);
        return;
      }

      auto WrappedHdlr = [handler=std::move(handler), this](const error_code_t& ec) mutable
      {
        if (ec)
        {
          if (ec.value() != boost::asio::error::operation_aborted) std::cerr << std::format("notif ec: {} {}", ec.message(), ec.value());
          return handler(ec, notification{});          
        }
        else if (PQconsumeInput(underlying_handle()) != 1) {
          std::cout << std::format("consume input failed...: {}", connection().last_error_message());
          handler(error_code_t{boost::asio::error::network_down, boost::system::system_category()}, notification{});
        }
        else
        {
          auto notif = notify_ptr{PQnotifies(underlying_handle()), PQfreemem};
          if (!notif) { return handler(error_code_t{boost::asio::error::network_down}, notification{}); }
          notification n{notif->relname, notif->extra, notif->be_pid};
          return handler(ec, n);
        }

      };

      socket().async_wait(std::decay_t<socket_t>::wait_read, std::move(WrappedHdlr));
    };

    return boost::asio::async_initiate<NotifCallableT, void(boost::system::error_code, notification)>(
          initiation, handler);
  }

  awaitable<bool> is_alive()
  {
      if (!socket_.is_open())
          co_return false;

      boost::system::error_code ec;

      socket_.non_blocking(true, ec);
      std::size_t n = socket_.receive(asio::buffer((char*)nullptr, 0),
                                    asio::ip::tcp::socket::message_peek, ec);

      if (ec == asio::error::eof) {
          // Peer performed an orderly shutdown
          co_return false;
      }
      if (ec == asio::error::connection_reset || ec == asio::error::connection_aborted) {
          co_return false;
      }
      if (ec && ec != asio::error::would_block && ec != asio::error::try_again) {
          // Some other fatal error
          co_return false;
      }

      try {
        co_await socket().async_wait(std::decay_t<socket_t>::wait_read, 
                                     asio::cancel_after(50ms, use_awaitable));
      } catch (boost::system::system_error& e) {
        if (e.code().value() != 125) {
          std::cerr << "sysexc " << e.what() << ' ' << e.code();
          co_return false;
        }
      }
      try {
        co_await socket().async_wait(std::decay_t<socket_t>::wait_error, 
                                     asio::cancel_after(50ms, use_awaitable));
      } catch (boost::system::system_error& e) {
        if (e.code().value() != 125) {
          std::cerr << std::format("sysexc {} {}", e.what(), e.code().message());
          co_return false;
        }
      }
      try {
        if (PQconsumeInput(underlying_handle()) != 1) { // read from socket into PQ lib buffer; should not affect other calls
          std::cerr << std::format("consume input failed...: '{}'", connection().last_error_message());
          co_return false;
        }
        else
        {
          co_return true;
        }
      } catch (boost::system::system_error& e) {
        if (e.code().value() == 125) co_return true;
        std::cerr << "isalive sysexc " << e.what() << ' ' << e.code();
        co_return false;
      } catch (std::exception& e) {
        std::cerr << "isalive exc " << e.what();
        co_return false;
      }
      co_return true; // Looks OK locally. Not a guarantee if the peer died silently.
  }

  PGconn* underlying_handle() { return c_; }

  const PGconn* underlying_handle() const { return c_; }

  socket_t& socket() { return socket_; }

  const char* last_error_message() const { return PQerrorMessage(underlying_handle()); }

private:
  int status() const
  {
    return PQstatus(c_);
  }

  basic_connection& connection() { return *this; }

private:
  socket_t socket_;

  PGconn* c_ = nullptr;
};

}
