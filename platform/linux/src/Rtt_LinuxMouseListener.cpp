#include "Rtt_LinuxMouseListener.h"
#include "Rtt_MPlatformDevice.h"
#include "Rtt_LinuxApp.h"
#include "Display/Rtt_Display.h"

namespace Rtt
{
	LinuxMouseListener::LinuxMouseListener()
		: fScaleX(1)
		, fScaleY(1)
		, fButtons(0)
	{
	}

	//  touch
	void LinuxMouseListener::TouchDown(int x, int y, int fid)
	{
		x = (int)(x / fScaleX);
		y = (int)(y / fScaleY);

		// sanity check
		bool notifyMultitouch = app->GetRuntime()->Platform().GetDevice().DoesNotify(MPlatformDevice::kMultitouchEvent);
		if (fStartPoint.find(fid) != fStartPoint.end() || (!notifyMultitouch && fStartPoint.size() > 0))
		{
			return;
		}

		fStartPoint[fid] = pt(x, y);

		TouchEvent t((float)x, (float)y, (float)x, (float)y, TouchEvent::kBegan);

		// it must not be ZERO!
		t.SetId((void*)(fid + 1));

#if Rtt_DEBUG_TOUCH
		printf("TouchDown %d(%d, %d)\n", fid, x, y);
#endif

		if (notifyMultitouch)
		{
			MultitouchEvent t2(&t, 1);
			DispatchEvent(t2);
		}
		else
		{
			DispatchEvent(t);
		}
	}

	void LinuxMouseListener::TouchMoved(int x, int y, int fid)
	{
		x = (int)(x / fScaleX);
		y = (int)(y / fScaleY);

#if Rtt_DEBUG_TOUCH
		// Commented out b/c it's very noisy
		//printf("TouchMoved %d(%d, %d)\n", id, x, y);
#endif

		// sanity check
		if (fStartPoint.find(fid) == fStartPoint.end())
		{
			return;
		}

		TouchEvent t((float)x, (float)y, (float)fStartPoint[fid].x, (float)fStartPoint[fid].y, TouchEvent::kMoved);

		// it must not be ZERO!
		t.SetId((void*)(fid + 1));

		bool notifyMultitouch = app->GetRuntime()->Platform().GetDevice().DoesNotify(MPlatformDevice::kMultitouchEvent);
		if (notifyMultitouch)
		{
			MultitouchEvent t2(&t, 1);
			DispatchEvent(t2);
		}
		else
		{
			DispatchEvent(t);
		}
	}

	void LinuxMouseListener::TouchUp(int x, int y, int fid)
	{
		x = (int)(x / fScaleX);
		y = (int)(y / fScaleY);

		// sanity check
		if (fStartPoint.find(fid) == fStartPoint.end())
		{
			return;
		}

		TouchEvent t((float)x, (float)y, (float)fStartPoint[fid].x, (float)fStartPoint[fid].y, TouchEvent::kEnded);

		// it must not be ZERO!
		t.SetId((void*)(fid + 1));

#if Rtt_DEBUG_TOUCH
		printf("TouchUp %d(%d, %d)\n", fid, x, y);
#endif

		bool notifyMultitouch = app->GetRuntime()->Platform().GetDevice().DoesNotify(MPlatformDevice::kMultitouchEvent);
		if (notifyMultitouch)
		{
			MultitouchEvent t2(&t, 1);
			DispatchEvent(t2);
		}
		else
		{
			DispatchEvent(t);
		}

		// Dispatch a "tap" event if mouse button was released in the same position it was pressed in.
		int dx = abs(fStartPoint[fid].x - x);
		int dy = abs(fStartPoint[fid].y - y);
		const int delta = 10;
		if (dx < delta && dy < delta)
		{
			Rtt::TapEvent event(x, y, fid + 1);
			DispatchEvent(event);
		}

		fStartPoint.erase(fid);
	}

	void LinuxMouseListener::DispatchEvent(const MEvent& e) const
	{
		app->GetRuntime()->DispatchEvent(e);
	}

	void LinuxMouseListener::OnEvent(const SDL_Event& evt, SDL_Window* window)
	{
		switch (evt.type)
		{
		case SDL_FINGERDOWN:
		{
			int w, h;
			SDL_GetWindowSize(window, &w, &h);
			const SDL_TouchFingerEvent& ef = evt.tfinger;
			TouchDown(w * ef.x, h * ef.y, ef.fingerId);
			break;
		}
		case SDL_FINGERUP:
		{
			int w, h;
			SDL_GetWindowSize(window, &w, &h);
			const SDL_TouchFingerEvent& ef = evt.tfinger;
			TouchUp(w * ef.x, h * ef.y, ef.fingerId);
			break;
		}
		case SDL_FINGERMOTION:
		{
			int w, h;
			SDL_GetWindowSize(window, &w, &h);
			const SDL_TouchFingerEvent& ef = evt.tfinger;
			TouchMoved(w * ef.x, h * ef.y, ef.fingerId);
			break;
		}

		case SDL_MOUSEBUTTONDOWN:
		{
			const SDL_MouseButtonEvent& b = evt.button;
			if (b.which != SDL_TOUCH_MOUSEID)
			{
				int x = b.x;
				int y = b.y;
				y -= app->GetMenuHeight();

				float scrollWheelDeltaX = 0;
				float scrollWheelDeltaY = 0;
				if (evt.type == SDL_MOUSEBUTTONDOWN) fButtons |= SDL_BUTTON(b.button);
				else fButtons &= ~SDL_BUTTON(b.button);

				// The buttons as of this event (as Windows' MK_ flags), not SDL_GetMouseState: that is the state after
				// everything already pumped, so a quick click's "down" would see the button released.
				bool isPrimaryDown = fButtons & SDL_BUTTON(SDL_BUTTON_LEFT);
				bool isSecondaryDown = fButtons & SDL_BUTTON(SDL_BUTTON_RIGHT);
				bool isMiddleDown = fButtons & SDL_BUTTON(SDL_BUTTON_MIDDLE);

				// Fetch the current state of the "shift", "alt", and "ctrl" keys.
				const Uint8* key = SDL_GetKeyboardState(NULL);
				bool IsAltDown = key[SDL_SCANCODE_LALT] | key[SDL_SCANCODE_RALT];
				bool IsShiftDown = key[SDL_SCANCODE_LSHIFT] | key[SDL_SCANCODE_RSHIFT];
				bool IsControlDown = key[SDL_SCANCODE_LCTRL] | key[SDL_SCANCODE_RCTRL];
				bool IsCommandDown = key[SDL_SCANCODE_LGUI] | key[SDL_SCANCODE_RGUI];

				Rtt::MouseEvent::MouseEventType eventType = Rtt::MouseEvent::kDown;
				Rtt::MouseEvent mouseEvent(eventType, x, y, Rtt_FloatToReal(scrollWheelDeltaX), Rtt_FloatToReal(scrollWheelDeltaY), 0, isPrimaryDown, isSecondaryDown, isMiddleDown, IsShiftDown, IsAltDown, IsControlDown, IsCommandDown);

				DispatchEvent(mouseEvent);
				TouchDown(x, y, 0);
			}
			break;
		}

		case SDL_MOUSEMOTION:
		{
			const SDL_MouseMotionEvent& b = evt.motion;
			if (b.which != SDL_TOUCH_MOUSEID)
			{
				int x = b.x;
				int y = b.y;
				y -= app->GetMenuHeight();

				float scrollWheelDeltaX = 0;
				float scrollWheelDeltaY = 0;
				fButtons = b.state;

				// The buttons as of this event (as Windows' MK_ flags), not SDL_GetMouseState: that is the state after
				// everything already pumped, so a quick click's "down" would see the button released.
				bool isPrimaryDown = fButtons & SDL_BUTTON(SDL_BUTTON_LEFT);
				bool isSecondaryDown = fButtons & SDL_BUTTON(SDL_BUTTON_RIGHT);
				bool isMiddleDown = fButtons & SDL_BUTTON(SDL_BUTTON_MIDDLE);

				// Fetch the current state of the "shift", "alt", and "ctrl" keys.
				const Uint8* key = SDL_GetKeyboardState(NULL);
				bool IsAltDown = key[SDL_SCANCODE_LALT] | key[SDL_SCANCODE_RALT];
				bool IsShiftDown = key[SDL_SCANCODE_LSHIFT] | key[SDL_SCANCODE_RSHIFT];
				bool IsControlDown = key[SDL_SCANCODE_LCTRL] | key[SDL_SCANCODE_RCTRL];
				bool IsCommandDown = key[SDL_SCANCODE_LGUI] | key[SDL_SCANCODE_RGUI];

				Rtt::MouseEvent::MouseEventType eventType = Rtt::MouseEvent::kMove;

				// Determine if this is a "drag" event.
				if (isPrimaryDown || isSecondaryDown || isMiddleDown)
				{
					eventType = Rtt::MouseEvent::kDrag;
				}

				Rtt::MouseEvent mouseEvent(eventType, x, y, Rtt_FloatToReal(scrollWheelDeltaX), Rtt_FloatToReal(scrollWheelDeltaY), 0,
					isPrimaryDown, isSecondaryDown, isMiddleDown, IsShiftDown, IsAltDown, IsControlDown, IsCommandDown);

#if Rtt_DEBUG_TOUCH
				//			printf("MouseEvent(%d, %d)\n", b.x, b.y);
#endif

				DispatchEvent(mouseEvent);
				TouchMoved(x, y, 0);
			}
			break;
		}

		case SDL_MOUSEBUTTONUP:
		{
			const SDL_MouseButtonEvent& b = evt.button;
			if (b.which != SDL_TOUCH_MOUSEID)
			{
				int x = b.x;
				int y = b.y;
				y -= app->GetMenuHeight();

				float scrollWheelDeltaX = 0;
				float scrollWheelDeltaY = 0;
				if (evt.type == SDL_MOUSEBUTTONDOWN) fButtons |= SDL_BUTTON(b.button);
				else fButtons &= ~SDL_BUTTON(b.button);

				// The buttons as of this event (as Windows' MK_ flags), not SDL_GetMouseState: that is the state after
				// everything already pumped, so a quick click's "down" would see the button released.
				bool isPrimaryDown = fButtons & SDL_BUTTON(SDL_BUTTON_LEFT);
				bool isSecondaryDown = fButtons & SDL_BUTTON(SDL_BUTTON_RIGHT);
				bool isMiddleDown = fButtons & SDL_BUTTON(SDL_BUTTON_MIDDLE);

				// Fetch the current state of the "shift", "alt", and "ctrl" keys.
				const Uint8* key = SDL_GetKeyboardState(NULL);
				bool IsAltDown = key[SDL_SCANCODE_LALT] | key[SDL_SCANCODE_RALT];
				bool IsShiftDown = key[SDL_SCANCODE_LSHIFT] | key[SDL_SCANCODE_RSHIFT];
				bool IsControlDown = key[SDL_SCANCODE_LCTRL] | key[SDL_SCANCODE_RCTRL];
				bool IsCommandDown = key[SDL_SCANCODE_LGUI] | key[SDL_SCANCODE_RGUI];

				Rtt::MouseEvent::MouseEventType eventType = Rtt::MouseEvent::kUp;
				Rtt::MouseEvent mouseEvent(eventType, x, y, Rtt_FloatToReal(scrollWheelDeltaX), Rtt_FloatToReal(scrollWheelDeltaY), 0, isPrimaryDown, isSecondaryDown, isMiddleDown, IsShiftDown, IsAltDown, IsControlDown, IsCommandDown);

				DispatchEvent(mouseEvent);
				TouchUp(x, y, 0);
			}
			break;
		}

		case SDL_MOUSEWHEEL:
		{
			const SDL_MouseWheelEvent& w = evt.wheel;
			if (w.which != SDL_TOUCH_MOUSEID)
			{
				// As the Windows Simulator: deltas in content units, wheel down / right positive, one notch =
				// 3 lines (or 3 characters) of the default font size. SDL gives notches, wheel up positive.
				// (This used to pass the raw notches, sign flipped against Windows and macOS, and the deltas as
				// the position.)
#if SDL_VERSION_ATLEAST(2, 0, 18)
				const float notchesX = w.preciseX, notchesY = w.preciseY;   // fractional on smooth touchpads
#else
				const float notchesX = (float)w.x, notchesY = (float)w.y;
#endif
				Runtime* runtime = app->GetRuntime();
				const float lineSize = 3.0f * Rtt_RealToFloat(runtime->Platform().GetStandardFontSize());
				float scrollWheelDeltaX = notchesX * lineSize * Rtt_RealToFloat(runtime->GetDisplay().GetSxUpright());
				float scrollWheelDeltaY = -notchesY * lineSize * Rtt_RealToFloat(runtime->GetDisplay().GetSyUpright());
				int x = 0, y = 0;
#if SDL_VERSION_ATLEAST(2, 26, 0)
				x = w.mouseX;
				y = w.mouseY;
#else
				SDL_GetMouseState(&x, &y);
#endif
				y -= app->GetMenuHeight();

				// The buttons as of this event (as Windows' MK_ flags), not SDL_GetMouseState: that is the state after
				// everything already pumped, so a quick click's "down" would see the button released.
				bool isPrimaryDown = fButtons & SDL_BUTTON(SDL_BUTTON_LEFT);
				bool isSecondaryDown = fButtons & SDL_BUTTON(SDL_BUTTON_RIGHT);
				bool isMiddleDown = fButtons & SDL_BUTTON(SDL_BUTTON_MIDDLE);

				// Fetch the current state of the "shift", "alt", and "ctrl" keys.
				const Uint8* key = SDL_GetKeyboardState(NULL);
				bool IsAltDown = key[SDL_SCANCODE_LALT] | key[SDL_SCANCODE_RALT];
				bool IsShiftDown = key[SDL_SCANCODE_LSHIFT] | key[SDL_SCANCODE_RSHIFT];
				bool IsControlDown = key[SDL_SCANCODE_LCTRL] | key[SDL_SCANCODE_RCTRL];
				bool IsCommandDown = key[SDL_SCANCODE_LGUI] | key[SDL_SCANCODE_RGUI];

				Rtt::MouseEvent::MouseEventType eventType = Rtt::MouseEvent::kScroll;
				Rtt::MouseEvent mouseEvent(eventType, x, y, Rtt_FloatToReal(scrollWheelDeltaX), Rtt_FloatToReal(scrollWheelDeltaY), 0,
					isPrimaryDown, isSecondaryDown, isMiddleDown, IsShiftDown, IsAltDown, IsControlDown, IsCommandDown);

#if Rtt_DEBUG_TOUCH
				//			printf("MouseEvent(%d, %d)\n", b.x, b.y);
#endif

				DispatchEvent(mouseEvent);
			}
			break;
		}
		}		// switch
	}

}
