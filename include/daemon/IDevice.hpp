#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace ptd
{

    // RAII wrapper simulating deterministic acquisition and release of an OS HID/USB handle
    class HardwareHandleGuard
    {
    public:
        explicit HardwareHandleGuard(std::string device_path)
            : device_path_(std::move(device_path)), is_open_(true) {}

        ~HardwareHandleGuard() noexcept
        {
            release();
        }

        HardwareHandleGuard(const HardwareHandleGuard &) = delete;
        HardwareHandleGuard &operator=(const HardwareHandleGuard &) = delete;

        HardwareHandleGuard(HardwareHandleGuard &&other) noexcept
            : device_path_(std::move(other.device_path_)), is_open_(other.is_open_)
        {
            other.is_open_ = false;
        }

        HardwareHandleGuard &operator=(HardwareHandleGuard &&other) noexcept
        {
            if (this != &other)
            {
                release();
                device_path_ = std::move(other.device_path_);
                is_open_ = other.is_open_;
                other.is_open_ = false;
            }
            return *this;
        }

        [[nodiscard]] bool isOpen() const noexcept { return is_open_; }
        [[nodiscard]] std::string_view path() const noexcept { return device_path_; }

    private:
        void release() noexcept
        {
            if (is_open_)
            {
                is_open_ = false;
            }
        }

        std::string device_path_;
        bool is_open_{false};
    };

    struct MouseTelemetry
    {
        uint32_t dpi{1600};
        uint32_t polling_rate_hz{1000};
        uint8_t battery_percent{100};
        double surface_lift_mm{1.2};
    };

    struct HeadsetTelemetry
    {
        std::string eq_preset{"Flat"};
        bool mic_muted{false};
        uint8_t battery_percent{100};
        double internal_temp_c{31.5};
    };

    using TelemetryPayload = std::variant<MouseTelemetry, HeadsetTelemetry>;

    struct ConfigResult
    {
        bool ok{false};
        std::string error_message;

        static ConfigResult success() { return {true, ""}; }
        static ConfigResult failure(std::string msg) { return {false, std::move(msg)}; }
    };

    class IDevice
    {
    public:
        virtual ~IDevice() = default;

        [[nodiscard]] virtual std::string id() const = 0;
        [[nodiscard]] virtual std::string name() const = 0;
        [[nodiscard]] virtual std::string type() const = 0;
        [[nodiscard]] virtual bool isConnected() const noexcept = 0;

        virtual void pollHardware() = 0;
        [[nodiscard]] virtual TelemetryPayload readTelemetry() const = 0;
        [[nodiscard]] virtual ConfigResult applyConfig(const nlohmann::json &config_patch) = 0;
        [[nodiscard]] virtual nlohmann::json toJson() const = 0;
    };

}