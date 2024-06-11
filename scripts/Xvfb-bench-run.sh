#!/usr/bin/env bash

#
# Usage: Xvfb-bench-run.sh <path-to-tb-executable>

TB_BIN="${1:-${TB_BIN}}"
CONTOUR_BIN="${CONTOUR_BIN:-contour}"
KITTY_BIN="${KITTY_BIN:-kitty}"
XTERM_BIN="${XTERM_BIN:-xterm}"
ALACRITTY_BIN="${ALACRITTY_BIN:-alacritty}"
WEZTERM_BIN="${WEZTERM_BIN:-wezterm}"
FB_DISPLAY="${FB_DISPLAY:-:99}"

OUTPUT_DIR="${PWD}"

if [[ "$TB_BIN" == "" ]]; then
    echo "Usage: $0 <path-to-tb-executable>"
    exit 1
fi

function require_bin() {
    local tool="${1}"
    if ! which "${tool}" >/dev/null; then
        echo 1>&2 "$0: Could not find the required tool ${tool}."
        exit 1
    fi
}

require_bin Xvfb
require_bin "${TB_BIN}"
require_bin "${CONTOUR_BIN}"
require_bin "${KITTY_BIN}"
require_bin "${XTERM_BIN}"
require_bin "${ALACRITTY_BIN}"
require_bin "${WEZTERM_BIN}"

export TB_BIN=$(realpath $TB_BIN)
export DISPLAY=${FB_DISPLAY}
export LIBGL_ALWAYS_SOFTWARE="${LIBGL_ALWAYS_SOFTWARE:-true}"

function program_exit() {
    local exit_code=$1
    if [[ "$GITHUB_OUTPUT" != "" ]]; then
        echo "exitCode=$exit_code" >> "$GITHUB_OUTPUT"
    fi
    exit $exit_code
}

function bench_terminal() {
    printf "\033[1m==> Running terminal: $1\033[m\n"
    local terminal_name=$(basename $1)
    time "${@}" -e "${TB_BIN}" --fixed-size --column-by-column --size 1 --output "${OUTPUT_DIR}/${terminal_name}_results"
    local exit_code=$?
    printf "\033[1m==> Terminal exit code: $exit_code\033[m\n"
    if [[ $exit_code -ne 0 ]]; then
        program_exit $exit_code
    fi
}

set -x

Xvfb $DISPLAY -screen 0 1280x1024x24 &
XVFB_PID=$!
trap "kill $XVFB_PID" EXIT

sleep 3

bench_terminal "${CONTOUR_BIN}" display ${DISPLAY}
bench_terminal "${KITTY_BIN}"
bench_terminal "${XTERM_BIN}" -display ${DISPLAY}
bench_terminal "${ALACRITTY_BIN}"
bench_terminal "${WEZTERM_BIN}"

program_exit 0
