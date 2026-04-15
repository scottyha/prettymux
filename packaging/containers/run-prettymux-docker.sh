#!/usr/bin/env bash
#
# run-prettymux-docker.sh — launch PrettyMux in Docker for manual testing.
#
# Unlike verify-strip-layout.sh, this script does not run smoke assertions or
# intentionally terminate PrettyMux after launch. It keeps the app running in
# the foreground so you can interact with it manually on your desktop.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
GHOSTTY_ROOT="${GHOSTTY_ROOT:-$REPO_ROOT/../ghostty}"
IMAGE_NAME="${IMAGE_NAME:-prettymux-strip-verify}"

if [[ ! -d "$REPO_ROOT/src/gtk" ]]; then
  echo "Repository root not found: $REPO_ROOT" >&2
  exit 1
fi

if [[ ! -f "$GHOSTTY_ROOT/zig-out/lib/libghostty.so" ]]; then
  echo "Ghostty shared library not found at $GHOSTTY_ROOT/zig-out/lib/libghostty.so" >&2
  exit 1
fi

docker_extra_args=()
docker_tty_args=()
runtime_mode="weston"
container_runtime_dir="/tmp/xdg-runtime"
container_wayland_display="wayland-1"

if [[ -n "${WAYLAND_DISPLAY:-}" ]] && [[ -n "${XDG_RUNTIME_DIR:-}" ]] && [[ -S "${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}" ]]; then
  runtime_mode="host-wayland"
  container_runtime_dir="$XDG_RUNTIME_DIR"
  container_wayland_display="$WAYLAND_DISPLAY"
  docker_extra_args+=(
    --user "$(id -u):$(id -g)"
    -e "WAYLAND_DISPLAY=$container_wayland_display"
    -e "XDG_RUNTIME_DIR=$container_runtime_dir"
    -e "XDG_SESSION_TYPE=wayland"
    -v "${XDG_RUNTIME_DIR}:${XDG_RUNTIME_DIR}"
  )
  if [[ -e /dev/dri ]]; then
    docker_extra_args+=(--device /dev/dri)
  fi
else
  docker_extra_args+=(
    -e "WAYLAND_DISPLAY=$container_wayland_display"
    -e "XDG_RUNTIME_DIR=$container_runtime_dir"
    -e "XDG_SESSION_TYPE=wayland"
  )
fi

if [[ -t 0 && -t 1 ]]; then
  docker_tty_args=(-it)
fi

echo "=== Building Docker image ==="
docker build -t "$IMAGE_NAME" \
  -f "$SCRIPT_DIR/debian-bookworm.Dockerfile" \
  "$SCRIPT_DIR"

echo "=== Launching PrettyMux in Docker ($runtime_mode) ==="
echo "Close PrettyMux or stop the container to exit."

docker run --rm \
  "${docker_tty_args[@]}" \
  "${docker_extra_args[@]}" \
  -e "PRETTYMUX_VERIFY_RUNTIME=$runtime_mode" \
  -e "PRETTYMUX_VERIFY_REPO=/workspace" \
  -e "PRETTYMUX_VERIFY_GHOSTTY=/ghostty" \
  -e "HOME=/tmp/prettymux-home" \
  -v "$REPO_ROOT:/workspace" \
  -v "$GHOSTTY_ROOT:/ghostty" \
  -w /tmp \
  "$IMAGE_NAME" \
  bash -lc '
    set -euo pipefail

    export HOME=/tmp/prettymux-home
    mkdir -p "$HOME" /tmp/build
    cd /tmp/build

    echo "--- Meson setup ---"
    meson setup builddir "$PRETTYMUX_VERIFY_REPO/src/gtk" --prefix=/usr -Dghostty_dir="$PRETTYMUX_VERIFY_GHOSTTY"

    echo "--- Build prettymux + prettymux-open ---"
    ninja -C builddir prettymux prettymux-open

    if [[ "$PRETTYMUX_VERIFY_RUNTIME" = "weston" ]]; then
      echo "--- Starting fallback Weston compositor ---"
      mkdir -p "$XDG_RUNTIME_DIR"
      weston --backend=headless-backend.so --socket="$WAYLAND_DISPLAY" --idle-time=0 --no-config >/tmp/weston.log 2>&1 &
      WESTON_PID=$!
      sleep 2
      if ! kill -0 "$WESTON_PID" 2>/dev/null; then
        echo "Weston failed to start" >&2
        cat /tmp/weston.log >&2 || true
        exit 1
      fi
      trap "kill \"$WESTON_PID\" 2>/dev/null || true; wait \"$WESTON_PID\" 2>/dev/null || true" EXIT
    fi

    rm -f /tmp/prettymux-*.sock

    echo "--- Launch PrettyMux ---"
    echo "Runtime mode: $PRETTYMUX_VERIFY_RUNTIME"
    echo "Wayland display: $WAYLAND_DISPLAY"
    echo "XDG_RUNTIME_DIR: $XDG_RUNTIME_DIR"
    echo "You can use another terminal to run:"
    echo "  docker ps"
    echo "  docker exec -it <container-id> bash"
    echo "then inside the container:"
    echo "  cd /tmp/build"
    echo "  ./builddir/prettymux-open --list-workspaces"

    exec env LD_LIBRARY_PATH="$PRETTYMUX_VERIFY_GHOSTTY/zig-out/lib" ./builddir/prettymux
  '
