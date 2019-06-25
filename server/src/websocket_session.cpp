#include <cstdlib>

#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <fusion_server/server.hpp>
#include <fusion_server/websocket_session.hpp>

namespace fusion_server {

WebSocketSession::WebSocketSession(boost::asio::ip::tcp::socket socket) noexcept
    : websocket_{std::move(socket)}, strand_{websocket_.get_executor()},
      handshake_complete_{false} {
  delegate_ = Server::GetInstance().Register(this);
}

WebSocketSession::~WebSocketSession() noexcept {
  Server::GetInstance().Unregister(this);
};

void WebSocketSession::Write(Package package) noexcept {
  std::lock_guard l{outgoing_queue_mtx_};
  outgoing_queue_.push(package);

  if (outgoing_queue_.size() > 1) {
    // Means we're already writing.
    return;
  }

  if (!handshake_complete_) {
    // We need to wait for the handshake to complete.
    return;
  }

  websocket_.async_write(
      boost::asio::buffer(*outgoing_queue_.front()),
      boost::asio::bind_executor(
        strand_,
        [self = shared_from_this()](
          const boost::system::error_code& ec,
          std::size_t bytes_transmitted
        ) {
          self->HandleWrite(ec, bytes_transmitted);
        }
      )
  );
}

void WebSocketSession::Close() noexcept {
  try{
    websocket_.close(boost::beast::websocket::close_code::none);
    // Now callers should not performs writing to the WebSocket and should
    // perform reading as long as a read returns boost::beast::websocket::closed
    // error.
  }
  catch (const boost::system::system_error& e) {
    std::cerr << "WebSocketSession::Close: " << e.what() << std::endl;
    // TODO: read if the comment below applies to the WebSocket connections.
    // We do nothing, because
    // https://www.boost.org/doc/libs/1_67_0/doc/html/boost_asio/reference/basic_stream_socket/close/overload1.html
    // [Thrown on failure. Note that, even if the function indicates an error, the underlying descriptor is closed.]
  }
}

WebSocketSession::operator bool() const noexcept {
  return websocket_.is_open();
}

void WebSocketSession::HandleHandshake(const boost::system::error_code& ec) noexcept {
  if (ec) {
    std::cerr << "WebSocketSession::HandleHandshake: " << ec.message() << std::endl;
    // We assume what the session cannot be fixed.
    // TODO: Find out if that's true.
    return;
  }
  handshake_complete_ = true;

  if (std::lock_guard l{outgoing_queue_mtx_}; !outgoing_queue_.empty()) {
    websocket_.async_write(
      boost::asio::buffer(*outgoing_queue_.front()),
      boost::asio::bind_executor(
        strand_,
        [self = shared_from_this()](
          const boost::system::error_code& ec,
          std::size_t bytes_transmitted
        ) {
          self->HandleWrite(ec, bytes_transmitted);
        }
      )
    );
  }

  websocket_.async_read(
    buffer_,
    boost::asio::bind_executor(
      strand_,
      [self = shared_from_this()](
        const boost::system::error_code& ec,
        std::size_t bytes_transmitted
      ) {
        self->HandleRead(ec, bytes_transmitted);
      }
    )
  );
}

void WebSocketSession::HandleRead(const boost::system::error_code& ec,
  [[ maybe_unused ]] std::size_t bytes_transmitted) noexcept {
  // TODO: find out what's that doing.
  boost::ignore_unused(buffer_);

  if (ec == boost::beast::websocket::error::closed) {
    // The WebSocketSession was closed. We don't need to report that.
    return;
  }
  if (ec == boost::asio::error::operation_aborted) {
    // The operation has been canceled due to some server's inner operation
    // (e.g. WebSocketSession::Close() has been called).
    return;
  }
  if (ec) {
    std::cerr << "WebSocketSession::HandleRead: " << ec.message() << std::endl;
    // We assume what the session cannot be fixed.
    // TODO: Find out if that's true.
    return;
  }

  const auto package = boost::beast::buffers_to_string(buffer_.data());
  buffer_.consume(buffer_.size());

  delegate_(std::make_shared<decltype(package)>(std::move(package)), this);

  websocket_.async_read(
    buffer_,
    boost::asio::bind_executor(
      strand_,
      [self = shared_from_this()](
        const boost::system::error_code& ec,
        std::size_t bytes_transmitted
      ) {
        self->HandleRead(ec, bytes_transmitted);
      }
    )
  );
}

void WebSocketSession::HandleWrite(const boost::system::error_code& ec,
  [[ maybe_unused ]] std::size_t bytes_transmitted) noexcept {
  if (ec == boost::beast::websocket::error::closed) {
    // The WebSocketSession was closed. We don't need to report that.
    return;
  }
  if (ec == boost::asio::error::operation_aborted) {
    // The operation has been canceled due to some server's inner operation
    // (e.g. WebSocketSession::Close() has been called).
    return;
  }
  if (ec) {
    std::cerr << "WebSocketSession::HandleWrite: " << ec.message() << std::endl;
    // We assume what the session cannot be fixed.
    // TODO: Find out if that's true.
    return;
  }

  std::lock_guard l{outgoing_queue_mtx_};
  outgoing_queue_.pop();

  if (!outgoing_queue_.empty()) {
    websocket_.async_write(
      boost::asio::buffer(*outgoing_queue_.front()),
      boost::asio::bind_executor(
        strand_,
        [self = shared_from_this()](
          const boost::system::error_code& ec,
          std::size_t bytes_transmitted
        ) {
          self->HandleWrite(ec, bytes_transmitted);
        }
      )
    );
  }
}

}  // namespace fusion_server