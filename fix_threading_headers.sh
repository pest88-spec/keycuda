#!/bin/bash

cd /root/keycuda

echo "Adding missing threading headers..."

# Fix asynchronous_stream_manager.h
sed -i '1a\
#include <thread>\
#include <condition_variable>\
#include <atomic>' src/ComputeCore/gpu/performance/asynchronous_stream_manager.h

# Fix double_buffer_manager.h
sed -i '1a\
#include <thread>\
#include <condition_variable>\
#include <atomic>' src/ComputeCore/gpu/performance/double_buffer_manager.h

# Fix memory_bandwidth_profiler.h
sed -i '1a\
#include <thread>\
#include <atomic>' src/ComputeCore/gpu/performance/memory_bandwidth_profiler.h

# Fix memory_pool_manager.h
sed -i '1a\
#include <thread>\
#include <atomic>' src/ComputeCore/gpu/performance/memory_pool_manager.h

# Fix bandwidth_validator.h - remove default parameter from reference
sed -i 's/virtual bool RunBandwidthTest(size_t test_size_mb = 1024, double& achieved_utilization);/virtual bool RunBandwidthTest(size_t test_size_mb, double\& achieved_utilization);/' src/ComputeCore/gpu/performance/bandwidth_validator.h

# Fix operator_metadata_validator.h - change json/json.h to nlohmann/json.hpp
sed -i 's|#include <json/json.h>|#include <nlohmann/json.hpp>|' src/services/operator_metadata_validator.h
sed -i 's/json metadata;/nlohmann::json metadata;/g' src/services/operator_metadata_validator.h
sed -i 's/json operation_parameters;/nlohmann::json operation_parameters;/g' src/services/operator_metadata_validator.h
sed -i 's/json result_summary;/nlohmann::json result_summary;/g' src/services/operator_metadata_validator.h
sed -i 's/json additional_info;/nlohmann::json additional_info;/g' src/services/operator_metadata_validator.h

echo "Threading header fixes completed!"
