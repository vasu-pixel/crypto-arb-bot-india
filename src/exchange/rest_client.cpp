#include "exchange/rest_client.h"
#include "common/logger.h"

#include <sstream>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <algorithm>
#include <cstring>

// ─── Static callbacks ───────────────────────────────────────────────────────

size_t RestClient::write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* response_body = static_cast<std::string*>(userdata);
    size_t total = size * nmemb;
    response_body->append(ptr, total);
    return total;
}

size_t RestClient::header_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* headers = static_cast<std::map<std::string, std::string>*>(userdata);
    size_t total = size * nmemb;
    std::string line(ptr, total);

    // Find colon separator
    auto colon_pos = line.find(':');
    if (colon_pos != std::string::npos) {
        std::string key = line.substr(0, colon_pos);
        std::string value = line.substr(colon_pos + 1);

        // Trim whitespace
        auto ltrim = [](std::string& s) {
            s.erase(s.begin(), std::find_if(s.begin(), s.end(),
                    [](unsigned char c) { return !std::isspace(c); }));
        };
        auto rtrim = [](std::string& s) {
            s.erase(std::find_if(s.rbegin(), s.rend(),
                    [](unsigned char c) { return !std::isspace(c); }).base(), s.end());
        };
        ltrim(key); rtrim(key);
        ltrim(value); rtrim(value);

        // Lowercase the key for consistent lookup
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        (*headers)[key] = value;
    }
    return total;
}

// ─── Constructor / Destructor ───────────────────────────────────────────────

RestClient::RestClient(const std::string& base_url) : base_url_(base_url) {
    curl_ = curl_easy_init();
    if (!curl_) {
        throw std::runtime_error("RestClient: failed to initialise curl handle");
    }
    LOG_DEBUG("RestClient created for base_url={}", base_url_);
}

RestClient::~RestClient() {
    if (curl_) {
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
    }
}

RestClient::RestClient(RestClient&& other) noexcept
    : base_url_(std::move(other.base_url_)),
      curl_(other.curl_),
      timeout_ms_(other.timeout_ms_),
      retry_count_(other.retry_count_) {
    other.curl_ = nullptr;
}

RestClient& RestClient::operator=(RestClient&& other) noexcept {
    if (this != &other) {
        if (curl_) {
            curl_easy_cleanup(curl_);
        }
        base_url_ = std::move(other.base_url_);
        curl_ = other.curl_;
        timeout_ms_ = other.timeout_ms_;
        retry_count_ = other.retry_count_;
        other.curl_ = nullptr;
    }
    return *this;
}

// ─── Configuration ──────────────────────────────────────────────────────────

void RestClient::set_timeout_ms(long timeout_ms) { timeout_ms_ = timeout_ms; }
void RestClient::set_retry_count(int retries) { retry_count_ = retries; }

// ─── URL helpers ────────────────────────────────────────────────────────────

std::string RestClient::url_encode(const std::string& value) const {
    char* encoded = curl_easy_escape(curl_, value.c_str(), static_cast<int>(value.size()));
    if (!encoded) {
        return value;
    }
    std::string result(encoded);
    curl_free(encoded);
    return result;
}

std::string RestClient::build_query_string(const std::map<std::string, std::string>& params) const {
    if (params.empty()) return "";
    std::ostringstream oss;
    bool first = true;
    for (const auto& [key, value] : params) {
        if (!first) oss << '&';
        oss << url_encode(key) << '=' << url_encode(value);
        first = false;
    }
    return oss.str();
}

bool RestClient::is_retryable_status(long status) const {
    return status == 429 || status == 500 || status == 502 ||
           status == 503 || status == 504;
}

// ─── HTTP verbs ─────────────────────────────────────────────────────────────

HttpResponse RestClient::get(const std::string& path,
                             const std::map<std::string, std::string>& params,
                             const std::map<std::string, std::string>& headers) {
    std::string url = base_url_ + path;
    std::string qs = build_query_string(params);
    if (!qs.empty()) {
        url += '?' + qs;
    }
    return perform("GET", url, "", headers);
}

HttpResponse RestClient::post(const std::string& path,
                              const std::string& body,
                              const std::map<std::string, std::string>& headers) {
    std::string url = base_url_ + path;
    return perform("POST", url, body, headers);
}

HttpResponse RestClient::del(const std::string& path,
                             const std::map<std::string, std::string>& headers) {
    std::string url = base_url_ + path;
    return perform("DELETE", url, "", headers);
}

// ─── Core perform with retry ────────────────────────────────────────────────

HttpResponse RestClient::perform(const std::string& method,
                                 const std::string& url,
                                 const std::string& body,
                                 const std::map<std::string, std::string>& headers) {
    HttpResponse response;
    int attempts = retry_count_ + 1;

    for (int attempt = 0; attempt < attempts; ++attempt) {
        // Reset response for this attempt
        response.body.clear();
        response.headers.clear();
        response.status_code = 0;

        curl_easy_reset(curl_);

        // URL
        curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());

        // Timeout
        curl_easy_setopt(curl_, CURLOPT_TIMEOUT_MS, timeout_ms_);
        curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT_MS, timeout_ms_ / 2);

        // Connection reuse
        curl_easy_setopt(curl_, CURLOPT_TCP_KEEPALIVE, 1L);

        // Write callback
        curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response.body);

        // Header callback
        curl_easy_setopt(curl_, CURLOPT_HEADERFUNCTION, header_callback);
        curl_easy_setopt(curl_, CURLOPT_HEADERDATA, &response.headers);

        // Method
        if (method == "POST") {
            curl_easy_setopt(curl_, CURLOPT_POST, 1L);
            curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        } else if (method == "DELETE") {
            curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, "DELETE");
        }
        // GET is the default

        // Custom headers
        struct curl_slist* header_list = nullptr;
        for (const auto& [key, value] : headers) {
            std::string h = key + ": " + value;
            header_list = curl_slist_append(header_list, h.c_str());
        }
        if (method == "POST" && headers.find("Content-Type") == headers.end()) {
            header_list = curl_slist_append(header_list, "Content-Type: application/json");
        }
        if (header_list) {
            curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, header_list);
        }

        // Perform
        CURLcode res = curl_easy_perform(curl_);

        // Clean up header list
        if (header_list) {
            curl_slist_free_all(header_list);
        }

        if (res != CURLE_OK) {
            LOG_ERROR("RestClient: curl error on {} {}: {} (attempt {}/{})",
                      method, url, curl_easy_strerror(res), attempt + 1, attempts);
            if (attempt < attempts - 1) {
                int delay_ms = static_cast<int>(500 * (1 << attempt)); // exponential backoff
                LOG_WARN("RestClient: retrying in {}ms", delay_ms);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                continue;
            }
            response.status_code = 0;
            response.body = std::string("curl_error: ") + curl_easy_strerror(res);
            return response;
        }

        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &response.status_code);

        // Retry on retryable HTTP status codes
        if (is_retryable_status(response.status_code) && attempt < attempts - 1) {
            int delay_ms = static_cast<int>(500 * (1 << attempt));
            LOG_WARN("RestClient: HTTP {} on {} {}, retrying in {}ms (attempt {}/{})",
                     response.status_code, method, url, delay_ms, attempt + 1, attempts);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            continue;
        }

        LOG_DEBUG("RestClient: {} {} -> {} ({} bytes)",
                  method, url, response.status_code, response.body.size());
        return response;
    }

    return response;
}
