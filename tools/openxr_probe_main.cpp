#include <aurora/aurora.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
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

const char* exercise_eye_targets(uint32_t viewCount) {
  if (viewCount == 0) {
    return "no_views";
  }

  constexpr uint32_t MaxFrames = 240;
  for (uint32_t frame = 0; frame < MaxFrames; ++frame) {
    aurora_update();
    if (!aurora_begin_frame()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(4));
      continue;
    }

    uint32_t renderedEyes = 0;
    if (aurora_xr_should_render()) {
      for (uint32_t eyeIndex = 0; eyeIndex < viewCount; ++eyeIndex) {
        if (aurora_xr_begin_eye(eyeIndex)) {
          ++renderedEyes;
          aurora_xr_end_eye();
        }
      }
    }
    aurora_end_frame();

    if (renderedEyes == viewCount) {
      return "submitted";
    }
    if (renderedEyes != 0) {
      return "partial";
    }
    if (aurora_xr_get_status() == AURORA_XR_LOST) {
      return "lost";
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(4));
  }

  return aurora_xr_is_active() ? "no_eye" : "no_active_frame";
}

struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct Quat {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float w = 1.0f;
};

struct HeadMotionSample {
  AuroraXRView left{};
  AuroraXRView right{};
  Vec3 headPosition{};
  Quat orientation{};
  uint64_t frameIndex = 0;
};

struct HeadMotionResult {
  const char* gate = "not_requested";
  float lateralMeters = 0.0f;
  float verticalMeters = 0.0f;
  float angularRadians = 0.0f;
  float imageDifference = 0.0f;
  float stereoDifference = 0.0f;
  std::filesystem::path firstImage;
  std::filesystem::path lastImage;
  std::filesystem::path deltaImage;
};

float clamp01(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

float quat_dot(Quat a, Quat b) {
  return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

float quat_angle(Quat a, Quat b) {
  const float dot = std::clamp(std::fabs(quat_dot(a, b)), 0.0f, 1.0f);
  return 2.0f * std::acos(dot);
}

Quat normalize(Quat q) {
  const float lenSq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
  if (lenSq <= 0.0f) {
    return {};
  }
  const float invLen = 1.0f / std::sqrt(lenSq);
  return {q.x * invLen, q.y * invLen, q.z * invLen, q.w * invLen};
}

float yaw_from_quat(Quat q) {
  q = normalize(q);
  return std::atan2(2.0f * (q.w * q.y + q.x * q.z), 1.0f - 2.0f * (q.y * q.y + q.x * q.x));
}

float pitch_from_quat(Quat q) {
  q = normalize(q);
  return std::asin(std::clamp(2.0f * (q.w * q.x - q.z * q.y), -1.0f, 1.0f));
}

HeadMotionSample make_head_motion_sample(const AuroraXRFrameState& frameState) {
  HeadMotionSample sample{};
  sample.frameIndex = frameState.frameIndex;
  aurora_xr_get_view(0, &sample.left);
  aurora_xr_get_view(1, &sample.right);
  sample.headPosition = {
      (sample.left.pose.position.x + sample.right.pose.position.x) * 0.5f,
      (sample.left.pose.position.y + sample.right.pose.position.y) * 0.5f,
      (sample.left.pose.position.z + sample.right.pose.position.z) * 0.5f,
  };
  sample.orientation = normalize({
      (sample.left.pose.orientation.x + sample.right.pose.orientation.x) * 0.5f,
      (sample.left.pose.orientation.y + sample.right.pose.orientation.y) * 0.5f,
      (sample.left.pose.orientation.z + sample.right.pose.orientation.z) * 0.5f,
      (sample.left.pose.orientation.w + sample.right.pose.orientation.w) * 0.5f,
  });
  return sample;
}

bool capture_head_motion_sample(HeadMotionSample& outSample, uint32_t maxFrames) {
  for (uint32_t frame = 0; frame < maxFrames; ++frame) {
    aurora_update();
    if (!aurora_begin_frame()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(4));
      continue;
    }

    const AuroraXRFrameState frameState = aurora_xr_get_frame_state();
    const bool valid = aurora_xr_should_render() && frameState.viewCount >= 2;
    if (valid) {
      AuroraXRView left{};
      AuroraXRView right{};
      const bool haveViews = aurora_xr_get_view(0, &left) && aurora_xr_get_view(1, &right);
      if (haveViews && left.positionValid && right.positionValid && left.orientationValid && right.orientationValid) {
        outSample = make_head_motion_sample(frameState);
        aurora_end_frame();
        return true;
      }
    }
    aurora_end_frame();
    std::this_thread::sleep_for(std::chrono::milliseconds(4));
  }
  return false;
}

std::vector<uint8_t> render_head_motion_image(const HeadMotionSample& sample, uint32_t width, uint32_t height) {
  std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 3);
  const uint32_t eyeWidth = width / 2;
  const float yaw = yaw_from_quat(sample.orientation);
  const float pitch = pitch_from_quat(sample.orientation);

  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      const bool rightEye = x >= eyeWidth;
      const uint32_t localX = rightEye ? x - eyeWidth : x;
      const AuroraXRView& view = rightEye ? sample.right : sample.left;
      const float u = static_cast<float>(localX) / static_cast<float>(std::max(1u, eyeWidth - 1));
      const float v = static_cast<float>(y) / static_cast<float>(std::max(1u, height - 1));
      const float lateral = sample.headPosition.x + (rightEye ? 0.08f : -0.08f);
      const float vertical = sample.headPosition.y - 1.6f;
      const float stripe = std::sin((u * 8.0f + lateral * 6.0f + yaw * 2.0f) * 3.14159265f);
      const float horizon = 0.5f + pitch * 2.0f - vertical * 0.25f;
      const float horizonLine = std::fabs(v - horizon) < 0.018f ? 1.0f : 0.0f;
      const float eyeTint = rightEye ? 0.16f : -0.16f;
      const float poseTint = view.pose.position.x * 0.4f + view.pose.position.y * 0.08f;

      const size_t index = (static_cast<size_t>(y) * width + x) * 3;
      pixels[index + 0] = static_cast<uint8_t>(255.0f * clamp01(0.28f + 0.45f * v + 0.20f * stripe + eyeTint));
      pixels[index + 1] = static_cast<uint8_t>(255.0f * clamp01(0.30f + 0.30f * (1.0f - v) + horizonLine * 0.55f));
      pixels[index + 2] = static_cast<uint8_t>(255.0f * clamp01(0.42f + 0.26f * u + 0.22f * poseTint - eyeTint));
    }
  }

  return pixels;
}

std::vector<uint8_t> render_delta_image(const std::vector<uint8_t>& first, const std::vector<uint8_t>& last) {
  std::vector<uint8_t> delta(first.size());
  for (size_t i = 0; i < first.size() && i < last.size(); ++i) {
    delta[i] = static_cast<uint8_t>(std::abs(static_cast<int>(last[i]) - static_cast<int>(first[i])));
  }
  return delta;
}

bool write_ppm(const std::filesystem::path& path, uint32_t width, uint32_t height, const std::vector<uint8_t>& pixels) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return false;
  }
  out << "P6\n" << width << " " << height << "\n255\n";
  out.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
  return static_cast<bool>(out);
}

float normalized_image_difference(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
  if (a.empty() || a.size() != b.size()) {
    return 0.0f;
  }
  uint64_t sum = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    sum += static_cast<uint64_t>(std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])));
  }
  return static_cast<float>(static_cast<double>(sum) / (static_cast<double>(a.size()) * 255.0));
}

float stereo_difference(const std::vector<uint8_t>& pixels, uint32_t width, uint32_t height) {
  const uint32_t eyeWidth = width / 2;
  if (eyeWidth == 0) {
    return 0.0f;
  }
  uint64_t sum = 0;
  uint64_t count = 0;
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < eyeWidth; ++x) {
      const size_t left = (static_cast<size_t>(y) * width + x) * 3;
      const size_t right = (static_cast<size_t>(y) * width + x + eyeWidth) * 3;
      for (uint32_t c = 0; c < 3; ++c) {
        sum += static_cast<uint64_t>(std::abs(static_cast<int>(pixels[left + c]) - static_cast<int>(pixels[right + c])));
        ++count;
      }
    }
  }
  return count == 0 ? 0.0f : static_cast<float>(static_cast<double>(sum) / (static_cast<double>(count) * 255.0));
}

HeadMotionResult exercise_head_motion(const std::filesystem::path& imageDir) {
  HeadMotionResult result{};
  if (!std::filesystem::create_directories(imageDir) && !std::filesystem::exists(imageDir)) {
    result.gate = "image_dir_failed";
    return result;
  }

  HeadMotionSample first{};
  HeadMotionSample last{};
  if (!capture_head_motion_sample(first, 240)) {
    result.gate = "no_initial_pose";
    return result;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));
  if (!capture_head_motion_sample(last, 240)) {
    result.gate = "no_later_pose";
    return result;
  }

  result.lateralMeters = std::fabs(last.headPosition.x - first.headPosition.x);
  result.verticalMeters = std::fabs(last.headPosition.y - first.headPosition.y);
  result.angularRadians = quat_angle(first.orientation, last.orientation);

  constexpr uint32_t ImageWidth = 320;
  constexpr uint32_t ImageHeight = 120;
  const auto firstImage = render_head_motion_image(first, ImageWidth, ImageHeight);
  const auto lastImage = render_head_motion_image(last, ImageWidth, ImageHeight);
  const auto deltaImage = render_delta_image(firstImage, lastImage);
  result.imageDifference = normalized_image_difference(firstImage, lastImage);
  result.stereoDifference = std::max(stereo_difference(firstImage, ImageWidth, ImageHeight),
                                     stereo_difference(lastImage, ImageWidth, ImageHeight));

  result.firstImage = imageDir / "openxr_head_motion_first.ppm";
  result.lastImage = imageDir / "openxr_head_motion_last.ppm";
  result.deltaImage = imageDir / "openxr_head_motion_delta.ppm";
  if (!write_ppm(result.firstImage, ImageWidth, ImageHeight, firstImage) ||
      !write_ppm(result.lastImage, ImageWidth, ImageHeight, lastImage) ||
      !write_ppm(result.deltaImage, ImageWidth, ImageHeight, deltaImage)) {
    result.gate = "image_write_failed";
    return result;
  }

  constexpr float MinLateralMeters = 0.05f;
  constexpr float MinAngularRadians = 0.01f;
  constexpr float MinImageDifference = 0.015f;
  constexpr float MinStereoDifference = 0.015f;
  if (result.lateralMeters < MinLateralMeters) {
    result.gate = "no_lateral_motion";
  } else if (result.angularRadians < MinAngularRadians) {
    result.gate = "no_rotational_motion";
  } else if (result.imageDifference < MinImageDifference) {
    result.gate = "image_static";
  } else if (result.stereoDifference < MinStereoDifference) {
    result.gate = "no_stereo_separation";
  } else {
    result.gate = "validated";
  }
  return result;
}
} // namespace

int main(int argc, char* argv[]) {
  bool allowUnavailable = false;
  bool requireDawnInterop = false;
  bool exerciseEyes = false;
  bool exerciseHeadMotion = false;
  std::filesystem::path headMotionImageDir;
  std::vector<char*> auroraArgv;
  auroraArgv.reserve(static_cast<size_t>(argc));
  auroraArgv.push_back(argv[0]);
  for (int i = 1; i < argc; ++i) {
    if (argv[i] != nullptr && std::string_view{argv[i]} == "--allow-unavailable") {
      allowUnavailable = true;
    } else if (argv[i] != nullptr && std::string_view{argv[i]} == "--require-dawn-interop") {
      requireDawnInterop = true;
    } else if (argv[i] != nullptr && std::string_view{argv[i]} == "--exercise-eye-targets") {
      exerciseEyes = true;
    } else if (argv[i] != nullptr && std::string_view{argv[i]} == "--exercise-head-motion") {
      exerciseHeadMotion = true;
    } else if (argv[i] != nullptr && std::string_view{argv[i]} == "--head-motion-image-dir" && i + 1 < argc) {
      headMotionImageDir = argv[++i];
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
  const char* eyeTargetGate = "not_requested";
  if (exerciseEyes) {
    std::fflush(stdout);
    eyeTargetGate = proofSucceeded ? exercise_eye_targets(viewCount) : "proof_not_cleared";
    std::printf("xr_eye_target_gate=%s\n", eyeTargetGate);
    std::printf("xr_live_gate=%s_%s\n", proof_gate_status(status, message), eyeTargetGate);
  }
  HeadMotionResult headMotionResult{};
  if (exerciseHeadMotion) {
    std::fflush(stdout);
    if (headMotionImageDir.empty()) {
      headMotionImageDir = configDir / "head-motion-images";
    }
    headMotionResult = proofSucceeded ? exercise_head_motion(headMotionImageDir) : HeadMotionResult{.gate = "proof_not_cleared"};
    std::printf("xr_head_motion_gate=%s\n", headMotionResult.gate);
    std::printf("xr_head_motion_lateral_m=%.4f\n", headMotionResult.lateralMeters);
    std::printf("xr_head_motion_vertical_m=%.4f\n", headMotionResult.verticalMeters);
    std::printf("xr_head_motion_angular_rad=%.4f\n", headMotionResult.angularRadians);
    std::printf("xr_head_motion_image_diff=%.4f\n", headMotionResult.imageDifference);
    std::printf("xr_head_motion_stereo_diff=%.4f\n", headMotionResult.stereoDifference);
    std::printf("xr_head_motion_first_image=%s\n", headMotionResult.firstImage.string().c_str());
    std::printf("xr_head_motion_last_image=%s\n", headMotionResult.lastImage.string().c_str());
    std::printf("xr_head_motion_delta_image=%s\n", headMotionResult.deltaImage.string().c_str());
  }

  const bool smokeSatisfied = allowUnavailable && is_unavailable_or_blocked(status);
  const bool dawnInteropSatisfied = !requireDawnInterop || std::string_view{dawnInterop} == "ready";
  const bool eyeTargetsSatisfied = !exerciseEyes || std::string_view{eyeTargetGate} == "submitted";
  const bool headMotionSatisfied = !exerciseHeadMotion || std::string_view{headMotionResult.gate} == "validated";
  aurora_shutdown();
  return dawnInteropSatisfied && eyeTargetsSatisfied && headMotionSatisfied && (proofSucceeded || smokeSatisfied) ? 0 : 2;
}
