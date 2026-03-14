#pragma once

#include <mutex>
#include <vector>

#include "interfaces/audit_sink.h"

namespace shizuru::core::testing {

// Hand-written mock for AuditSink.
// Captures all AuditRecords written for test verification.
class MockAuditSink : public AuditSink {
 public:
  // All records written via Write().
  std::vector<AuditRecord> records;

  // Number of Flush() calls received.
  int flush_count = 0;

  void Write(const AuditRecord& record) override {
    std::lock_guard<std::mutex> lock(mu_);
    records.push_back(record);
  }

  void Flush() override {
    std::lock_guard<std::mutex> lock(mu_);
    ++flush_count;
  }

 private:
  std::mutex mu_;
};

}  // namespace shizuru::core::testing
