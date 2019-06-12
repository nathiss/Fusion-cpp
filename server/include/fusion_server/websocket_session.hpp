#pragma once

#include <memory>

#include <boost/asio.hpp>
#include <boost/beast.hpp>

namespace fusion_server {

/**
 * This class represents the WebSocket session between a client and the server.
 */
class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
 public:
  WebSocketSession(const WebSocketSession&) = delete;
  WebSocketSession(WebSocketSession&&) = delete;
  WebSocketSession& operator=(const WebSocketSession&) = delete;
  WebSocketSession& operator=(WebSocketSession&&) = delete;

  /**
   * This constructor takes the overship of the socket connected to a client.
   */
  WebSocketSession(boost::asio::ip::tcp::socket socket) noexcept;

  /**
   * This is the default destructor.
   */
  ~WebSocketSession() noexcept;

  /**
   * This method delegates the write operation to the client.
   * Since Boost::Beast allows only one writing at a time, the packages is
   * always queued and if no writing is taking place the asynchronous write
   * operation is called.
   *
   * @param[in] package
   *   The package to be send to the client. The shared_ptr is used to ensure
   *   that the package will be freed only if it has been sent to all the
   *   specific clients.
   *
   * @note
   *   This method is thread-safe.
   */
  void Write(std::shared_ptr<const std::string> package) noexcept;

  /**
   * This method upgrades the connection to the WebSocket Protocol and performs
   * the asynchronous handshake.
   *
   * @param[in] request
   *   A HTTP Upgrade request.
   */
  template <typename Body, typename Allocator>
  void Run(boost::beast::http::request<Body, boost::beast::http::basic_fields<Allocator>> request) noexcept;

  /**
   * This method returns the oldest package sent by the client. If no package
   * has yet arrived, the nullptr is returned.
   *
   * @return
   *   The oldest package sent by the client is returned. If no package has yet
   *   arrived, the nullptr is returned.
   *
   * @note
   *   This method is thread-safe.
   */
  std::shared_ptr<const std::string> Pop() noexcept;

  /**
   * This method closes the connection immediately. Any asynchronous operation
   * will be cancelled.
   */
  void Close() noexcept;

  /**
   * This method returns a value that indicates whether or not the socket is
   * connected to a client.
   *
   * @return
   *   A value that indicates whether or not the socket is connected to a client
   *   is returned.
   */
  explicit operator bool() const noexcept;

 private:
  /**
   * This method is the callback to asynchronous handshake with the client.
   *
   * @param[in] ec
   *   This is the Boost error code.
   */
  void HandleHandshake(const boost::system::error_code& ec) noexcept;

  /**
   * This method is the callback to asynchronous read from the client.
   *
   * @param[in] ec
   *   This is the Boost error code.
   *
   * @param[in] bytes_transmitted
   *   This is the amount of transmitted bytes.
   */
  void HandleRead(const boost::system::error_code& ec, std::size_t bytes_transmitted) noexcept;

  /**
   * This method is the callback to asynchronous write to the client.
   *
   * @param[in] ec
   *   This is the Boost error code.
   *
   * @param[in] bytes_transmitted
   *   This is the amount of transmitted bytes.
   */
  void HandleWrite(const boost::system::error_code& ec, std::size_t bytes_transmitted) noexcept;

  /**
   * This is the type of structure that contains the private
   * properties of the instance.  It is defined in the implementation
   * and declared here to ensure that it is scoped inside the class.
   */
  struct Impl;

  /**
   * This contains the private properties of the instance.
   */
  std::unique_ptr<Impl> impl_;
};

}  // namespace fusion_server