#include "dusk/vr/vr.hpp"

#include "m_Do/m_Do_mtx.h"

#include <cmath>

namespace dusk::vr {
namespace {

constexpr float kMetersToGameUnits = 100.0f;
constexpr float kPi = 3.14159265358979323846f;

AuroraXRQuaternionf yaw_quaternion(float yaw) noexcept {
    const float halfYaw = yaw * 0.5f;
    return {0.0f, std::sin(halfYaw), 0.0f, std::cos(halfYaw)};
}

constexpr Matrix4x4 identity_matrix() noexcept {
    return Matrix4x4{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
}

struct State {
    AuroraXRFrameState frameState{};
    std::vector<EyeData> eyes;
    RecenterOffset recenterOffset{};
    bool recenterPending = false;
    std::vector<WorldProjectedItem> worldProjectedQueue;
};

State g_state;

float square(float value) noexcept { return value * value; }

AuroraXRQuaternionf normalized(AuroraXRQuaternionf q) noexcept {
    const float lenSq = square(q.x) + square(q.y) + square(q.z) + square(q.w);
    if (lenSq <= 0.0f) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }
    const float invLen = 1.0f / std::sqrt(lenSq);
    return {q.x * invLen, q.y * invLen, q.z * invLen, q.w * invLen};
}

AuroraXRQuaternionf conjugate(AuroraXRQuaternionf q) noexcept {
    return {-q.x, -q.y, -q.z, q.w};
}

AuroraXRQuaternionf multiply(AuroraXRQuaternionf a, AuroraXRQuaternionf b) noexcept {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

AuroraXRVector3f rotate(AuroraXRQuaternionf orientation, AuroraXRVector3f value) noexcept {
    const AuroraXRQuaternionf q = normalized(orientation);
    const AuroraXRQuaternionf v{value.x, value.y, value.z, 0.0f};
    const AuroraXRQuaternionf result = multiply(multiply(q, v), conjugate(q));
    return {result.x, result.y, result.z};
}

AuroraXRPose recentered_pose(const AuroraXRPose& pose) noexcept {
    if (!g_state.recenterOffset.valid) {
        return pose;
    }

    const AuroraXRQuaternionf inverseOrigin = conjugate(normalized(g_state.recenterOffset.pose.orientation));
    const AuroraXRVector3f delta{
        pose.position.x - g_state.recenterOffset.pose.position.x,
        pose.position.y - g_state.recenterOffset.pose.position.y,
        pose.position.z - g_state.recenterOffset.pose.position.z,
    };
    return {
        .orientation = normalized(multiply(inverseOrigin, normalized(pose.orientation))),
        .position = rotate(inverseOrigin, delta),
    };
}

Matrix4x4 matrix4_from_mtx(const Mtx m) noexcept {
    return Matrix4x4{
        m[0][0], m[0][1], m[0][2], m[0][3],
        m[1][0], m[1][1], m[1][2], m[1][3],
        m[2][0], m[2][1], m[2][2], m[2][3],
        0.0f, 0.0f, 0.0f, 1.0f,
    };
}

Matrix4x4 matrix4_from_mtx44(const Mtx44 m) noexcept {
    return Matrix4x4{
        m[0][0], m[0][1], m[0][2], m[0][3],
        m[1][0], m[1][1], m[1][2], m[1][3],
        m[2][0], m[2][1], m[2][2], m[2][3],
        m[3][0], m[3][1], m[3][2], m[3][3],
    };
}

void copy_mtx44(const Mtx44 src, Mtx44 dst) noexcept {
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            dst[row][column] = src[row][column];
        }
    }
}

void reset_eye_placeholders(EyeData& eye) noexcept {
    const Matrix4x4 identity = identity_matrix();
    eye.viewFromWorld = identity;
    eye.projectionFromView = identity;
    eye.clipFromWorld = identity;
}

void save_view(EyeViewToken& token, view_class& view, uint32_t eyeIndex) noexcept {
    token.view = &view;
    token.eyeIndex = eyeIndex;
    token.active = false;
    token.lookat = view.lookat;
    token.bank = view.bank;
    token.fovy = view.fovy;
    token.aspect = view.aspect;
    copy_mtx44(view.projMtx, token.projMtx);
    MTXCopy(view.viewMtx, token.viewMtx);
    MTXCopy(view.invViewMtx, token.invViewMtx);
    copy_mtx44(view.projViewMtx, token.projViewMtx);
    MTXCopy(view.viewMtxNoTrans, token.viewMtxNoTrans);
}

void restore_view(const EyeViewToken& token) noexcept {
    if (token.view == nullptr) {
        return;
    }

    view_class& view = *token.view;
    view.lookat = token.lookat;
    view.bank = token.bank;
    view.fovy = token.fovy;
    view.aspect = token.aspect;
    copy_mtx44(token.projMtx, view.projMtx);
    MTXCopy(token.viewMtx, view.viewMtx);
    MTXCopy(token.invViewMtx, view.invViewMtx);
    copy_mtx44(token.projViewMtx, view.projViewMtx);
    MTXCopy(token.viewMtxNoTrans, view.viewMtxNoTrans);
}

bool build_eye_projection(view_class& view, const AuroraXRView& xrView) noexcept {
    if (!xrView.fovValid) {
        return false;
    }

    const float nearZ = view.near_ > 0.01f ? view.near_ : 0.01f;
    const float farZ = view.far_ > nearZ + 1.0f ? view.far_ : nearZ + 1.0f;
    const float left = std::tan(xrView.fov.angleLeft) * nearZ;
    const float right = std::tan(xrView.fov.angleRight) * nearZ;
    const float top = std::tan(xrView.fov.angleUp) * nearZ;
    const float bottom = std::tan(xrView.fov.angleDown) * nearZ;

    if (!(left < right) || !(bottom < top)) {
        return false;
    }

    C_MTXFrustum(view.projMtx, top, bottom, left, right, nearZ, farZ);
    view.fovy = (xrView.fov.angleUp - xrView.fov.angleDown) * 180.0f / kPi;
    view.aspect = (right - left) / (top - bottom);
    return true;
}

void build_eye_local_matrix(const AuroraXRView& xrView, Mtx out) noexcept {
    const AuroraXRPose pose = recentered_pose(xrView.pose);
    const AuroraXRQuaternionf orientation = normalized(pose.orientation);
    const Quaternion dolphinQuat{orientation.x, orientation.y, orientation.z, orientation.w};
    MTXQuat(out, &dolphinQuat);
    out[0][3] = pose.position.x * kMetersToGameUnits;
    out[1][3] = pose.position.y * kMetersToGameUnits;
    out[2][3] = pose.position.z * kMetersToGameUnits;
}

bool latest_valid_hmd_pose(AuroraXRPose& pose) noexcept {
    AuroraXRVector3f positionSum{};
    AuroraXRQuaternionf orientation{};
    uint32_t validPositionCount = 0;
    bool foundOrientation = false;

    for (const EyeData& eyeData : g_state.eyes) {
        if (!eyeData.poseValid) {
            continue;
        }

        if (eyeData.rawView.positionValid) {
            positionSum.x += eyeData.rawView.pose.position.x;
            positionSum.y += eyeData.rawView.pose.position.y;
            positionSum.z += eyeData.rawView.pose.position.z;
            ++validPositionCount;
        }

        if (!foundOrientation && eyeData.rawView.orientationValid) {
            orientation = eyeData.rawView.pose.orientation;
            foundOrientation = true;
        }
    }

    if (validPositionCount == 0 || !foundOrientation) {
        return false;
    }

    const float invCount = 1.0f / static_cast<float>(validPositionCount);
    pose.position = {
        positionSum.x * invCount,
        positionSum.y * invCount,
        positionSum.z * invCount,
    };
    pose.orientation = normalized(orientation);
    return true;
}

AuroraXRQuaternionf yaw_only(AuroraXRQuaternionf orientation) noexcept {
    const AuroraXRVector3f forward = rotate(orientation, {0.0f, 0.0f, -1.0f});
    return yaw_quaternion(std::atan2(forward.x, -forward.z));
}

void update_lookat_from_inv_view(view_class& view) noexcept {
    const Vec origin{0.0f, 0.0f, 0.0f};
    const Vec forward{0.0f, 0.0f, -1.0f};
    const Vec up{0.0f, 1.0f, 0.0f};
    MTXMultVec(view.invViewMtx, &origin, &view.lookat.eye);
    MTXMultVec(view.invViewMtx, &forward, &view.lookat.center);
    MTXMultVecSR(view.invViewMtx, &up, &view.lookat.up);
    view.bank = 0;
}

WorldProjectedItem make_world_projected_item(dDlst_base_c* drawList, const Matrix4x4& modelFromWorld) noexcept {
    return WorldProjectedItem{
        .drawList = drawList,
        .modelFromWorld = modelFromWorld,
    };
}

}  // namespace

void begin_frame() noexcept {
    clear_world_projected_queue();

    g_state.frameState = aurora_xr_get_frame_state();
    g_state.frameState.requested = g_state.frameState.requested || aurora_xr_is_requested();
    g_state.frameState.active = g_state.frameState.active || aurora_xr_is_active();
    g_state.frameState.shouldRender = g_state.frameState.shouldRender && aurora_xr_should_render();

    const uint32_t viewCount = aurora_xr_get_view_count();
    g_state.eyes.clear();
    g_state.eyes.resize(viewCount);
    g_state.frameState.viewCount = viewCount;

    for (uint32_t i = 0; i < viewCount; ++i) {
        EyeData& eyeData = g_state.eyes[i];
        reset_eye_placeholders(eyeData);

        AuroraXRView rawView{};
        if (!aurora_xr_get_view(i, &rawView)) {
            continue;
        }

        eyeData.rawView = rawView;
        eyeData.poseValid = rawView.orientationValid || rawView.positionValid;
        eyeData.fovValid = rawView.fovValid;
    }
}

bool requested() noexcept { return g_state.frameState.requested; }

bool active() noexcept { return g_state.frameState.active; }

bool should_render() noexcept { return g_state.frameState.shouldRender; }

AuroraXRStatus status() noexcept { return g_state.frameState.status; }

const AuroraXRFrameState& frame_state() noexcept { return g_state.frameState; }

uint32_t eye_count() noexcept { return static_cast<uint32_t>(g_state.eyes.size()); }

const EyeData* eye(uint32_t index) noexcept {
    if (index >= g_state.eyes.size()) {
        return nullptr;
    }
    return &g_state.eyes[index];
}

const std::vector<EyeData>& eyes() noexcept { return g_state.eyes; }

bool begin_eye_view(view_class& view, uint32_t eyeIndex, EyeViewToken& token) noexcept {
    save_view(token, view, eyeIndex);

    if (!active() || eyeIndex >= g_state.eyes.size()) {
        return false;
    }

    const EyeData& sourceEye = g_state.eyes[eyeIndex];
    if (!sourceEye.fovValid) {
        return false;
    }

    Mtx eyeLocal;
    build_eye_local_matrix(sourceEye.rawView, eyeLocal);

    Mtx composedInvView;
    MTXConcat(token.invViewMtx, eyeLocal, composedInvView);

    Mtx composedView;
    if (!MTXInverse(composedInvView, composedView)) {
        return false;
    }

    MTXCopy(composedInvView, view.invViewMtx);
    MTXCopy(composedView, view.viewMtx);
    if (!build_eye_projection(view, sourceEye.rawView)) {
        restore_view(token);
        return false;
    }

    update_lookat_from_inv_view(view);
    MTXCopy(view.viewMtx, view.viewMtxNoTrans);
    view.viewMtxNoTrans[0][3] = 0.0f;
    view.viewMtxNoTrans[1][3] = 0.0f;
    view.viewMtxNoTrans[2][3] = 0.0f;
    cMtx_concatProjView(view.projMtx, view.viewMtx, view.projViewMtx);

    EyeData& mutableEye = g_state.eyes[eyeIndex];
    mutableEye.viewFromWorld = matrix4_from_mtx(view.viewMtx);
    mutableEye.projectionFromView = matrix4_from_mtx44(view.projMtx);
    mutableEye.clipFromWorld = matrix4_from_mtx44(view.projViewMtx);

    token.active = true;
    return true;
}

void end_eye_view(EyeViewToken& token) noexcept {
    if (token.active) {
        restore_view(token);
    }
    token.active = false;
    token.view = nullptr;
}

void request_recenter() noexcept { g_state.recenterPending = true; }

bool recenter_pending() noexcept { return g_state.recenterPending; }

void clear_recenter_request() noexcept { g_state.recenterPending = false; }

RecenterResult recenter_from_latest_hmd_pose() noexcept {
    clear_recenter_request();

    if (!active()) {
        return RecenterResult::Inactive;
    }

    AuroraXRPose latestPose{};
    if (!latest_valid_hmd_pose(latestPose)) {
        return RecenterResult::NoTracking;
    }

    g_state.recenterOffset = {
        .pose = {
            .orientation = yaw_only(latestPose.orientation),
            .position = {latestPose.position.x, 0.0f, latestPose.position.z},
        },
        .valid = true,
    };
    return RecenterResult::Success;
}

const RecenterOffset& recenter_offset() noexcept { return g_state.recenterOffset; }

void set_recenter_offset(const RecenterOffset& offset) noexcept { g_state.recenterOffset = offset; }

void queue_world_projected_2d(dDlst_base_c* drawList) noexcept {
    queue_world_projected_2d(drawList, identity_matrix());
}

void queue_world_projected_2d(dDlst_base_c* drawList, const Matrix4x4& modelFromWorld) noexcept {
    if (drawList == nullptr) {
        return;
    }
    g_state.worldProjectedQueue.emplace_back(make_world_projected_item(drawList, modelFromWorld));
}

const std::vector<WorldProjectedItem>& world_projected_queue() noexcept {
    return g_state.worldProjectedQueue;
}

void clear_world_projected_queue() noexcept { g_state.worldProjectedQueue.clear(); }

}  // namespace dusk::vr
