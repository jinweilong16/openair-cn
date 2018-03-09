/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <unordered_map>

#include <prometheus/registry.h>
#include <prometheus/family.h>
#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>

#include <protos/metrics.pb.h>
#include <protos/metricsd.pb.h>

using prometheus::Registry;
using prometheus::Family;

namespace magma { namespace service303 {

/**
 * MetricsRegistry is a dictionary for metrics instances. It ensures we
 * constuct a single instance of a metric family per name and a single
 * instance for each label set in that family.
 */
template <typename T, typename MetricFamilyFactory>
class MetricsRegistry {
  public:
    MetricsRegistry(const std::shared_ptr<prometheus::Registry>& registry,
                    const MetricFamilyFactory& factory);

    /**
     * Get or create a metric instance matching this name and label set
     *
     * @param name: the metric name
     * @param labels: list of tuples denoting label key value pairs
     * @param args...: other arguments the Metric constructor may need
     * @return prometheus T instance
     */
    template <typename... Args>
    T& Get(const std::string& name,
           const std::map<std::string, std::string>& labels,
           Args&&... args);

    const std::size_t SizeFamilies() {
      return families_.size();
    }

    const std::size_t SizeMetrics() {
      return metrics_.size();
    }

  private:
    static std::size_t hash_name_and_labels(const std::string& name,
        const std::map<std::string, std::string>& labels);
    // Convert labels to enums if applicable
    static void parse_labels(const std::map<std::string, std::string>& labels,
        std::map<std::string, std::string>& parsed_labels);
    std::unordered_map<std::size_t, Family<T>*> families_;
    std::unordered_map<std::size_t, T*> metrics_;
    const std::shared_ptr<prometheus::Registry>& registry_;
    const MetricFamilyFactory& factory_;
};

template <typename T, typename MetricFamilyFactory>
MetricsRegistry<T, MetricFamilyFactory>::MetricsRegistry(
  const std::shared_ptr<prometheus::Registry>& registry,
  const MetricFamilyFactory& factory)
  : registry_(registry), factory_(factory) {}

template <typename T, typename MetricFamilyFactory>
template <typename... Args>
T& MetricsRegistry<T, MetricFamilyFactory>::Get(
  const std::string& name,
  const std::map<std::string, std::string>& labels, Args&&... args) {

  // Create the family if we haven't seen it before
  Family<T>* family;
  size_t name_hash = std::hash<std::string>{}(name);
  auto family_it = families_.find(name_hash);
  if (family_it != families_.end()) {
    family = family_it->second;
  } else {
    // If the name is a defined MetricName, use the enum value instead
    MetricName name_value;
    const std::string& proto_name = MetricName_Parse(name, &name_value)
        ? std::to_string(name_value) : name;
    // Factory constructs the metric on the heap and adds it to registry_
    family = &factory_()
                        .Name(proto_name)
                        .Register(*registry_);
    families_.insert({{name_hash, family}});
  }

  // Create the metric if we haven't seen it before
  T* metric;
  size_t metric_hash = hash_name_and_labels(name, labels);
  auto metric_it = metrics_.find(metric_hash);
  if (metric_it != metrics_.end()) {
    metric = metric_it->second;
  } else {
    std::map<std::string, std::string> converted_labels;
    parse_labels(labels, converted_labels);
    metric = &family->Add(converted_labels, std::forward<Args>(args)...);
    metrics_.insert({{metric_hash, metric}});
  }
  return *metric;
}

template <typename T, typename MetricFamilyFactory>
std::size_t MetricsRegistry<T, MetricFamilyFactory>::hash_name_and_labels(
    const std::string& name,
    const std::map<std::string, std::string>& labels) {
  auto combined = std::accumulate(
      labels.begin(), labels.end(), std::string{name},
      [](const std::string& acc,
         const std::pair<std::string, std::string>& label_pair) {
        return acc + label_pair.first + label_pair.second;
      });
  return std::hash<std::string>{}(combined);
}

template <typename T, typename MetricFamilyFactory>
void MetricsRegistry<T, MetricFamilyFactory>::parse_labels(
    const std::map<std::string, std::string>& labels,
    std::map<std::string, std::string>& parsed_labels) {
  for (const auto& label_pair : labels) {
    // convert label name
    MetricLabelName label_name_enum;
    const std::string& label_name =
      MetricLabelName_Parse(label_pair.first, &label_name_enum)
      ? std::to_string(label_name_enum) : label_pair.first;
    // insert into new map
    parsed_labels.insert({{label_name, label_pair.second}});
  }
}

}} // namespace magma::service303