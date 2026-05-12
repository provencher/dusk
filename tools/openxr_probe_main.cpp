#include <aurora/aurora.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {
const char* status_name(AuroraXRStatus status) {
  switch (status) {
  case AURORA_XR_DISABLED:
    return "disabled";
  case AURORA_XR_UNAVAILABLE:
    return "unavailable";
  case AURORA_XR_BLOCKED:
    return "blocked";
  case AURORA_XR_READY:
    return "ready";
  case AURORA_XR_ACTIVE:
    return "active";
  case AURORA_XR_LOST:
    return "lost";
  default:
    return "unknown";
  }
}

bool message_contains(const char* message, const char* needle) {
  return message != nullptr && std::strstr(message, needle) != nullptr;
}

bool proof_cleared_swapchains(AuroraXRStatus status, const char* message) {
  return (status == AURORA_XR_READY || status == AURORA_XR_ACTIVE ||
          message_contains(message, "cleared both eye swapchain images successfully"));
}

const char* proof_gate_status(AuroraXRStatus status, const char* message) {
  if (proof_cleared_swapchains(status, message)) {
    return "cleared";
  }
  if (message_contains(message, "XR_ERROR_FORM_FACTOR_UNAVAILABLE")) {
    return "no_hmd";
  }
  if (message_contains(message, "Dawn/OpenXR Vulkan proof is blocked")) {
    return "proof_blocked";
  }
  if (status == AURORA_XR_BLOCKED) {
    return "blocked";
  }
  if (status == AURORA_XR_UNAVAILABLE) {
    return "unavailable";
  }
  if (status == AURORA_XR_DISABLED) {
    return "disabled";
  }
  return "unknown";
}

bool is_unavailable_or_blocked(AuroraXRStatus status) {
  return status == AURORA_XR_UNAVAILABLE || status == AURORA_XR_BLOCKED;
}

const char* dawn_interop_status(const char* message) {
  if (message_contains(message, "patched Dawn exposes Vulkan instance")) {
    return "ready";
  }
  if (message_contains(message, "patched Dawn returned incomplete") ||
      message_contains(message, "patched Dawn Vulkan handle query failed") ||
      message_contains(message, "incomplete Vulkan handles or queue family metadata")) {
    return "incomplete";
  }
  if (message_contains(message, "current Dawn package does not expose")) {
    return "missing";
  }
  if (message_contains(message, "Dawn backend is not Vulkan")) {
    return "not_vulkan";
  }
  if (message_contains(message, "Dawn Vulkan device is not initialized")) {
    return "no_device";
  }
  return "unknown";
}
} // namespace

int main(int argc, char* argv[]) {
  bool allowUnavailable = false;
  bool requireDawnInterop = false;
  std::vector<char*> auroraArgv;
  auroraArgv.reserve(static_cast<size_t>(argc));
  auroraArgv.push_back(argv[0]);
  for (int i = 1; i < argc; ++i) {
    if (argv[i] != nullptr && std::string_view{argv[i]} == "--allow-unavailable") {
      allowUnavailable = true;
    } else if (argv[i] != nullptr && std::string_view{argv[i]} == "--require-dawn-interop") {
      requireDawnInterop = true;
    } else {
      auroraArgv.push_back(argv[i]);
    }
  }

  std::error_code ec;
  const std::filesystem::path configDir = std::filesystem::temp_directory_path(ec) / "dusk-openxr-probe";
  std::filesystem::create_directories(configDir, ec);
  const std::string configPath = configDir.string();

  AuroraConfig config{};
  config.appName = "Dusk OpenXR Probe";
  config.configPath = configPath.c_str();
  config.desiredBackend = BACKEND_VULKAN;
  config.windowWidth = 640;
  config.windowHeight = 480;
  config.windowPosX = -1;
  config.windowPosY = -1;
  config.mem1Size = 0;
  config.mem2Size = 0;
  config.enableOpenXR = true;
  config.requireOpenXR = false;
  config.logLevel = LOG_DEBUG;

  const AuroraInfo info = aurora_initialize(static_cast<int>(auroraArgv.size()), auroraArgv.data(), &config);
  const AuroraXRStatus status = aurora_xr_get_status();
  const char* message = aurora_xr_get_status_message();
  const uint32_t viewCount = aurora_xr_get_view_count();
  const bool proofSucceeded = proof_cleared_swapchains(status, message);

  std::printf("backend=%d\n", static_cast<int>(info.backend));
  std::printf("xr_status=%s\n", status_name(status));
  std::printf("xr_message=%s\n", message != nullptr ? message : "");
  const char* dawnInterop = dawn_interop_status(message);
  std::printf("xr_dawn_interop=%s\n", dawnInterop);
  std::printf("xr_proof_gate=%s\n", proof_gate_status(status, message));
  std::printf("xr_view_count=%u\n", viewCount);
  for (uint32_t i = 0; i < viewCount; ++i) {
    AuroraXRView view{};
    if (aurora_xr_get_view(i, &view)) {
      std::printf("xr_view[%u]=%ux%u samples=%u\n", i, view.recommendedWidth, view.recommendedHeight,
                  view.recommendedSampleCount);
    }
  }

  const bool smokeSatisfied = allowUnavailable && is_unavailable_or_blocked(status);
  const bool dawnInteropSatisfied = !requireDawnInterop || std::string_view{dawnInterop} == "ready";
  aurora_shutdown();
  return dawnInteropSatisfied && (proofSucceeded || smokeSatisfied) ? 0 : 2;
}
