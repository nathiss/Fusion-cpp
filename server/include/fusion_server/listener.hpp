#pragma once

#include <cstdint>

#include <memory>
#include <string_view>

#include <boost/asio.hpp>
#include <boost/beast.hpp>

namespace fusion_server {

/**
 * This class represends the local endpoint used to accept new connections
 * from the clients.
 */
class Listener : public std::enable_shared_from_this<Listener> {
 public:
  Listener(const Listener&) = delete;
  Listener(Listener&&) = delete;

  Listener& operator=(const Listener&) = delete;
  Listener& operator=(Listener&&) = delete;

  /**
   * This constructor may be used for accepting connections on a specific
   * local interace.
   *
   * @param[in] ioc
   *   The context for providing core I/O functionality.
   *
   * @param[in] ip_address
   *   The ip address of a local interface.
   *
   * @param[in] port_numer
   *   A port numer.
   */
  Listener(boost::asio::io_context& ioc, std::string_view ip_address, uint16_t port_numer) noexcept;

  /**
   * This constructor may be used for accepting connections on the given
   * endpoint.
   *
   * @param[in] ioc
   *   The context for providing core I/O functionality.
   *
   * @param[in] endpoint
   *   A local endpoint.
   */
  Listener(boost::asio::io_context& ioc, boost::asio::ip::tcp::endpoint endpoint) noexcept;

  /**
   * This constructor may be used for accepting connection on any local
   * interface.
   *
   * @param[in] ioc
   *   The context for providing core I/O functionality.
   *
   * @param[in] port_numer
   *   A port numer.
   */
  Listener(boost::asio::io_context& ioc, uint16_t port_number) noexcept;

  /**
   * This is the default destructor.
   */
  ~Listener() noexcept;

  /**
   * This method starts the asynchronous accepting loop on the specified
   * endpoint.
   */
  void Run() noexcept;

  /**
   * This is the callback to asynchronous accept of a new connection.
   */
  void HandleAccept(const boost::system::error_code&) noexcept;

 private:

  /**
   * This method opens the acceptor & binds it to the endpoint.
   */
  void OpenAcceptor() noexcept;

  /**
   * The context for providing core I/O functionality.
   */
  boost::asio::io_context& ioc_;

  /**
   * This is the local endpoint on which new connections will be accepted.
   */
  boost::asio::ip::tcp::endpoint endpoint_;

  /**
   * This is the acceptor for accepting new connections.
   */
  boost::asio::ip::tcp::acceptor acceptor_;

  /**
   * This is the socket used to handle a new incomming connection.
   */
  boost::asio::ip::tcp::socket socket_;
};

}  // namespace fusion_server