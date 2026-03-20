#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace shizuru::runtime {

struct PortAddress {
  std::string device_id;
  std::string port_name;

  bool operator==(const PortAddress& o) const {
    return device_id == o.device_id && port_name == o.port_name;
  }
};

struct PortAddressHash {
  std::size_t operator()(const PortAddress& a) const {
    auto h1 = std::hash<std::string>{}(a.device_id);
    auto h2 = std::hash<std::string>{}(a.port_name);
    return h1 ^ (h2 << 16);
  }
};

struct RouteOptions {
  bool requires_control_plane = true;  // false = DMA path
};

struct Route {
  PortAddress source;
  PortAddress destination;
  RouteOptions options;
};

class RouteTable {
 public:
  // Idempotent: adding the same source→destination pair again is a no-op.
  void AddRoute(const PortAddress& source, PortAddress destination,
                RouteOptions options = {}) {
    auto& destinations = routes_[source];
    for (const auto& [dest, _] : destinations) {
      if (dest == destination) { return; }  // already exists
    }
    destinations.emplace_back(std::move(destination), options);
  }

  // No-op if the route doesn't exist.
  void RemoveRoute(const PortAddress& source, const PortAddress& destination) {
    auto it = routes_.find(source);
    if (it == routes_.end()) { return; }
    auto& destinations = it->second;
    destinations.erase(
        std::remove_if(destinations.begin(), destinations.end(),
                       [&](const std::pair<PortAddress, RouteOptions>& entry) {
                         return entry.first == destination;
                       }),
        destinations.end());
    if (destinations.empty()) { routes_.erase(it); }
  }

  // Returns all destinations for a given source port (empty if none).
  [[nodiscard]] std::vector<std::pair<PortAddress, RouteOptions>> Lookup(
      const PortAddress& source) const {
    auto it = routes_.find(source);
    if (it == routes_.end()) { return {}; }
    return it->second;
  }

  // Returns all routes as a flat vector.
  [[nodiscard]] std::vector<Route> AllRoutes() const {
    std::vector<Route> result;
    for (const auto& [source, destinations] : routes_) {
      for (const auto& [dest, opts] : destinations) {
        result.push_back({source, dest, opts});
      }
    }
    return result;
  }

  [[nodiscard]] bool IsEmpty() const { return routes_.empty(); }

 private:
  std::unordered_map<PortAddress,
                     std::vector<std::pair<PortAddress, RouteOptions>>,
                     PortAddressHash>
      routes_;
};

}  // namespace shizuru::runtime
