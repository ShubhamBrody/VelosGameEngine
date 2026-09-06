#pragma once

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>

namespace velos {

struct Camera {
    DirectX::XMFLOAT3 target{0, 1, 0};
    float yaw = 0.65f;
    float pitch = 0.42f;
    float distance = 12.0f;
    float aspect = 1.6f;
    bool orthographic = false;

    [[nodiscard]] DirectX::XMVECTOR eye() const {
        const auto offset = DirectX::XMVectorSet(std::sin(yaw) * std::cos(pitch), std::sin(pitch),
            std::cos(yaw) * std::cos(pitch), 0);
        return DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&target), DirectX::XMVectorScale(offset, distance));
    }
    [[nodiscard]] DirectX::XMMATRIX view() const {
        return DirectX::XMMatrixLookAtRH(eye(), DirectX::XMLoadFloat3(&target), DirectX::XMVectorSet(0, 1, 0, 0));
    }
    [[nodiscard]] DirectX::XMMATRIX projection() const {
        return orthographic
            ? DirectX::XMMatrixOrthographicRH(distance * aspect, distance, 0.05f, 500.0f)
            : DirectX::XMMatrixPerspectiveFovRH(DirectX::XMConvertToRadians(50), aspect, 0.05f, 500.0f);
    }
    void orbit(float horizontal, float vertical) {
        if (orthographic) { return; }
        yaw -= horizontal * 0.006f;
        pitch = std::clamp(pitch + vertical * 0.006f, -1.48f, 1.48f);
    }
    void zoom(float wheel) { distance = std::clamp(distance * std::exp(-wheel * 0.12f), 0.3f, 180.0f); }
    void pan(float horizontal, float vertical) {
        const auto inverse = DirectX::XMMatrixInverse(nullptr, view());
        const float speed = distance * 0.0015f;
        const auto movement = DirectX::XMVectorAdd(DirectX::XMVectorScale(inverse.r[0], -horizontal * speed),
            DirectX::XMVectorScale(inverse.r[1], vertical * speed));
        DirectX::XMStoreFloat3(&target, DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&target), movement));
    }
    void set2D(bool enabled) {
        orthographic = enabled;
        if (enabled) { yaw = 0; pitch = 0; target = {0, 0, 0}; distance = 12; }
        else { yaw = 0.65f; pitch = 0.42f; target = {0, 1, 0}; distance = 12; }
    }
};

}