#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--firmware]" >&2
}

run_static_checks() {
    local actionlint_bin
    local test_dir
    local dead_strip

    python3 tools/check_repo.py

    actionlint_bin="${ACTIONLINT_BIN:-}"
    if [[ -z "${actionlint_bin}" ]]; then
        actionlint_bin="$(command -v actionlint || true)"
    fi
    if [[ -z "${actionlint_bin}" || ! -x "${actionlint_bin}" ]]; then
        actionlint_bin="$(./tools/install-actionlint.sh)"
    fi
    "${actionlint_bin}" -color .github/workflows/*.yml

    test_dir="$(mktemp -d /tmp/ai-passport-host-tests.XXXXXX)"
    "${CC:-cc}" -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Imain/common -Imain/common/network -Imain/apps/esp-claw \
        tests/test_passport_input.c main/common/network/passport_input.c main/apps/esp-claw/passport_model.c \
        -o "${test_dir}/test_passport_input"
    "${test_dir}/test_passport_input"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/apps/pomodoro \
        tests/test_passport_pomodoro.c main/apps/pomodoro/passport_pomodoro_model.c \
        -o "${test_dir}/test_passport_pomodoro"
    "${test_dir}/test_passport_pomodoro"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/runner \
        tests/test_passport_manage_menu.c main/runner/passport_manage_menu.c \
        -o "${test_dir}/test_passport_manage_menu"
    "${test_dir}/test_passport_manage_menu"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain/runner \
        tests/test_passport_app_pool.c main/runner/passport_app_pool.c \
        -o "${test_dir}/test_passport_app_pool"
    "${test_dir}/test_passport_app_pool"
    "${CC:-cc}" -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -Imain/runner \
        tests/test_passport_launcher.c main/runner/passport_launcher.c \
        -o "${test_dir}/test_passport_launcher"
    "${test_dir}/test_passport_launcher"
    python3 tools/generate_passport_font.py --check
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_display_rounding.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_display_rounding"
    "${test_dir}/test_bsp_display_rounding"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_es8311_sleep_check.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_es8311_sleep_check"
    "${test_dir}/test_bsp_es8311_sleep_check"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/battery_stubs -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_battery.c -o "${test_dir}/test_bsp_battery"
    "${test_dir}/test_bsp_battery"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_button.c -o "${test_dir}/test_bsp_button"
    "${test_dir}/test_bsp_button"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_lvgl_init.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_lvgl_init"
    "${test_dir}/test_bsp_lvgl_init"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/audio_stubs -Icomponents/bsp/include -Icomponents/bsp/src \
        tests/test_bsp_audio_recovery.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_audio_recovery"
    "${test_dir}/test_bsp_audio_recovery"
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_battery_profile.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_check_repo.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_verify_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_archive_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_extract_passport_app.py
    if command -v node >/dev/null 2>&1; then
        node --test tests/test_passport_import.mjs
    else
        echo "Browser import parser tests: NOT RUN (Node.js unavailable)"
    fi
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_install_passport_skills.py
    rm -rf "${test_dir}"
    echo "Host tests: PASS"
}

run_firmware_checks() (
    local validation_build_dir manager_build claw_build pomodoro_build

    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py is not available; activate ESP-IDF 5.5.3 first." >&2
        return 1
    fi

    validation_build_dir="$(mktemp -d /tmp/ai-passport-firmware.XXXXXX)"
    trap 'case "${validation_build_dir}" in /tmp/ai-passport-firmware.*) rm -rf -- "${validation_build_dir}" ;; esac' EXIT
    manager_build="${validation_build_dir}/manager"
    claw_build="${validation_build_dir}/claw"
    pomodoro_build="${validation_build_dir}/pomodoro"

    build_variant() {
        local app="$1" output="$2"
        SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults" \
            idf.py -B "${output}" -D "SDKCONFIG=${output}/sdkconfig" \
                -D "PASSPORT_APP=${app}" build
    }

    build_variant claw "${claw_build}"
    build_variant pomodoro "${pomodoro_build}"
    build_variant manager "${manager_build}"
    idf.py -B "${manager_build}" merge-bin -o "${manager_build}/FoloToy-AI-Passport-full.bin"
    python3 tools/verify_firmware.py "${manager_build}"
    PYTHONDONTWRITEBYTECODE=1 python3 tools/package_passport_apps.py \
        "${manager_build}" "${claw_build}" "${pomodoro_build}" "${repo_root}/build/apps"
    PYTHONDONTWRITEBYTECODE=1 python3 tools/archive_firmware.py create \
        "${manager_build}" --archive-root "${repo_root}/build/firmware"
    mkdir -p "${repo_root}/build"
    install -m 0644 \
        "${manager_build}/FoloToy-AI-Passport-full.bin" \
        "${repo_root}/build/FoloToy-AI-Passport-full.bin"
    echo "Firmware build: PASS"
)

cd "${repo_root}"
case "${mode}" in
    --all)
        run_static_checks
        run_firmware_checks
        ;;
    --static)
        run_static_checks
        ;;
    --firmware)
        run_firmware_checks
        ;;
    *)
        usage
        exit 2
        ;;
esac
