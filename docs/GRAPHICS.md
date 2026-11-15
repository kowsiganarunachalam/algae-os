# Algae OS graphics boundary

Algae Home is a Wayland client. It connects to the socket named by
`WAYLAND_DISPLAY` under `XDG_RUNTIME_DIR` and uses `wl_compositor`, `wl_shm`,
`wl_seat`, and `xdg_wm_base`. The UI does not launch Weston or use Weston APIs.
Its source and build are kept in `system/algae-home/` and
`scripts/build-algae-home.sh`.

`algae-graphical-session` supervises the selected compositor and starts Algae
Home after the configured Wayland socket appears. The default selection is
Weston, configured in `/etc/algae/graphical-session.conf`. Compositor binary,
arguments, socket name, and runtime directory are independent settings. A
replacement compositor can be selected there once its binary is included in
the image and it provides the Wayland protocols and input behavior Algae Home
uses. Weston-specific settings remain in `weston.ini`; they are not UI
settings.

This is a compositor boundary, not a claim that every compositor is a
drop-in replacement. For example, Smithay is a Rust compositor framework, so
Algae OS would need a compositor server built with Smithay and the required
DRM, input, and Wayland protocol support. The existing Algae Home client can
remain separate if that server honors its Wayland requirements.

The current UI draws with Cairo into shared-memory Wayland buffers. That keeps
the client simple and portable, but does not provide GPU-accelerated
transitions. Smooth mobile transitions will require profiling on the target
hardware and likely a GPU-backed UI/rendering path; changing the compositor
alone does not guarantee them.
