#pragma once

#include "daemon/IDevice.hpp"
#include <mutex>

namespace ptd
{

    class HeadsetDevice final : public IDevice
    {
    public:
        HeadsetDevice(std::string id, std::string name, std::string hid_path);

        [[nodiscard]] std::string id() const override;
        [[nodiscard]] std::string name() const override;
        [[nodiscard]] std::string type() const override { return "headset"; }
        [[nodiscard]] bool isConnected() const noexcept override;

        void pollHardware() override;
        [[nodiscard]] TelemetryPayload readTelemetry() const override;
        [[nodiscard]] ConfigResult applyConfig(const nlohmann::json &config_patch) override;
        [[nodiscard]] nlohmann::json toJson() const override;

    private:
        std::string id_;
        std::string name_;
        HardwareHandleGuard handle_;
        mutable std::mutex mutex_;
        HeadsetTelemetry state_{};
        uint64_t poll_counter_{0};
    };

}