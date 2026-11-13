#include "daemon/TelemetrySyncClient.hpp"
#include <httplib.h>
#include <thread>

namespace ptd
{

    TelemetrySyncClient::TelemetrySyncClient(std::string host,
                                             int port,
                                             std::string endpoint_path,
                                             int max_retries,
                                             std::chrono::milliseconds initial_backoff)
        : host_(std::move(host)),
          port_(port),
          endpoint_path_(std::move(endpoint_path)),
          max_retries_(max_retries),
          initial_backoff_(initial_backoff) {}

    bool TelemetrySyncClient::pushTelemetry(const nlohmann::json &payload)
    {
        httplib::Client client(host_, port_);
        client.set_connection_timeout(1, 0);
        client.set_read_timeout(2, 0);

        const std::string body = payload.dump();
        auto backoff = initial_backoff_;

        for (int attempt = 0; attempt <= max_retries_; ++attempt)
        {
            if (attempt > 0)
            {
                retry_attempts_.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(backoff);
                backoff *= 2;
            }

            auto res = client.Post(endpoint_path_, body, "application/json");
            if (res && res->status >= 200 && res->status < 300)
            {
                successful_pushes_.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
        }

        failed_pushes_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    SyncMetrics TelemetrySyncClient::metrics() const noexcept
    {
        return SyncMetrics{
            successful_pushes_.load(std::memory_order_relaxed),
            failed_pushes_.load(std::memory_order_relaxed),
            retry_attempts_.load(std::memory_order_relaxed)};
    }

} // namespace ptd