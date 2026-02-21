#pragma once

#include <string>
#include <map>
#include <memory>
#include <curl/curl.h>

struct HttpResponse {
    long status_code = 0;
    std::string body;
    std::map<std::string, std::string> headers;
};

class RestClient {
public:
    explicit RestClient(const std::string& base_url);
    ~RestClient();

    // Non-copyable
    RestClient(const RestClient&) = delete;
    RestClient& operator=(const RestClient&) = delete;

    // Movable
    RestClient(RestClient&& other) noexcept;
    RestClient& operator=(RestClient&& other) noexcept;

    // ── HTTP methods ────────────────────────────────────────────────────
    HttpResponse get(const std::string& path,
                     const std::map<std::string, std::string>& params = {},
                     const std::map<std::string, std::string>& headers = {});

    HttpResponse post(const std::string& path,
                      const std::string& body = "",
                      const std::map<std::string, std::string>& headers = {});

    HttpResponse del(const std::string& path,
                     const std::map<std::string, std::string>& headers = {});

    // ── Configuration ───────────────────────────────────────────────────
    void set_timeout_ms(long timeout_ms);
    void set_retry_count(int retries);

    const std::string& base_url() const { return base_url_; }

private:
    HttpResponse perform(const std::string& method,
                         const std::string& url,
                         const std::string& body,
                         const std::map<std::string, std::string>& headers);

    std::string build_query_string(const std::map<std::string, std::string>& params) const;
    std::string url_encode(const std::string& value) const;
    bool is_retryable_status(long status) const;

    static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);
    static size_t header_callback(char* ptr, size_t size, size_t nmemb, void* userdata);

    std::string base_url_;
    CURL* curl_ = nullptr;
    long timeout_ms_ = 10000;
    int retry_count_ = 3;
};
