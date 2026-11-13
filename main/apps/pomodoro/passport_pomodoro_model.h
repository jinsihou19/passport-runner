#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t remaining;
    uint8_t preset;
    uint8_t resting;
} passport_pomodoro_state_t;

typedef struct {
    passport_pomodoro_state_t state;
    int64_t deadline_us;
} passport_pomodoro_t;

void passport_pomodoro_restore(passport_pomodoro_t *timer);
uint32_t passport_pomodoro_duration(const passport_pomodoro_t *timer);
bool passport_pomodoro_tick(passport_pomodoro_t *timer, int64_t now_us);
void passport_pomodoro_toggle(passport_pomodoro_t *timer, int64_t now_us);
bool passport_pomodoro_adjust(passport_pomodoro_t *timer, int direction);
