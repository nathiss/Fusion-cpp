/**
 * @file server.cpp
 *
 * This module is a part of Fusion Server project.
 * It contains the implementation of the Server class.
 *
 * (c) 2019 by Kamil Rusin
 */

#include <string_view>
#include <tuple>
#include <utility>

#include <fusion_server/logger_types.hpp>
#include <fusion_server/package_parser.hpp>
#include <fusion_server/server.hpp>
#include <fusion_server/websocket_session.hpp>

namespace fusion_server {

std::unique_ptr<Server> Server::instance_ = nullptr;

Server& Server::GetInstance() noexcept {
  if (instance_ == nullptr) {
    spdlog::get("server")->info("Creating a new server instance.");
    instance_.reset(new Server);
  }
  return *instance_;
}

boost::asio::io_context& Server::GetIOContext() noexcept {
  return ioc_;
}

auto Server::Register(WebSocketSession* session) noexcept
    -> system_abstractions::IncommingPackageDelegate& {
    unidentified_sessions_mtx_.lock();
    auto [it, took_place] = unidentified_sessions_.insert(session);
    unidentified_sessions_mtx_.unlock();

  if (!took_place) {
    logger_->warn("Second registration of a session {}.", session->GetRemoteEndpoint());
    return unjoined_delegate_;
  }

  sessions_correlation_mtx_.lock();
  sessions_correlation_[session] = {};
  sessions_correlation_mtx_.unlock();

  logger_->debug("New WebSocket session registered {}.", session->GetRemoteEndpoint());

  return unjoined_delegate_;
}

void Server::Unregister(WebSocketSession* session) noexcept {
  // If the game is registered it's in sessions_correlation_ set.
  sessions_correlation_mtx_.lock();
  if (auto it = sessions_correlation_.find(session); it != sessions_correlation_.end()) {
    auto game_name = it->second;
    sessions_correlation_.erase(it);
    sessions_correlation_mtx_.unlock();

    if (!game_name) {
      logger_->debug("Unregistering session {}.", session->GetRemoteEndpoint());
      std::lock_guard l{unidentified_sessions_mtx_};
      unidentified_sessions_.erase(session);
    } else {
      logger_->debug("Removing session {} from game {}.", session->GetRemoteEndpoint(), game_name.value());
      std::lock_guard l{games_mtx_};
      auto& game = games_[game_name.value()];
      game->Leave(session);
      if (game->GetPlayersCount() == 0) {
        logger_->debug("Game {} has no players. Removing.", game_name.value());
          games_.erase(game_name.value());
      }
      return;
    }
  }
  sessions_correlation_mtx_.unlock();
  logger_->warn("Trying to unregister session which is not registered. [{}]",
    session->GetRemoteEndpoint());
}

bool Server::StartAccepting() noexcept {
  logger_->info("Creating a Listener object.");

  // TODO: read the local endpoint from a config file.
  return std::make_shared<Listener>(ioc_, "127.0.0.1", 8080)->Run();
}

Server::Server() noexcept {
  logger_ = spdlog::get("server");

  unjoined_delegate_ = [this](const PackageParser::JSON& package, WebSocketSession* src) {
    logger_->debug("Received a new package from {}.", src->GetRemoteEndpoint());
    auto response = MakeResponse(src, package);
    src->Write(system_abstractions::make_Package(response.dump()));
  };
}

PackageParser::JSON
Server::MakeResponse(WebSocketSession* src, const PackageParser::JSON& request) noexcept {
  const auto make_unidentified = [] {
    PackageParser::JSON ret = PackageParser::JSON::object();
    ret["type"] = "warning";
    ret["message"] = "Received an unidentified package.";
    ret["closed"] = false;
    return ret;
  };

  const auto make_game_full = [](std::size_t id){
    PackageParser::JSON ret = PackageParser::JSON::object();
    ret["id"] = id;
    ret["result"] = "full";
    return ret;
  };

  if (request["type"] == "join") {

    std::string game_name = std::move(request["game"]);
    games_mtx_.lock();
    auto it = games_.find(game_name);
    if (it == games_.end()) {
      it = games_.emplace(game_name, std::make_shared<Game>(game_name)).first;
    }
    games_mtx_.unlock();
    auto join_result = it->second->Join(src);
    if (!join_result) {  // The game is full.
      return std::make_pair(false, make_game_full(request["id"]));
    }
    // If we're here it means the join was successful.
    src->delegate_ = std::get<0>(join_result.value());

    {
      std::lock_guard l{unidentified_sessions_mtx_};
      unidentified_sessions_.erase(src);
    }
    {
      std::lock_guard l{sessions_correlation_mtx_};
      sessions_correlation_[src] = std::move(game_name);
    }

    auto response = [&request, &join_result]{
      PackageParser::JSON ret = PackageParser::JSON::object();
      ret["id"] = request["id"];
      ret["result"] = "joined";
      ret["my_id"] = std::get<2>(join_result.value());
      ret["players"] = std::get<1>(join_result.value())["players"];
      ret["rays"] = std::get<1>(join_result.value())["rays"];
      return ret;
    }();

    return std::make_pair(false, std::move(response));
  }  // "join"

  // If we're here it means we've received an unidentified package.
  logger_->warn("Received an unidentified package from {}. [type={}]",
    src->GetRemoteEndpoint(), request["type"].dump());
  return std::make_pair(false, make_unidentified());
}

}  // namespace fusion_server
