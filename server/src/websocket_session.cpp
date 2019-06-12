#include <atomic>
#include <functional>
#include <mutex>
#include <queue>
#include <utility>
#include <iostream>

#include <boost/beast.hpp>

#include <fusion_server/websocket_session.hpp>

namespace fusion_server {

struct WebSocketSession::Impl {
  // Methods
  Impl(boost::asio::ip::tcp::socket socket)
      : websocket_{std::move(socket)}, strand_{websocket_.get_executor()} {}

  // Atributes

  /**
   * This is the WebSocket wrapper around the socket connected to a client.
   */
  boost::beast::websocket::stream<boost::asio::ip::tcp::socket> websocket_;

  /**
   * This is the buffer for the incomming packages.
   */
  boost::beast::multi_buffer buffer_;

  /**
   * This is the strand for this instance of WebSocketSession class.
   */
  boost::asio::strand<boost::asio::io_context::executor_type> strand_;

  /**
   * This queue holds all incomming packages, which have not yet been processed.
   */
  std::queue<std::shared_ptr<const std::string>> incomming_queue_;

  /**
   * This is the mutex for incoming queue.
   */
  std::mutex incomming_queue_mtx_;

  /**
   * This queue holds all outgoing packages, which have not yet been sent.
   */
  std::queue<std::shared_ptr<const std::string>> outgoing_queue_;

  /**
   * This is the mutex for outgoing queue.
   */
  std::mutex outgoing_queue_mtx_;

  /**
   * This indicates whether or not the handshake has been completed.
   */
  std::atomic<bool> handshake_complete{false};
};

WebSocketSession::WebSocketSession(boost::asio::ip::tcp::socket socket) noexcept
    : impl_{new Impl{std::move(socket)}} {
}

WebSocketSession::~WebSocketSession() noexcept = default;

void WebSocketSession::Write(std::shared_ptr<const std::string> package) noexcept {
  std::lock_guard l{impl_->outgoing_queue_mtx_};
  impl_->outgoing_queue_.push(package);

  if (impl_->outgoing_queue_.size() > 1) {
    // Means we're already writing.
    return;
  }

  if (!impl_->handshake_complete) {
    // We need to wait for the handshake to complete.
    return;
  }

  impl_->websocket_.async_write(
      boost::asio::buffer(*impl_->outgoing_queue_.front()),
      boost::asio::bind_executor(
        impl_->strand_,
        std::bind(
          &WebSocketSession::HandleWrite,
          shared_from_this(),
          std::placeholders::_1,
          std::placeholders::_2
        )
      )
  );
}

template <typename Body, typename Allocator>
void WebSocketSession::Run(boost::beast::http::request<Body, boost::beast::http::basic_fields<Allocator>> request) noexcept {
  impl_->websocket_.async_accept(
    std::move(request),
    boost::asio::bind_executor(
      impl_->strand_,
      std::bind(
        &WebSocketSession::HandleHandshake,
        shared_from_this(),
        std::placeholders::_1
      )
    )
  );
}

std::shared_ptr<const std::string> WebSocketSession::Pop() noexcept {
  std::lock_guard l{impl_->incomming_queue_mtx_};
  if (impl_->incomming_queue_.empty()) {
    return nullptr;
  }

  auto front = impl_->incomming_queue_.front();
  impl_->incomming_queue_.pop();
  return front;
}

void WebSocketSession::Close() noexcept {
  try{
    impl_->websocket_.close(boost::beast::websocket::close_code::none);
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
  return impl_->websocket_.is_open();
}

void WebSocketSession::HandleHandshake(const boost::system::error_code& ec) noexcept {
  if (ec) {
    std::cerr << "WebSocketSession::HandleHandshake: " << ec.message() << std::endl;
    // We assume what the session cannot be fixed.
    // TODO: Find out if that's true.
    return;
  }

  if (std::lock_guard l{impl_->outgoing_queue_mtx_}; !impl_->outgoing_queue_.empty()) {
    impl_->websocket_.async_write(
      boost::asio::buffer(*impl_->outgoing_queue_.front()),
      boost::asio::bind_executor(
        impl_->strand_,
        std::bind(
          &WebSocketSession::HandleWrite,
          shared_from_this(),
          std::placeholders::_1,
          std::placeholders::_2
        )
      )
    );
  }

  impl_->websocket_.async_read(
    impl_->buffer_,
    boost::asio::bind_executor(
      impl_->strand_,
      std::bind(
        &WebSocketSession::HandleRead,
        shared_from_this(),
        std::placeholders::_1,
        std::placeholders::_2
      )
    )
  );
}

void WebSocketSession::HandleRead(const boost::system::error_code& ec,
  [[ maybe_unused ]] std::size_t bytes_transmitted) noexcept {
  // TODO: find out what's that doing.
  boost::ignore_unused(impl_->buffer_);

  if (ec == boost::beast::websocket::error::closed) {
    // The WebSocketSession was closed. We don't need to report that.
    return;
  }
  if (ec) {
    std::cerr << "WebSocketSession::HandleRead: " << ec.message() << std::endl;
    // We assume what the session cannot be fixed.
    // TODO: Find out if that's true.
    return;
  }

  auto str = boost::beast::buffers_to_string(impl_->buffer_.data());
  impl_->buffer_.consume(impl_->buffer_.size());
  auto shared_str = std::make_shared<const std::string>(std::move(str));

  impl_->incomming_queue_mtx_.lock();
  impl_->incomming_queue_.push(shared_str);
  impl_->incomming_queue_mtx_.unlock();

  impl_->websocket_.async_read(
    impl_->buffer_,
    boost::asio::bind_executor(
      impl_->strand_,
      std::bind(
        &WebSocketSession::HandleRead,
        shared_from_this(),
        std::placeholders::_1,
        std::placeholders::_2
      )
    )
  );
}

void WebSocketSession::HandleWrite(const boost::system::error_code& ec,
  [[ maybe_unused ]] std::size_t bytes_transmitted) noexcept {
  if (ec == boost::beast::websocket::error::closed) {
    // The WebSocketSession was closed. We don't need to report that.
    return;
  }
  if (ec) {
    std::cerr << "WebSocketSession::HandleRead: " << ec.message() << std::endl;
    // We assume what the session cannot be fixed.
    // TODO: Find out if that's true.
    return;
  }

  std::lock_guard l{impl_->outgoing_queue_mtx_};
  impl_->outgoing_queue_.pop();

  if (!impl_->outgoing_queue_.empty()) {
    impl_->websocket_.async_write(
      boost::asio::buffer(*impl_->outgoing_queue_.front()),
      boost::asio::bind_executor(
        impl_->strand_,
        std::bind(
          &WebSocketSession::HandleWrite,
          shared_from_this(),
          std::placeholders::_1,
          std::placeholders::_2
        )
      )
    );
  }
}

}  // namespace fusion_server