//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_PlatformTimer_H__
#define _Rtt_PlatformTimer_H__

#include "Rtt_MCallback.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

class PlatformTimer
{
	Rtt_CLASS_NO_COPIES( PlatformTimer )

	public:
		PlatformTimer( MCallback& callback );
		virtual ~PlatformTimer();

	public:
		virtual void Start() = 0;
		virtual void Stop() = 0;
		virtual void SetInterval( U32 milliseconds ) = 0;
		virtual bool IsRunning() const = 0;

		// Problem: On iOS, when the screen locks and the display turns off,
		// CADisplayLink timers stop firing.
		// In my experiments, backgrounding allows the CADisplayLink to continue firing, though if the display sleeps/locks, the timer stops firing.
		// To avoid this problem, we need to switch off of CADisplayLink to NSTimer.
		// Since we can't easily distinguish between a screen lock and backgrounding, we treat both events as the same and switch to the background timer.
		// Note that CVDisplayLink may have similar issues on Mac when there is no display, offscreen rendering, and possibly VNC remote desktop.
		// But as a more general principle, we may want all platforms to use a "nice-to-CPU" background timer which invokes less frequently to ease CPU cost. 
		virtual void SwitchToForegroundTimer();
		virtual void SwitchToBackgroundTimer();

		// Returns the display refresh rate in Hz.
		// Overridden by platform-specific subclasses (e.g. WinTimer).
		// Defaults to 0 for platforms that do not provide a native query.
		virtual double GetRefreshRate() const { return 0.0; }

		/// <summary>
		///  Returns whether render-sync mode is enabled.
		///  When true, the display invalidates every vsync tick even when no
		///  logic tick has fired, syncing redraws to the monitor refresh rate.
		///  Always returns false on non-Windows platforms.
		/// </summary>
		virtual bool GetFrameSync() const { return false; }

		/// <summary>
		///  <para>Enables or disables render-sync mode.</para>
		///  <para>
		///   When true, the render loop calls InvalidateRect on every vsync tick
		///   where no logic step is due, keeping the display refreshing at monitor
		///   rate even when logic runs at a lower rate (e.g. 60fps on a 120Hz display).
		///   This reduces compositor jitter at the cost of ~1W additional GPU power draw.
		///  </para>
		///  <para>
		///   When false, the display redraws only when a logic tick fires.
		///  </para>
		///  <para>
		///   Defaults to true on Windows. No-op on non-Windows platforms.
		///   Can be changed at runtime via display.setDefault("renderSync", bool).
		///  </para>
		/// </summary>
		virtual void SetFrameSync(bool enabled) {}
	
	public:
		// Allow manual invocation
		Rtt_FORCE_INLINE void operator()() { fCallback(); }

	protected:
		Rtt_FORCE_INLINE MCallback& Callback() { return fCallback; }

	private:
		MCallback& fCallback;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_PlatformTimer_H__
