#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace fusion_server {

/**
 * This is the forward declaration of the WebSocketSession class.
 */
class WebSocketSession;

namespace system_abstractions {

/**
 * This is the package type used in both WebSocket and HTTP sessions.
 */
using Package = std::shared_ptr<const std::string>;

template <typename... Args>
[[ nodiscard ]] Package make_Package(Args... args) noexcept {
  return std::make_shared<const std::string>(std::forward<Args>(args)...);
}

/**
 * This type is used to create delegates that will be called by WebSocket
 * sessions each time, when a new package arrives.
 */
using IncommingPackageDelegate = std::function< void(Package, WebSocketSession*) >;

}  // namespace system_abstractions

}  // namespace fusion_server