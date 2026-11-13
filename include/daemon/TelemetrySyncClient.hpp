#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

namespace ptd
{

    struct SyncMetrics
    {
        uint64_t successful_pushes{0};
        uint64_t failed_pushes{0};
        uint64_t retry_attempts{0};
    };

    class TelemetrySyncClient
    {
    public:
        TelemetrySyncClient(std::string host,
                            int port,
                            std::string endpoint_path = "/api/v1/telemetry/ingest",
                            int max_retries = 3,
                            std::chrono::milliseconds initial_backoff = std::chrono::milliseconds(50));

        [[nodiscard]] bool pushTelemetry(const nlohmann::json &payload);
        [[nodiscard]] SyncMetrics metrics() const noexcept;

    private:
        std::string host_;
        int port_;
        std::string endpoint_path_;
        int max_retries_;
        std::chrono::milliseconds initial_backoff_;

        std::atomic<uint64_t> successful_pushes_{0};
        std::atomic<uint64_t> failed_pushes_{0};
        std::atomic<uint64_t> retry_attempts_{0};
    };

}