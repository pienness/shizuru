#pragma once

#include "policy/types.h"

namespace shizuru::core {

// Abstract interface for audit log backends.
class AuditSink {
 public:
  virtual ~AuditSink() = default;

  // Write an audit record. Must be thread-safe.
  virtual void Write(const AuditRecord& record) = 0;

  // Flush any buffered records.
  virtual void Flush() = 0;
};

}  // namespace shizuru::core
