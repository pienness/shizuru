#include "agent_runtime.h"

#include <chrono>
#include <stdexcept>
#include <utility>

#include "async_logger.h"
#include "llm/openai/openai_client.h"
#include "memory/in_memory_store.h"
#include "audit/log_audit_sink.h"
#include "io/tool_dispatcher.h"

namespace shizuru::runtime {

AgentRuntime::AgentRuntime(RuntimeConfig config, services::ToolRegistry& tools)
    : config_(std::move(config)), tools_(tools) {
  core::InitLogger(config_.logger);
}

AgentRuntime::~AgentRuntime() {
  Shutdown();
}

// ---------------------------------------------------------------------------
// Device management
// ---------------------------------------------------------------------------

void AgentRuntime::RegisterDevice(std::unique_ptr<io::IoDevice> device) {
  const std::string id = device->GetDeviceId();
  if (devices_.count(id)) {
    throw std::invalid_argument("Device already registered: " + id);
  }

  // Wire the device's output callback to DispatchFrame.
  device->SetOutputCallback(
      [this](const std::string& device_id, const std::string& port_name,
             io::DataFrame frame) {
        DispatchFrame(device_id, port_name, std::move(frame));
      });

  registration_order_.push_back(id);
  devices_[id] = std::move(device);
}

void AgentRuntime::UnregisterDevice(const std::string& device_id) {
  auto it = devices_.find(device_id);
  if (it == devices_.end()) { return; }

  // Remove all routes that reference this device.
  for (auto& route : route_table_.AllRoutes()) {
    if (route.source.device_id == device_id ||
        route.destination.device_id == device_id) {
      route_table_.RemoveRoute(route.source, route.destination);
    }
  }

  devices_.erase(it);
  registration_order_.erase(
      std::remove(registration_order_.begin(), registration_order_.end(),
                  device_id),
      registration_order_.end());
}

// ---------------------------------------------------------------------------
// Route management
// ---------------------------------------------------------------------------

void AgentRuntime::AddRoute(PortAddress source, PortAddress destination,
                             RouteOptions options) {
  route_table_.AddRoute(std::move(source), std::move(destination), options);
}

void AgentRuntime::RemoveRoute(const PortAddress& source,
                                const PortAddress& destination) {
  route_table_.RemoveRoute(source, destination);
}

// ---------------------------------------------------------------------------
// Backward-compatible public API
// ---------------------------------------------------------------------------

std::string AgentRuntime::StartSession() {
  // Shut down any existing session first.
  if (HasActiveSession()) {
    Shutdown();
  }

  const std::string session_id =
      "session-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count());

  // Build CoreDevice with all session dependencies.
  auto llm    = std::make_unique<services::OpenAiClient>(config_.llm);
  auto io     = std::make_unique<services::ToolDispatcher>(tools_);
  auto memory = std::make_unique<services::InMemoryStore>();
  auto audit  = std::make_unique<services::LogAuditSink>();

  auto core = std::make_unique<CoreDevice>(
      "core", session_id,
      config_.controller, config_.context, config_.policy,
      std::move(llm), std::move(io), std::move(memory), std::move(audit));

  core_device_ = core.get();
  RegisterDevice(std::move(core));

  // Wire CoreDevice text_out → output callback sink.
  // We use a special sentinel device ID "app_output" that is never registered
  // as a real device; DispatchFrame handles it inline.
  AddRoute(PortAddress{"core", "text_out"},
           PortAddress{"app_output", "text_in"},
           RouteOptions{.requires_control_plane = false});

  // Start all devices.
  for (const auto& id : registration_order_) {
    devices_.at(id)->Start();
  }

  LOG_INFO("[{}] Session started: {}", MODULE_NAME, session_id);
  return session_id;
}

void AgentRuntime::SendMessage(const std::string& content) {
  if (!core_device_) {
    LOG_WARN("[{}] SendMessage called with no active session", MODULE_NAME);
    return;
  }

  io::DataFrame frame;
  frame.type = "text/plain";
  frame.payload = std::vector<uint8_t>(content.begin(), content.end());
  frame.source_device = "user";
  frame.source_port = "text";
  frame.timestamp = std::chrono::steady_clock::now();

  // Deliver directly to CoreDevice's text_in port (Requirement 11.6).
  core_device_->OnInput("text_in", std::move(frame));
}

void AgentRuntime::OnOutput(OutputCallback cb) {
  std::lock_guard<std::mutex> lock(output_cb_mutex_);
  output_cb_ = std::move(cb);
}

void AgentRuntime::Shutdown() {
  if (devices_.empty()) { return; }

  // Stop devices in reverse registration order (Requirement 8.6).
  for (auto it = registration_order_.rbegin();
       it != registration_order_.rend(); ++it) {
    auto dev_it = devices_.find(*it);
    if (dev_it != devices_.end()) {
      try {
        dev_it->second->Stop();
      } catch (const std::exception& e) {
        LOG_ERROR("[{}] Error stopping device {}: {}", MODULE_NAME, *it,
                  e.what());
      }
    }
  }

  devices_.clear();
  registration_order_.clear();
  route_table_ = RouteTable{};
  core_device_ = nullptr;

  LOG_INFO("[{}] Session shut down", MODULE_NAME);
}

core::State AgentRuntime::GetState() const {
  if (!core_device_) { return core::State::kTerminated; }
  return core_device_->GetState();
}

bool AgentRuntime::HasActiveSession() const {
  return core_device_ != nullptr;
}

// ---------------------------------------------------------------------------
// Internal routing
// ---------------------------------------------------------------------------

void AgentRuntime::DispatchFrame(const std::string& device_id,
                                  const std::string& port_name,
                                  io::DataFrame frame) {
  PortAddress source{device_id, port_name};
  auto destinations = route_table_.Lookup(source);

  if (destinations.empty()) {
    // No routes — silently discard (Requirement 3.5).
    return;
  }

  for (const auto& [dest, opts] : destinations) {
    // Handle the virtual app_output sink inline.
    if (dest.device_id == "app_output") {
      if (frame.type == "text/plain") {
        RuntimeOutput output;
        output.text = std::string(frame.payload.begin(), frame.payload.end());
        OutputCallback cb;
        {
          std::lock_guard<std::mutex> lock(output_cb_mutex_);
          cb = output_cb_;
        }
        if (cb) { cb(output); }
      }
      continue;
    }

    // Deliver to registered device.
    auto it = devices_.find(dest.device_id);
    if (it == devices_.end()) {
      LOG_WARN("[{}] Route destination not found: {}", MODULE_NAME,
               dest.device_id);
      continue;
    }

    try {
      // Zero transformation: frame passes through untouched (Requirement 8.1).
      it->second->OnInput(dest.port_name, frame);
    } catch (const std::exception& e) {
      LOG_ERROR("[{}] Error delivering frame to {}:{} — {}", MODULE_NAME,
                dest.device_id, dest.port_name, e.what());
    }
  }
}

}  // namespace shizuru::runtime
