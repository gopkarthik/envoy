#include "source/common/http/filter_chain_helper.h"

#include <memory>
#include <string>

#include "envoy/extensions/filters/http/upstream_codec/v3/upstream_codec.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/common/empty_string.h"
#include "source/common/common/fmt.h"
#include "source/common/config/utility.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Http {

void FilterChainUtility::createFilterChainForFactories(
    Http::FilterChainManager& manager, const FilterChainOptions& options,
    const FilterFactoriesList& filter_factories) {
  bool added_missing_config_filter = false;
  for (const auto& filter_config_provider : filter_factories) {
    // If this filter is disabled explicitly, skip trying to create it.
    if (options.filterDisabled(filter_config_provider.provider->name())
            .value_or(filter_config_provider.disabled)) {
      continue;
    }

    auto config = filter_config_provider.provider->config();
    if (config.has_value()) {
      manager.applyFilterFactoryCb({filter_config_provider.provider->name()}, config.ref());
      continue;
    }

    // If a filter config is missing after warming, inject a local reply with status 500.
    if (!added_missing_config_filter) {
      ENVOY_LOG(trace, "Missing filter config for a provider {}",
                filter_config_provider.provider->name());
      manager.applyFilterFactoryCb({}, MissingConfigFilterFactory);
      added_missing_config_filter = true;
    } else {
      ENVOY_LOG(trace, "Provider {} missing a filter config",
                filter_config_provider.provider->name());
    }
  }
}

SINGLETON_MANAGER_REGISTRATION(downstream_filter_config_provider_manager);
SINGLETON_MANAGER_REGISTRATION(upstream_filter_config_provider_manager);

std::shared_ptr<UpstreamFilterConfigProviderManager>
FilterChainUtility::createSingletonUpstreamFilterConfigProviderManager(
    Server::Configuration::ServerFactoryContext& context) {
  std::shared_ptr<UpstreamFilterConfigProviderManager> upstream_filter_config_provider_manager =
      context.singletonManager().getTyped<Http::UpstreamFilterConfigProviderManager>(
          SINGLETON_MANAGER_REGISTERED_NAME(upstream_filter_config_provider_manager),
          [] { return std::make_shared<Filter::UpstreamHttpFilterConfigProviderManagerImpl>(); });
  return upstream_filter_config_provider_manager;
}

std::shared_ptr<Http::DownstreamFilterConfigProviderManager>
FilterChainUtility::createSingletonDownstreamFilterConfigProviderManager(
    Server::Configuration::ServerFactoryContext& context) {
  std::shared_ptr<Http::DownstreamFilterConfigProviderManager>
      downstream_filter_config_provider_manager =
          context.singletonManager().getTyped<Http::DownstreamFilterConfigProviderManager>(
              SINGLETON_MANAGER_REGISTERED_NAME(downstream_filter_config_provider_manager),
              [] { return std::make_shared<Filter::HttpFilterConfigProviderManagerImpl>(); }, true);
  return downstream_filter_config_provider_manager;
}

absl::Status FilterChainUtility::checkUpstreamHttpFiltersList(const FiltersList& filters) {
  static const std::string upstream_codec_type_url(
      envoy::extensions::filters::http::upstream_codec::v3::UpstreamCodec::default_instance()
          .GetTypeName());

  if (!filters.empty()) {
    const auto last_type_url = Config::Utility::getFactoryType(filters.rbegin()->typed_config());
    if (last_type_url != upstream_codec_type_url) {
      return absl::InvalidArgumentError(
          fmt::format("The codec filter is the only valid terminal upstream HTTP filter, use '{}'",
                      upstream_codec_type_url));
    }
  }

  return absl::OkStatus();
}

} // namespace Http
} // namespace Envoy
