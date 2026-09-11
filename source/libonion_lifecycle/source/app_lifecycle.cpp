/* Copyright (C) 2026 OnionHEN / LightningMods */

#include <onion/app_lifecycle.hpp>

#include <algorithm>

namespace onion::lifecycle {

void RuntimeStartupGate::reset() noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  status_ = RuntimeStartupStatus::Pending;
}

void RuntimeStartupGate::signal(RuntimeStartupStatus status) noexcept {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (status_ == RuntimeStartupStatus::Pending)
      status_ = status;
  }
  ready_.notify_all();
}

RuntimeStartupStatus RuntimeStartupGate::wait_for(
    std::chrono::milliseconds timeout) noexcept {
  std::unique_lock<std::mutex> lock(mutex_);
  if (!ready_.wait_for(lock, timeout, [this] {
    return status_ != RuntimeStartupStatus::Pending;
  }) && status_ == RuntimeStartupStatus::Pending) {
    status_ = RuntimeStartupStatus::Timeout;
  }
  return status_;
}

RuntimeStartupStatus RuntimeStartupGate::status() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return status_;
}

Dispatcher::Dispatcher(size_t max_pending) noexcept
    : max_pending_(max_pending == 0 ? 1 : max_pending) {}

Dispatcher::~Dispatcher() { stop(); }

bool Dispatcher::subscribe(ISubscriber &subscriber) noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  if (started_ || std::find(subscribers_.begin(), subscribers_.end(),
                            &subscriber) != subscribers_.end())
    return false;
  subscribers_.push_back(&subscriber);
  std::stable_sort(subscribers_.begin(), subscribers_.end(),
                   [](const ISubscriber *left, const ISubscriber *right) {
                     return left->priority() < right->priority();
                   });
  return true;
}

bool Dispatcher::start() noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  if (started_)
    return true;
  stopping_ = false;
  if (pthread_create(&worker_, nullptr, worker_entry, this) != 0)
    return false;
  started_ = true;
  return true;
}

void Dispatcher::stop() noexcept {
  pthread_t worker {};
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!started_)
      return;
    stopping_ = true;
    pending_.clear();
    worker = worker_;
    ready_.notify_one();
  }
  (void)pthread_join(worker, nullptr);
  std::lock_guard<std::mutex> lock(mutex_);
  started_ = false;
}

bool Dispatcher::publish(Event event) noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!started_ || stopping_ || event.app.pid <= 1 || event.app.title_id.empty() ||
      pending_.size() >= max_pending_)
    return false;
  event.sequence = next_sequence_++;
  pending_.push_back(std::move(event));
  ready_.notify_one();
  return true;
}

void *Dispatcher::worker_entry(void *context) noexcept {
  static_cast<Dispatcher *>(context)->run();
  return nullptr;
}

void Dispatcher::run() noexcept {
  for (;;) {
    Event event;
    std::vector<ISubscriber *> subscribers;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      ready_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
      if (stopping_)
        return;
      event = std::move(pending_.front());
      pending_.pop_front();
      subscribers = subscribers_;
    }
    for (ISubscriber *subscriber : subscribers) {
      if (!subscriber->on_event(event))
        break;
    }
  }
}

} // namespace onion::lifecycle
