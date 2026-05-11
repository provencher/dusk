#pragma once

#include <aurora/aurora.h>
#include "f_op/f_op_view.h"

#include <array>
#include <cstdint>
#include <vector>

class dDlst_base_c;

namespace dusk::vr {

using Matrix4x4 = std::array<float, 16>;

struct EyeData {
    AuroraXRView rawView{};
    bool poseValid = false;
    bool fovValid = false;

    // Placeholders for later camera composition work. These remain identity matrices until
    // Dusk's camera/render pass split starts consuming OpenXR eye poses and projections.
    Matrix4x4 viewFromWorld{};
    Matrix4x4 projectionFromView{};
    Matrix4x4 clipFromWorld{};
};

struct RecenterOffset {
    AuroraXRPose pose{};
    bool valid = false;
};

enum class RecenterResult {
    Success,
    Inactive,
    NoTracking,
};

struct WorldProjectedItem {
    dDlst_base_c* drawList = nullptr;
    Matrix4x4 modelFromWorld{};
};

struct EyeViewToken {
    view_class* view = nullptr;
    uint32_t eyeIndex = 0;
    bool active = false;

    lookat_class lookat{};
    s16 bank = 0;
    f32 fovy = 0.0f;
    f32 aspect = 1.0f;
    Mtx44 projMtx{};
    Mtx viewMtx{};
    Mtx invViewMtx{};
    Mtx44 projViewMtx{};
    Mtx viewMtxNoTrans{};
};

void begin_frame() noexcept;

bool requested() noexcept;
bool active() noexcept;
bool should_render() noexcept;
AuroraXRStatus status() noexcept;
const AuroraXRFrameState& frame_state() noexcept;

uint32_t eye_count() noexcept;
const EyeData* eye(uint32_t index) noexcept;
const std::vector<EyeData>& eyes() noexcept;

bool begin_eye_view(view_class& view, uint32_t eyeIndex, EyeViewToken& token) noexcept;
void end_eye_view(EyeViewToken& token) noexcept;

void request_recenter() noexcept;
bool recenter_pending() noexcept;
void clear_recenter_request() noexcept;
RecenterResult recenter_from_latest_hmd_pose() noexcept;
const RecenterOffset& recenter_offset() noexcept;
void set_recenter_offset(const RecenterOffset& offset) noexcept;

void queue_world_projected_2d(dDlst_base_c* drawList) noexcept;
void queue_world_projected_2d(dDlst_base_c* drawList, const Matrix4x4& modelFromWorld) noexcept;
const std::vector<WorldProjectedItem>& world_projected_queue() noexcept;
void clear_world_projected_queue() noexcept;

}  // namespace dusk::vr
