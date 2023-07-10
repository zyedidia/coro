#pragma once

#include <coroutine>
#include <cstdint>
#include <cstdio>
#include <exception>

struct scheduler_queue {
    constexpr static int const N = 256;

    using coro_handle = std::coroutine_handle<>;

    size_t head = 0;
    size_t tail = 0;

    coro_handle arr[N];

    void push_back(coro_handle h) {
        arr[head] = h;
        head = (head + 1) % N;
    }

    coro_handle pop_front() {
        auto result = arr[tail];
        tail = (tail + 1) % N;
        return result;
    }

    auto try_pop_front() { return head != tail ? pop_front() : coro_handle{}; }

    void run() {
        while (auto h = try_pop_front()) {
            h.resume();
        }
    }
};

inline scheduler_queue scheduler;

struct schedule_awaitable {
    bool await_ready() { return false; }

    template <typename Handle>
    auto await_suspend(Handle h) {
        auto& q = scheduler;
        q.push_back(h);
        return q.pop_front();
    }

    void await_resume() {}
};

static inline auto schedule() {
    return schedule_awaitable{};
}

struct throttler;

struct task_awaiter;

struct coro_task {
    struct promise_type;
    using coro_handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        using coro_handle_type = coro_task::coro_handle_type;
        throttler* owner = nullptr;

        coro_handle_type continuation;

        promise_type() : continuation{} {}

        coro_task get_return_object() {
            return coro_task{coro_handle_type::from_promise(*this)};
        }

        auto initial_suspend() { return std::suspend_always{}; }

        auto final_suspend() noexcept {
            struct final_awaiter {
                promise_type& me;

                bool await_ready() noexcept { return !me.continuation; }

                std::coroutine_handle<> await_suspend(coro_handle_type) noexcept {
                    if (me.continuation) {
                        return me.continuation;
                    }
                    return std::noop_coroutine();
                }

                void await_resume() noexcept {}
            };

            return final_awaiter{*this};
        }

        void return_void();

        void unhandled_exception() noexcept { std::terminate(); }
    };

    coro_task(coro_handle_type coro_handle_) : coro_handle{coro_handle_} {}

    ~coro_task() {
        if (coro_handle) {
            coro_handle.destroy();
        }
    }

    auto set_owner(throttler* owner) {
        auto result = coro_handle;
        // printf("setting owner %p %p\n", &coro_handle.promise(), owner);
        coro_handle.promise().owner = owner;
        coro_handle = nullptr;
        return result;
    }

    task_awaiter operator co_await();

    bool resume() {
        if (!coro_handle.done()) {
            coro_handle.resume();
        }

        return !coro_handle.done();
    }

   private:
    coro_handle_type coro_handle;
};


struct task_awaiter {
    using coro_handle_type = coro_task::coro_handle_type;

    task_awaiter(coro_handle_type coro_handle_) : coro_handle{coro_handle_} {}

    bool await_ready() { return false; }

    auto await_suspend(coro_handle_type awaiting_coro_) {
        coro_handle.promise().continuation = awaiting_coro_;
        return coro_handle;
    }

    void await_resume() {}

   private:
    coro_handle_type coro_handle;
};

inline task_awaiter coro_task::operator co_await() {
    return task_awaiter{this->coro_handle};
}

struct throttler {
    size_t limit;

    explicit throttler(size_t limit_) : limit{limit_} {}

    void on_task_done() { ++limit; }

    void spawn(coro_task t) {
        if (0 == limit) {
            scheduler.pop_front().resume();
        }

        auto handle = t.set_owner(this);
        scheduler.push_back(handle);

        --limit;
    }

    void run() { scheduler.run(); }

    ~throttler() { run(); }
};

inline void coro_task::promise_type::return_void() {
    if (owner) {
        owner->on_task_done();
    }
}
