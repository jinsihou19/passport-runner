#include "passport_pomodoro_model.h"

static const uint32_t FOCUS_SECONDS[] = {15 * 60, 25 * 60, 45 * 60};
enum { PRESETS = sizeof(FOCUS_SECONDS) / sizeof(FOCUS_SECONDS[0]), REST_SECONDS = 5 * 60, US_PER_SEC = 1000000 };

uint32_t passport_pomodoro_duration(const passport_pomodoro_t *timer)
{
    return timer->state.resting ? REST_SECONDS : FOCUS_SECONDS[timer->state.preset];
}

void passport_pomodoro_restore(passport_pomodoro_t *timer)
{
    timer->deadline_us = 0;
    if (timer->state.preset >= PRESETS || timer->state.resting > 1 || !timer->state.remaining ||
        timer->state.remaining > passport_pomodoro_duration(timer)) {
        timer->state = (passport_pomodoro_state_t){.preset = 1, .remaining = FOCUS_SECONDS[1]};
    }
}

bool passport_pomodoro_tick(passport_pomodoro_t *timer, int64_t now_us)
{
    if (!timer->deadline_us) return false;
    if (now_us < timer->deadline_us) {
        timer->state.remaining = (uint32_t)((timer->deadline_us - now_us + US_PER_SEC - 1) / US_PER_SEC);
        return false;
    }
    timer->deadline_us = 0;
    timer->state.resting = !timer->state.resting;
    timer->state.remaining = passport_pomodoro_duration(timer);
    return true;
}

void passport_pomodoro_toggle(passport_pomodoro_t *timer, int64_t now_us)
{
    if (timer->deadline_us) {
        passport_pomodoro_tick(timer, now_us);
        timer->deadline_us = 0;
    } else {
        timer->deadline_us = now_us + (int64_t)timer->state.remaining * US_PER_SEC;
    }
}

bool passport_pomodoro_adjust(passport_pomodoro_t *timer, int direction)
{
    if (timer->deadline_us) return false;
    if (!timer->state.resting && timer->state.remaining == passport_pomodoro_duration(timer)) {
        unsigned step = direction > 0 ? 1 : PRESETS - 1;
        timer->state.preset = (timer->state.preset + step) % PRESETS;
    } else if (direction > 0) {
        timer->state.resting = 0;
    } else {
        return false;
    }
    timer->state.remaining = passport_pomodoro_duration(timer);
    return true;
}
