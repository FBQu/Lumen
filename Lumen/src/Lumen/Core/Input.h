#pragma once

#include <glm/glm.hpp>

#include <bitset>
#include <cstdint>
#include <optional>
#include <string_view>

namespace Lumen {

	enum class Key : uint16_t
	{
		Unknown = 0,
		A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
		Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
		Space, Enter, Escape, Tab, Backspace, Delete,
		Left, Right, Up, Down,
		LeftShift, RightShift, LeftControl, RightControl, LeftAlt, RightAlt,
		F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
		Count
	};

	enum class MouseButton : uint8_t { Left, Right, Middle, Count };

	// Keyboard and mouse state for one window, independent of the windowing library. The window feeds it from its event
	// callbacks; scripts and the editor read it. Edge queries ("pressed this frame") are valid until the next BeginFrame().
	class InputState
	{
	public:
		// Clears the per-frame edges, the mouse delta and the scroll amount. Call once per frame before polling events.
		void BeginFrame();

		void SetKey(Key key, bool down);
		void SetMouseButton(MouseButton button, bool down);
		void SetMousePosition(const glm::vec2& position);
		void AddScroll(float amount);
		void ReleaseAll(); // e.g. when the window loses focus, so keys do not stay stuck

		bool IsKeyDown(Key key) const;
		bool WasKeyPressed(Key key) const;
		bool WasKeyReleased(Key key) const;
		bool IsMouseButtonDown(MouseButton button) const;
		bool WasMouseButtonPressed(MouseButton button) const;
		bool WasMouseButtonReleased(MouseButton button) const;
		const glm::vec2& GetMousePosition() const { return m_MousePosition; }
		const glm::vec2& GetMouseDelta() const { return m_MouseDelta; }
		float GetScroll() const { return m_Scroll; }

		// Case-insensitive names: "W", "space", "left", "leftshift", "f5", "0"... (see Input.cpp).
		static std::optional<Key> KeyFromName(std::string_view name);
		static std::optional<MouseButton> MouseButtonFromName(std::string_view name); // "left", "right", "middle"
		static const char* KeyName(Key key);
	private:
		static constexpr size_t s_KeyCount = static_cast<size_t>(Key::Count);

		std::bitset<s_KeyCount> m_Down, m_Pressed, m_Released;
		std::bitset<3> m_MouseDown, m_MousePressed, m_MouseReleased;
		glm::vec2 m_MousePosition = { 0.0f, 0.0f };
		glm::vec2 m_MouseDelta = { 0.0f, 0.0f };
		float m_Scroll = 0.0f;
		bool m_HasMousePosition = false;
	};

}
