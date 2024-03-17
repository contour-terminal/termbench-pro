#!/usr/bin/env bash

#
# Usage: Xvfb-contour-run.sh <contour-args>

set -x


LIBGL_ALWAYS_SOFTWARE="${LIBGL_ALWAYS_SOFTWARE:-true}"
DISPLAY=:99
export LIBGL_ALWAYS_SOFTWARE
export DISPLAY

Xvfb $DISPLAY -screen 0 1280x1024x24 &
XVFB_PID=$!
trap "kill $XVFB_PID" EXIT

sleep 3

TB_BIN=$PWD/build/tb/tb

contour display ${DISPLAY} $TB_BIN --output contour_results
mv $HOME/contour_results .
kitty -e $TB_BIN --output kitty_results
xterm -display ${DISPLAY} -e $TB_BIN --output xterm_results
alacritty -e $TB_BIN --output alacritty_results

if [[ "$GITHUB_OUTPUT" != "" ]]; then
    echo "exitCode=$?" >> "$GITHUB_OUTPUT"
fi
