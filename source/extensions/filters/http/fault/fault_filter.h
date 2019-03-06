#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "envoy/api/v2/route/route.pb.h"
#include "envoy/config/filter/http/fault/v2/fault.pb.h"
#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/token_bucket_impl.h"
#include "common/http/header_utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Fault {

/**
 * All stats for the fault filter. @see stats_macros.h
 */
// clang-format off
#define ALL_FAULT_FILTER_STATS(COUNTER, GAUGE)                                                     \
  COUNTER(delays_injected)                                                                         \
  COUNTER(aborts_injected)                                                                         \
  COUNTER(response_rl_injected)                                                                    \
  COUNTER(faults_overflow)                                                                         \
  GAUGE  (active_faults)
// clang-format on

/**
 * Wrapper struct for connection manager stats. @see stats_macros.h
 */
struct FaultFilterStats {
  ALL_FAULT_FILTER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Configuration for fault injection.
 */
class FaultSettings : public Router::RouteSpecificFilterConfig {
public:
  struct RateLimit {
    uint64_t fixed_rate_kbps_;
    envoy::type::FractionalPercent percentage_;
  };

  FaultSettings(const envoy::config::filter::http::fault::v2::HTTPFault& fault);

  const std::vector<Http::HeaderUtility::HeaderData>& filterHeaders() const {
    return fault_filter_headers_;
  }
  envoy::type::FractionalPercent abortPercentage() const { return abort_percentage_; }
  envoy::type::FractionalPercent delayPercentage() const { return fixed_delay_percentage_; }
  uint64_t delayDuration() const { return fixed_duration_ms_; }
  uint64_t abortCode() const { return http_status_; }
  const std::string& upstreamCluster() const { return upstream_cluster_; }
  const std::unordered_set<std::string>& downstreamNodes() const { return downstream_nodes_; }
  absl::optional<uint64_t> maxActiveFaults() const { return max_active_faults_; }
  const absl::optional<RateLimit>& responseRateLimit() const { return response_rate_limit_; }

private:
  envoy::type::FractionalPercent abort_percentage_;
  uint64_t http_status_{}; // HTTP or gRPC return codes
  envoy::type::FractionalPercent fixed_delay_percentage_;
  uint64_t fixed_duration_ms_{}; // in milliseconds
  std::string upstream_cluster_; // restrict faults to specific upstream cluster
  std::vector<Http::HeaderUtility::HeaderData> fault_filter_headers_;
  std::unordered_set<std::string> downstream_nodes_{}; // Inject failures for specific downstream
  absl::optional<uint64_t> max_active_faults_;
  absl::optional<RateLimit> response_rate_limit_;
};

/**
 * Configuration for the fault filter.
 */
class FaultFilterConfig {
public:
  FaultFilterConfig(const envoy::config::filter::http::fault::v2::HTTPFault& fault,
                    Runtime::Loader& runtime, const std::string& stats_prefix, Stats::Scope& scope,
                    TimeSource& time_source);

  Runtime::Loader& runtime() { return runtime_; }
  FaultFilterStats& stats() { return stats_; }
  const std::string& statsPrefix() { return stats_prefix_; }
  Stats::Scope& scope() { return scope_; }
  const FaultSettings* settings() { return &settings_; }
  TimeSource& timeSource() { return time_source_; }

private:
  static FaultFilterStats generateStats(const std::string& prefix, Stats::Scope& scope);

  const FaultSettings settings_;
  Runtime::Loader& runtime_;
  FaultFilterStats stats_;
  const std::string stats_prefix_;
  Stats::Scope& scope_;
  TimeSource& time_source_;
};

typedef std::shared_ptr<FaultFilterConfig> FaultFilterConfigSharedPtr;

/**
 * fixfix
 */
class StreamRateLimiter : Logger::Loggable<Logger::Id::filter> {
public:
  /**
   * fixfix
   */
  StreamRateLimiter(uint64_t max_kbps, uint64_t max_buffered_data,
                    std::function<void()> pause_data_cb, std::function<void()> resume_data_cb,
                    std::function<void()> write_data_cb, TimeSource& time_source,
                    Event::Dispatcher& dispatcher);

  /**
   * fixfix
   */
  void writeData(Buffer::Instance& incoming_buffer);

private:
  void onTokenTimer();

  const uint64_t bytes_per_time_slice_;
  const uint64_t max_buffered_data_;
  const std::function<void()> pause_data_cb_;
  const std::function<void()> resume_data_cb_;
  const std::function<void()> write_data_cb_;
  TokenBucketImpl token_bucket_;
  Event::TimerPtr token_timer_;
  bool waiting_for_token_{};
  Buffer::OwnedImpl buffer_;
};

/**
 * A filter that is capable of faulting an entire request before dispatching it upstream.
 */
class FaultFilter : public Http::StreamFilter {
public:
  FaultFilter(FaultFilterConfigSharedPtr config);
  ~FaultFilter();

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::HeaderMap&) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap&, bool) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }

private:
  class RuntimeKeyValues {
  public:
    const std::string DelayPercentKey = "fault.http.delay.fixed_delay_percent";
    const std::string AbortPercentKey = "fault.http.abort.abort_percent";
    const std::string DelayDurationKey = "fault.http.delay.fixed_duration_ms";
    const std::string AbortHttpStatusKey = "fault.http.abort.http_status";
    const std::string MaxActiveFaultsKey = "fault.http.max_active_faults";
    const std::string ResponseRateLimitKey = "fault.http.rate_limit.response";
  };

  using RuntimeKeys = ConstSingleton<RuntimeKeyValues>;

  bool faultOverflow();
  void recordAbortsInjectedStats();
  void recordDelaysInjectedStats();
  void resetTimerState();
  void postDelayInjection();
  void abortWithHTTPStatus();
  bool matchesTargetUpstreamCluster();
  bool matchesDownstreamNodes(const Http::HeaderMap& headers);
  bool isAbortEnabled();
  bool isDelayEnabled();
  absl::optional<uint64_t> delayDuration();
  uint64_t abortHttpStatus();
  void incActiveFaults();
  void maybeSetupResponseRateLimit();

  FaultFilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  Event::TimerPtr delay_timer_;
  std::string downstream_cluster_{};
  const FaultSettings* fault_settings_;
  bool fault_active_{};
  std::unique_ptr<StreamRateLimiter> response_limiter_;
  std::string downstream_cluster_delay_percent_key_{};
  std::string downstream_cluster_abort_percent_key_{};
  std::string downstream_cluster_delay_duration_key_{};
  std::string downstream_cluster_abort_http_status_key_{};
};

} // namespace Fault
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
