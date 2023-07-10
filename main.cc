#include "coro.hh"

coro_task foo() {
    for (int i = 0; i < 10; i++) {
        printf("foo %d\n", i);
        co_await schedule();
    }
    co_return;
}

coro_task bar() {
    for (int i = 0; i < 10; i++) {
        printf("bar %d\n", i);
        co_await schedule();
    }
    co_return;
}

int main() {
    throttler t{64};
    t.spawn(foo());
    t.spawn(bar());
    t.run();
    return 0;
}
