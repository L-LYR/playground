#pragma once

namespace driver {

class Status {
 private:
  enum class Code {
    kOk = 0,
    kInvalidArgs,
    kDeviceFailure,
    kIoQueueFull,
    kIoFailure,
  };

 public:
  virtual ~Status() = default;

  // copy ctor
  Status(const Status& rhs) = default;
  Status& operator=(const Status& rhs) = default;

  // move ctor
  Status(Status&& rhs) = default;
  Status& operator=(Status&& rhs) = default;

 public:
  static auto OK() -> Status { return Status(Code::kOk); }
  static auto InvalidArgs() -> Status { return Status(Code::kInvalidArgs); }
  static auto DeviceFailure() -> Status { return Status(Code::kDeviceFailure); }
  static auto IoQueueFull() -> Status { return Status(Code::kIoQueueFull); }
  static auto IoFailure() -> Status { return Status(Code::kIoFailure); }

  auto IsOK() const -> bool { return status_code_ == Code::kOk; }
  auto IsInvalidArgs() const -> bool {
    return status_code_ == Code::kInvalidArgs;
  }
  auto IsDeviceFailure() const -> bool {
    return status_code_ == Code::kDeviceFailure;
  }
  auto IsIoQueueFull() const -> bool {
    return status_code_ == Code::kIoQueueFull;
  }
  auto IsIoFailure() const -> bool { return status_code_ == Code::kIoFailure; }

 private:
  // no public ctor
  explicit Status(const Code& status_code) : status_code_(status_code) {}

  Code status_code_;
};

}  // namespace driver