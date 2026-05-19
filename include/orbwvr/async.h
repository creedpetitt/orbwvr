#pragma once

#include <condition_variable>
#include <coroutine>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace orbwvr {
template <typename T> class async {
  private:
    struct state {
        bool ready = false;
        std::optional<T> value;
        std::exception_ptr err;
        std::coroutine_handle<> continuation{};
    };
    struct awaiter {
        std::shared_ptr<state> shared_state_;
        bool await_ready() const noexcept { return shared_state_->ready; }
        void await_suspend(std::coroutine_handle<> handle) noexcept {
            shared_state_->continuation = handle;
        }
        T await_resume() {
            if (shared_state_->err) {
                std::rethrow_exception(shared_state_->err);
            }
            return std::move(*shared_state_->value);
        }
    };
    std::shared_ptr<state> shared_state_;
    explicit async(std::shared_ptr<state> state)
        : shared_state_(std::move(state)) {}

  public:
    struct producer {
      private:
        std::shared_ptr<state> shared_state_;

      public:
        explicit producer(std::shared_ptr<state> state)
            : shared_state_(std::move(state)) {}
        void set_value(T value) {
            shared_state_->value = std::move(value);
            shared_state_->ready = true;
            if (shared_state_->continuation) {
                shared_state_->continuation.resume();
            }
        }
        void set_error(std::exception_ptr err) {
            shared_state_->err = std::move(err);
            shared_state_->ready = true;
            if (shared_state_->continuation) {
                shared_state_->continuation.resume();
            }
        }
    };
    struct promise_type {
      private:
        std::shared_ptr<state> shared_state_ = std::make_shared<state>();
        struct final_awaiter {
            bool await_ready() const noexcept { return false; }
            void await_suspend(
                std::coroutine_handle<promise_type> handle) const noexcept {
                auto &shared_state = handle.promise().shared_state_;
                if (shared_state->continuation) {
                    shared_state->continuation.resume();
                }
            }
            void await_resume() const noexcept {}
        };

      public:
        async get_return_object() { return async(shared_state_); }
        std::suspend_never initial_suspend() const noexcept { return {}; }
        final_awaiter final_suspend() const noexcept { return {}; }
        void return_value(T value) {
            shared_state_->value = std::move(value);
            shared_state_->ready = true;
        }
        void unhandled_exception() {
            shared_state_->err = std::current_exception();
            shared_state_->ready = true;
        }
    };
    async(const async &other) = delete;
    async &operator=(const async &other) = delete;
    async(async &&other) noexcept = default;
    async &operator=(async &&other) noexcept = default;
    ~async() = default;
    awaiter operator co_await() const noexcept {
        return awaiter{shared_state_};
    }
    static std::pair<async, producer> make_pending() {
        auto shared_state = std::make_shared<state>();
        return std::make_pair(async(shared_state), producer(shared_state));
    }
};
template <> class async<void> {
  private:
    struct state {
        bool ready = false;
        std::exception_ptr err;
        std::coroutine_handle<> continuation{};
    };
    struct awaiter {
        std::shared_ptr<state> shared_state_;
        bool await_ready() const noexcept { return shared_state_->ready; }
        void await_suspend(std::coroutine_handle<> handle) noexcept {
            shared_state_->continuation = handle;
        }
        void await_resume() {
            if (shared_state_->err) {
                std::rethrow_exception(shared_state_->err);
            }
        }
    };
    std::shared_ptr<state> shared_state_;
    explicit async(std::shared_ptr<state> state)
        : shared_state_(std::move(state)) {}

  public:
    struct producer {
      private:
        std::shared_ptr<state> shared_state_;

      public:
        explicit producer(std::shared_ptr<state> state)
            : shared_state_(std::move(state)) {}
        void set_value() {
            shared_state_->ready = true;
            if (shared_state_->continuation) {
                shared_state_->continuation.resume();
            }
        }
        void set_error(std::exception_ptr err) {
            shared_state_->err = std::move(err);
            shared_state_->ready = true;
            if (shared_state_->continuation) {
                shared_state_->continuation.resume();
            }
        }
    };
    struct promise_type {
      private:
        std::shared_ptr<state> shared_state_ = std::make_shared<state>();
        struct final_awaiter {
            bool await_ready() const noexcept { return false; }
            void await_suspend(
                std::coroutine_handle<promise_type> handle) const noexcept {
                auto &shared_state = handle.promise().shared_state_;
                if (shared_state->continuation) {
                    shared_state->continuation.resume();
                }
            }
            void await_resume() const noexcept {}
        };

      public:
        async get_return_object() { return async(shared_state_); }
        std::suspend_never initial_suspend() const noexcept { return {}; }
        final_awaiter final_suspend() const noexcept { return {}; }
        void return_void() { shared_state_->ready = true; }
        void unhandled_exception() {
            shared_state_->err = std::current_exception();
            shared_state_->ready = true;
        }
    };
    async(const async &other) = delete;
    async &operator=(const async &other) = delete;
    async(async &&other) noexcept = default;
    async &operator=(async &&other) noexcept = default;
    ~async() = default;
    awaiter operator co_await() const noexcept {
        return awaiter{shared_state_};
    }
    static std::pair<async, producer> make_pending() {
        auto shared_state = std::make_shared<state>();
        return std::make_pair(async(shared_state), producer(shared_state));
    }
};

template <typename T> T sync_wait(async<T> operation) {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    std::optional<T> result;
    std::exception_ptr err;
    auto runner = [&]() -> async<void> {
        try {
            result = co_await std::move(operation);
        } catch (...) {
            err = std::current_exception();
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            done = true;
        }
        cv.notify_one();
        co_return;
    };
    auto task = runner();
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&] { return done; });
    if (err) {
        std::rethrow_exception(err);
    }
    return std::move(*result);
}

inline void sync_wait(async<void> operation) {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    std::exception_ptr err;
    auto runner = [&]() -> async<void> {
        try {
            co_await std::move(operation);
        } catch (...) {
            err = std::current_exception();
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            done = true;
        }
        cv.notify_one();
        co_return;
    };
    auto task = runner();
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&] { return done; });
    if (err) {
        std::rethrow_exception(err);
    }
}
} // namespace orbwvr
