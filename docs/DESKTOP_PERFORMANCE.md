# Desktop refresh performance

The compositor previously wrote a small framebuffer region and then issued
`FBIOPAN_DISPLAY`. The kernel interpreted that request as a complete VMware
SVGA update, so cursor movement and application frames repeatedly submitted and
waited for the whole display. The desktop now uses the LeonOS fbdev extension
`LEONOS_FBIOUPDATE_REGION` (`0x46f1`) after each blit. The kernel clamps the
rectangle to the framebuffer and submits only that region; `FBIOPAN_DISPLAY`
remains the explicit full-refresh operation.

This removes the unnecessary full-screen transfer and synchronous wait from
normal dirty-region repaint. It does not claim a hardware refresh rate: VMware
host scheduling, the selected virtual GPU, and application rendering still
determine the observed rate. `glxgears`' counter measures submitted frames and
must not be used alone as scanout evidence.

For a compositor sample, create `/etc/leonos/desktop-profile` in the guest and
restart the desktop. It logs `[desktop-perf] frames=... elapsed_ms=...
paint_ms=... inputm_ms=...` every five seconds. The profile is disabled by
default and has no effect on normal images.

Verification on 2026-09-12:

- `python3 -m unittest tools.test_runtime_responsiveness.RuntimeResponsivenessTests.test_framebuffer_reports_hardware_limits_and_remaps_after_mode_change tools.test_runtime_responsiveness.RuntimeResponsivenessTests.test_window_repaints_reuse_live_shared_memory`: pass.
- `python3 build.py run app:desktop`: 0 errors.
- Existing `tools/test_svga.py` remains the driver-level FIFO/update regression;
  VMware-specific 60 FPS measurement remains pending.
