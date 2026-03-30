## Branch: `fix/windows-frame-pacing`

This branch replaces the legacy Windows `WM_TIMER` frame delivery mechanism with a DWM-synchronized background thread, eliminating frame pacing issues and enabling consistent sub-millisecond frame timing on Windows.

---

## The problem

The original Windows implementation used `WM_TIMER` for frame delivery. This had two fundamental issues:

- **~15.6ms system timer resolution** — Windows defaults to a 15.6ms timer interrupt interval, making it impossible to accurately schedule 60fps frames (16.67ms budget) with `WM_TIMER` alone.
- **Queue-idle delivery** — `WM_TIMER` is a low-priority message that Windows only delivers when the message queue is idle. Under any load, frames arrived late and erratically, causing visible stutter regardless of timer interval settings.

The result was inconsistent frame pacing — frames that should arrive every 16.67ms at 60fps would instead arrive at irregular intervals, causing jitter and stutter especially during smooth scrolling and camera movement.

---

## The fix

Replaced `WM_TIMER` with a DWM-synchronized background thread in `WinTimer`. When DWM composition is available (Windows Vista+, all modern hardware), the thread:

1. Queries the monitor refresh rate via `EnumDisplaySettings`
2. Uses `timeBeginPeriod(1)` to raise system timer resolution to 1ms
3. Sleeps until ~1ms before each frame deadline
4. Spins with `YieldProcessor()` for sub-millisecond precision in the final millisecond
5. Posts `WM_CORONA_TIMER` to the main thread — keeping Lua runtime calls safely on the main thread

The result is frame delivery phase-locked to the monitor's refresh cycle with ~1ms jitter, consistent at 60fps, 120fps, and any refresh rate the monitor supports.

On systems where DWM is unavailable (remote desktop, older Windows), the implementation falls back to the original `WM_TIMER` behavior automatically.

---

## Architecture

```
Background thread (DWM-sync)          Main thread
─────────────────────────────         ──────────────────────
QueryPerformanceCounter loop
  │
  ├─ Sleep (deadline - 1ms)
  │
  └─ Spin (YieldProcessor)
       │
       └─ Deadline reached
            │
            ├─ fTickPending gate ──── prevents queue flooding
            │
            └─ PostMessage(WM_CORONA_TIMER)
                                           │
                                      RenderSurfaceControl
                                           │
                                      WinTimer::Evaluate()
                                           │
                                      Runtime::operator()()
                                      (logic + render)
```

**`fTickPending` gate** — ensures at most one `WM_CORONA_TIMER` is in the message queue at any time. If the main thread is busy when the next tick fires, the tick is skipped rather than queued. The timing loop continues advancing so no drift accumulates. This mirrors the natural idle-queue behavior of `WM_TIMER` without sacrificing frame timing precision.

---

## Files changed

```
platform/windows/Corona.Native.Library.Win32/Rtt/Rtt_WinTimer.h
platform/windows/Corona.Native.Library.Win32/Rtt/Rtt_WinTimer.cpp
platform/windows/Corona.Native.Library.Win32/Rtt/Rtt_WinScreenSurface.cpp
platform/windows/Corona.Native.Library.Win32/Interop/RuntimeEnvironment.cpp
platform/windows/Corona.Native.Library.Win32/Interop/UI/RenderSurfaceControl.cpp
```

All changes are strictly Windows-specific. No shared engine code (`librtt`) was modified in this branch.

---

## Key implementation details

### `Rtt_WinTimer.cpp` — `ThreadLoop()`
- Queries monitor Hz via `GetRefreshRate()` → `EnumDisplaySettings`
- Sleep/spin loop phase-locked to `targetFrameTime = 1.0 / refreshRate`
- `fTickPending` atomic flag gates message posting

### `Rtt_WinTimer.cpp` — `Start()` / `Stop()`
- `timeBeginPeriod(1)` on start, `timeEndPeriod(1)` on stop
- Background thread created with `CreateThread`, signaled to stop via `CreateEvent`
- Falls back to `SetTimer` if `DwmIsCompositionEnabled` returns false

### `Rtt_WinScreenSurface.cpp`
- Additional `timeBeginPeriod(1)` for the lifetime of the rendering surface
- Ensures 1ms resolution is active even during edge cases where the timer is not running

### `RuntimeEnvironment.cpp` — `DidResume()`
- Resets `fTickPending` after every runtime resume
- Placed in `DidResume()` rather than `Resume()` because the Simulator calls `Rtt::Runtime::Resume()` directly, bypassing `RuntimeEnvironment::Resume()`

---

## Fallback behavior

| Environment | Path used |
|---|---|
| Windows 8+ with DWM | DWM-sync thread (this fix) |
| Remote desktop | Legacy `WM_TIMER` fallback |
| Older Windows without DWM | Legacy `WM_TIMER` fallback |

---

## No breaking changes

All existing projects build and run identically. The fallback path preserves the original `WM_TIMER` behavior exactly for environments where DWM is unavailable.
