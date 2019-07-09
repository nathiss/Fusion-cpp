#pragma once

#include <cstdlib>

#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>

#include <fusion_server/package_parser.hpp>
#include <fusion_server/player.hpp>
#include <fusion_server/system_abstractions.hpp>

using fusion_server::system_abstractions::Package;

namespace fusion_server {

/**
 * This is the forward declaration of the WebSocketSession class.
 */
class WebSocketSession;

/**
 * This class represents a game. It creates a common context for at least two
 * clients.
 */
class Game {
 public:
  /**
   * This enum contains the teams' identifiers.
   */
  enum class Team {
    /**
     * This identifies the first team in the game.
     */
    kFirst,

    /**
     * This identifies the second team in the game.
     */
    kSecond,

    /**
     * This indicates that a WebSocketSession should be assigned to a random team.
     */
    kRandom,
  };

  /**
   * This type is a pair of incomming package delegate and the current game
   * state encoded as a JSON object.
   */
  using successful_join_result_t =
  std::pair<system_abstractions::IncommingPackageDelegate&, PackageParser::JSON>;

  /**
   * This type is the return type of the Join method.
   */
  using join_result_t = std::optional<successful_join_result_t>;

  Game(const Game&) noexcept = delete;

  Game& operator=(const Game&) noexcept = delete;

  /**
   * This constructor creates the asynchronous reading delegate.
   */
  Game() noexcept;

  /**
   * This method joins the client to this game and adds its session to the
   * proper team. If the joining was successful it returns a pair of a new
   * incomming package delegate and a JSON object containing information about
   * the current state of the game, otherwise the returned object is in its
   * invalid state.
   *
   * @param[in] session
   *   This is the WebSocket session connected to a client.
   *
   * @param[in] team
   *   This identifies the team, to which the client will be assigned.
   *   The default value indicates that the client will be assigned to a random
   *   team.
   *
   * @return
   *   If the joining was successful pair of a new incomming package delegate
   *   and a JSON object containing information about the current state of the
   *   game is returned, otherwise the returned object is in its invalid state.
   *
   * @note
   *   If a client has already joined to this game, the method does nothing and
   *   returns an invalid state object.
   */
  [[ nodiscard ]] join_result_t
  Join(WebSocketSession *session, Team team = Team::kRandom) noexcept;

  /**
   * This method removes the given session from this game.
   * It returns a indication whether or not the session has been removed.
   *
   * @param[in] session
   *   The session to be removed from this game.
   *
   * @return
   *   A indication whether or not the session has been removed is returned.
   *
   * @note
   *   If the session has not been assigned to this game, the method does
   *   nothing.
   */
  bool Leave(WebSocketSession *session) noexcept;

  /**
   * This method broadcasts the given package to all clients connected to this
   * game.
   *
   * @param[in] package
   *   The package to be broadcasted.
   */
  void BroadcastPackage(Package package) noexcept;

  /**
   * This method returns the amount of players in this game.
   *
   * @return
   *   The amount of players in this game is returned.
   */
  std::size_t GetPlayersCount() const noexcept;

  /**
   * This constant contains the number of players that can be assigned to
   * a team.
   */
  static constexpr size_t kMaxPlayersPerTeam = 5;

 private:
  /**
   * This method returns an indication whether or not the client identified by
   * the given session has already joined to this game.
   *
   * @param[in] session
   *   The WebSocketSession connected to the client.
   *
   * @return
   *   An indication whether or not the client identified by the given session
   *   has already joined to this game is returned.
   */
  bool IsInGame(WebSocketSession *session) const noexcept;

  /**
   * This method returns a JSON object containg an encoded current state of this
   * game.
   *
   * @return
   *   A JSON object containg an encoded current state of this game is returned.
   */
  PackageParser::JSON GetCurrentState() const noexcept;

  /**
   * This set contains the pairs of WebSocket sessions and their roles in the
   * game of the first team.
   */
  std::set<std::pair<WebSocketSession*, std::unique_ptr<Player>>> first_team_;

  /**
   * This mutex is used to synchronise all oprations done on the collection
   * containing the first team.
   */
  mutable std::mutex first_team_mtx_;

  /**
   * This set contains the pairs of WebSocket sessions and their roles in the
   * game of the second team.
   */
  std::set<std::pair<WebSocketSession*, std::unique_ptr<Player>>> second_team_;

  /**
   * This mutex is used to synchronise all oprations done on the collection
   * containing the second team.
   */
  mutable std::mutex second_team_mtx_;

  /**
   * This map contains all rays in the game. The key is the ray's parent which
   * is either a player or another ray.
   */
  std::map <std::variant<Ray*, Player*>, Ray> rays_;

  /**
   * This mutex is used to synchronise all oprations done on the map
   * containing the rays.
   */
  mutable std::mutex rays_mtx_;

  /**
   * This callable object is used as a callback to the asynchronous reading of
   * all clients in this game.
   */
  system_abstractions::IncommingPackageDelegate delegete_;

  /**
   * This is used to parse packages from the clients.
   */
  PackageParser package_parser_;
};

}  // namespace fusion_server