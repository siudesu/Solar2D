## Branch: `feature/windows-render-decouple`

This branch builds on `fix/windows-frame-pacing` to decouple the logic update and rendering paths in the Windows engine, adds 120fps support, introduces a monitor refresh rate cap, and exposes new Lua APIs for high-refresh-rate displays.

---

## The problem

The original engine fused logic and rendering into a single synchronous call — `Runtime::operator()()`. Every frame ran the scheduler, dispatched Lua `enterFrame`, stepped physics, and rendered — all in one tick. This made it impossible to:

- Run logic at a fixed rate while rendering at a higher rate
- Support 120fps logic on hardware that can run it
- Lay groundwork for future engine-side interpolation

---

## What changed

### Logic / render decoupling (Windows)

`Runtime::operator()()` is split into two independent methods on Windows:

- `Runtime::Step()` — advances simulation by one fixed tick: scheduler, Lua `enterFrame`, physics, display object state. Does not render.
- `Runtime::Render()` — renders the current frame state. Already existed, now called explicitly and separately.
- `Runtime::operator()()` — reduced to a shim calling `Step()` + `Render()` in sequence, keeping the Simulator and legacy path unchanged.

All other platforms continue to use the original `operator()()` implementation exactly — no behavioral change.

### 120fps support (Windows)

`config.lua` now accepts `fps = 120` on Windows alongside `fps = 30` and `fps = 60`. Other platforms continue to support `fps = 30` and `fps = 60` only.

### Monitor refresh rate cap

`BeginRunLoop()` queries the monitor refresh rate at startup and caps `fFPS` downward if the configured value exceeds it. A warning is logged when the cap is applied.

- `fps = 120` on a 120Hz monitor → runs at 120fps
- `fps = 120` on a 60Hz monitor → auto-capped to 60fps, warning logged
- `fps = 30` on any monitor → always runs at 30fps (downward only)
- Non-Windows platforms → `GetRefreshRate()` returns 0.0 (unknown), cap is skipped entirely

### Two message types

`ThreadLoop()` now posts one of two messages per VSYNC tick:

- `WM_CORONA_TIMER` — logic tick due. Main thread runs `Step()` + `Render()`.
- `WM_CORONA_RENDER` — VSYNC fired, no logic tick due. Only posted when `setRenderSync(true)` is enabled. Main thread queues a `WM_PAINT` to redraw the current frame state.

When `setRenderSync` is disabled (default), render-only ticks release `fTickPending` immediately without posting — render runs at logic rate with no duplicate frames.

---

## Architecture

```
DWM sync thread                        Main thread
──────────────────────────────         ──────────────────────────────
VSYNC tick
  │
  ├─ accumulator += targetFrameTime
  │
  ├─ acc >= intervalSeconds?
  │     YES → doStep = true
  │            accumulator = 0
  │
  ├─ fTickPending gate
  │
  ├─ doStep?
  │     YES → PostMessage(WM_CORONA_TIMER) ──────────────→ Step() + Render()
  │
  ├─ frameSync enabled?
  │     YES → PostMessage(WM_CORONA_RENDER) ─────────────→ InvalidateRect → WM_PAINT
  │
  └─ NO  → release fTickPending (no message posted)


config.lua fps
  └─ intervalSeconds = 1 / fps ──→ accumulator threshold
  └─ BeginRunLoop() caps fFPS to monitor Hz if exceeded
```

---

## Frame rate behavior

| Config fps | Monitor Hz | setRenderSync | Logic rate | Render rate | Notes |
|---|---|---|---|---|---|
| 30 | 60Hz | false | 30fps | 30fps | standard |
| 30 | 120Hz | false | 30fps | 30fps | logic respected |
| 30 | 144Hz | false | 30fps | 30fps | logic respected |
| 60 | 56Hz | false | 56fps (capped) | 56fps | capped to monitor |
| 60 | 60Hz | false | 60fps | 60fps | standard |
| 60 | 75Hz | false | 60fps | 60fps | logic respected |
| 60 | 120Hz | false | 60fps | 60fps | logic respected |
| 60 | 144Hz | false | 60fps | 60fps | logic respected |
| 120 | 60Hz | false | 60fps (capped) | 60fps | capped to monitor |
| 120 | 75Hz | false | 75fps (capped) | 75fps | capped to monitor |
| 120 | 120Hz | false | 120fps | 120fps | standard |
| 120 | 144Hz | false | 120fps | 120fps | logic respected |
| 60 | 60Hz | true | 60fps | 60fps | no benefit* |
| 60 | 75Hz | true | 60fps | 75fps | render-only* |
| 60 | 120Hz | true | 60fps | 120fps | render-only* |
| 60 | 144Hz | true | 60fps | 144fps | render-only* |
| 120 | 144Hz | true | 120fps | 144fps | render-only* |

*render-only frames redraw last known state — no visual improvement without engine-side interpolation.

---

## New Lua API (Windows)

### `display.refreshRate`

Returns the monitor's actual refresh rate in Hz. Returns `nil` on non-Windows platforms.

```lua
local hz = display.refreshRate
if hz then
    print("Monitor: " .. hz .. "Hz")
end
```

### `display.setRenderSync(bool)`

Enables or disables VSYNC-rate rendering at runtime. Off by default.

When `true`, the render loop syncs to the monitor refresh rate while logic continues at `config.lua fps`. Without engine-side interpolation, render-only frames are redraws of the same state — this is foundation infrastructure for a future interpolation feature.

Returns a warning on non-Windows platforms.

```lua
display.setRenderSync(true)   -- sync render to monitor Hz
display.setRenderSync(false)  -- render at logic rate (default)
```

### `fps = 120` in config.lua (Windows only)

```lua
-- config.lua
application = {
    content = {
        fps = 120,  -- Windows only. Values: 30, 60, 120. Default: 30.
                    -- auto-capped to monitor refresh rate if exceeded.
    }
}
```

---

## Delta time

Games targeting multiple fps values should calculate delta time manually for frame-rate independent movement. Without it, a game at 120fps moves objects twice as fast as at 60fps.

```lua
local lastTime = system.getTimer()

Runtime:addEventListener("enterFrame", function(event)
    local now = system.getTimer()
    local dt = (now - lastTime) / 1000  -- seconds since last frame
    lastTime = now

    object.x = object.x + (object.speed * dt)  -- speed in units/second
end)
```

---

## Files changed

### Shared (all platforms)
```
librtt/Display/Rtt_LuaLibDisplay.cpp   — display.refreshRate, display.setRenderSync()
librtt/Rtt_PlatformTimer.h             — GetRefreshRate() virtual, SetFrameSync() virtual
librtt/Rtt_Runtime.cpp                 — Step(), operator()() shim, ReadConfig() 120fps, BeginRunLoop() cap
librtt/Rtt_Runtime.h                   — Step() declaration (WIN_ENV guard), kFrameSync property
```

### Windows only
```
platform/windows/.../Rtt_WinTimer.h              — WM_CORONA_RENDER, fLastMessage, fFrameSync, overrides
platform/windows/.../Rtt_WinTimer.cpp            — ThreadLoop() dual message, Evaluate() dispatch, SetFrameSync()
platform/windows/.../Rtt_WinScreenSurface.cpp    — timeBeginPeriod(1) for surface lifetime
platform/windows/.../RuntimeEnvironment.cpp      — fTickPending reset in DidResume()
platform/windows/.../UI/RenderSurfaceControl.cpp — WM_CORONA_RENDER handler, fLastMessage stamp
```

---

## Important notes

**`display.setRenderSync()`** is opt-in and off by default. Without engine-side interpolation, render-only ticks are redraws of the same frame state — on high refresh rate monitors this produces no visual improvement over running at the configured fps. The `Step()`/`Render()` split and `WM_CORONA_RENDER` path are foundation infrastructure for a future interpolation PR.

**Non-Windows platforms** — `operator()()` is preserved exactly using `#ifdef Rtt_WIN_ENV` guards. `GetRefreshRate()` returns `0.0` on non-Windows (cap skipped). `SetFrameSync()` is a no-op. `display.refreshRate` returns `nil`. `display.setRenderSync()` logs a warning. No behavioral change on any non-Windows platform.

---

## No breaking changes

All existing projects build and run identically unless `fps = 120` is explicitly set or `display.setRenderSync(true)` is called.
