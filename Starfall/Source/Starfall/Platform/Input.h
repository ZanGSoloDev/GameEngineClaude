#pragma once

#include "Starfall/Platform/KeyCodes.h"

#include <glm/glm.hpp>

namespace Starfall {

	// Polled input state. The platform layer feeds it via the On* functions (tests can inject events the same way).
	// Call BeginFrame() once per frame *before* pumping window events and use Pressed/Released queries during the frame.
	class Input
	{
	public:
		static bool IsKeyDown(KeyCode key);
		static bool IsKeyPressed(KeyCode key);   // went down this frame
		static bool IsKeyReleased(KeyCode key);  // went up this frame
		static bool IsMouseButtonDown(MouseButton button);
		static bool IsMouseButtonPressed(MouseButton button);
		static bool IsMouseButtonReleased(MouseButton button);
		static glm::vec2 GetMousePosition();
		static glm::vec2 GetMouseDelta();
		static glm::vec2 GetScrollDelta();

		// When disabled, all queries return "nothing pressed" (the editor disables game input while the viewport is not focused).
		static void SetEnabled(bool enabled);
		static bool IsEnabled();

		static void BeginFrame();
		static void Reset();

		static void OnKey(KeyCode key, bool down);
		static void OnMouseButton(MouseButton button, bool down);
		static void OnMouseMoved(float x, float y);
		static void OnScroll(float dx, float dy);
	};

}
