#include <assert.h>

#include "passport_pomodoro_model.h"

int main(void)
{
    const int64_t second = 1000000;
    passport_pomodoro_t timer = {.state = {.remaining = 90, .preset = 99}, .deadline_us = 123};
    passport_pomodoro_restore(&timer);
    assert(timer.state.remaining == 25 * 60 && !timer.deadline_us && !timer.state.resting);

    assert(passport_pomodoro_adjust(&timer, -1));
    assert(timer.state.remaining == 15 * 60);
    passport_pomodoro_toggle(&timer, second);
    assert(!passport_pomodoro_adjust(&timer, 1));
    passport_pomodoro_toggle(&timer, 11 * second + 100);
    assert(!timer.deadline_us && timer.state.remaining == 15 * 60 - 10);

    // Reopening from saved state preserves the remaining time without restarting the countdown.
    passport_pomodoro_t reopened = {.state = timer.state, .deadline_us = 1};
    passport_pomodoro_restore(&reopened);
    assert(!passport_pomodoro_tick(&reopened, 100 * second));
    assert(reopened.state.remaining == timer.state.remaining && !reopened.deadline_us);
    passport_pomodoro_toggle(&reopened, 100 * second);
    int64_t deadline = reopened.deadline_us;
    assert(!passport_pomodoro_tick(&reopened, deadline - 1));
    assert(reopened.state.remaining == 1);
    assert(passport_pomodoro_tick(&reopened, deadline));
    assert(!reopened.deadline_us && reopened.state.resting && reopened.state.remaining == 5 * 60);
    passport_pomodoro_toggle(&reopened, deadline);
    assert(passport_pomodoro_tick(&reopened, reopened.deadline_us));
    assert(!reopened.state.resting && reopened.state.remaining == 15 * 60);
    return 0;
}
