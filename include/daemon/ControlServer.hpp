#pragma once

#include "daemon/DeviceManager.hpp"
#include "daemon/TelemetrySyncClient.hpp"
#include <atomic>
#include <httplib.h>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ptd
{

    class ControlServer
    {
    public:
        ControlServer(DeviceManager &manager,
                      TelemetrySyncClient &sync_client,
                      std::string bind_host = "127.0.0.1",
                      int port = 8080);
        ~ControlServer();

        ControlServer(const ControlServer &) = delete;
        ControlServer &operator=(const ControlServer &) = delete;

        void startAsync();
        void stop();
        [[nodiscard]] int port() const noexcept { return port_; }
        [[nodiscard]] size_t ingestedBatchCount() const;

    private:
        void setupRoutes();
        [[nodiscard]] static std::string renderDashboardHtml();

        DeviceManager &manager_;
        TelemetrySyncClient &sync_client_;
        std::string bind_host_;
        int port_;

        httplib::Server server_;
        std::jthread server_thread_;
        std::atomic<bool> fault_mode_enabled_{false};

        mutable std::mutex ingest_mutex_;
        std::vector<nlohmann::json> ingested_batches_;
    };

}