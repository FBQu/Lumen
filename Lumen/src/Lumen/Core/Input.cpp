#include "Lumen/Core/Input.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace Lumen {

	namespace {

		struct NameEntry
		{
			Key Value;
			const char* Name;
		};

		// Canonical names (lowercase); a few aliases are accepted by KeyFromName.
		constexpr NameEntry s_Names[] = {
			{ Key::A, "a" }, { Key::B, "b" }, { Key::C, "c" }, { Key::D, "d" }, { Key::E, "e" }, { Key::F, "f" }, { Key::G, "g" },
			{ Key::H, "h" }, { Key::I, "i" }, { Key::J, "j" }, { Key::K, "k" }, { Key::L, "l" }, { Key::M, "m" }, { Key::N, "n" },
			{ Key::O, "o" }, { Key::P, "p" }, { Key::Q, "q" }, { Key::R, "r" }, { Key::S, "s" }, { Key::T, "t" }, { Key::U, "u" },
			{ Key::V, "v" }, { Key::W, "w" }, { Key::X, "x" }, { Key::Y, "y" }, { Key::Z, "z" },
			{ Key::Num0, "0" }, { Key::Num1, "1" }, { Key::Num2, "2" }, { Key::Num3, "3" }, { Key::Num4, "4" },
			{ Key::Num5, "5" }, { Key::Num6, "6" }, { Key::Num7, "7" }, { Key::Num8, "8" }, { Key::Num9, "9" },
			{ Key::Space, "space" }, { Key::Enter, "enter" }, { Key::Escape, "escape" }, { Key::Tab, "tab" },
			{ Key::Backspace, "backspace" }, { Key::Delete, "delete" },
			{ Key::Left, "left" }, { Key::Right, "right" }, { Key::Up, "up" }, { Key::Down, "down" },
			{ Key::LeftShift, "leftshift" }, { Key::RightShift, "rightshift" }, { Key::LeftControl, "leftcontrol" },
			{ Key::RightControl, "rightcontrol" }, { Key::LeftAlt, "leftalt" }, { Key::RightAlt, "rightalt" },
			{ Key::F1, "f1" }, { Key::F2, "f2" }, { Key::F3, "f3" }, { Key::F4, "f4" }, { Key::F5, "f5" }, { Key::F6, "f6" },
			{ Key::F7, "f7" }, { Key::F8, "f8" }, { Key::F9, "f9" }, { Key::F10, "f10" }, { Key::F11, "f11" }, { Key::F12, "f12" },
		};

		constexpr NameEntry s_Aliases[] = {
			{ Key::Enter, "return" }, { Key::Escape, "esc" }, { Key::LeftShift, "shift" }, { Key::LeftControl, "ctrl" },
			{ Key::LeftControl, "control" }, { Key::LeftAlt, "alt" }, { Key::Space, " " },
		};

		bool Valid(Key key) { return key > Key::Unknown && key < Key::Count; }
		size_t Index(Key key) { return static_cast<size_t>(key); }

	}

	void InputState::BeginFrame()
	{
		m_Pressed.reset();
		m_Released.reset();
		m_MousePressed.reset();
		m_MouseReleased.reset();
		m_MouseDelta = { 0.0f, 0.0f };
		m_Scroll = 0.0f;
	}

	void InputState::SetKey(Key key, bool down)
	{
		if (!Valid(key) || m_Down[Index(key)] == down)
			return; // ignore unknown keys and key repeat
		m_Down[Index(key)] = down;
		(down ? m_Pressed : m_Released)[Index(key)] = true;
	}

	void InputState::SetMouseButton(MouseButton button, bool down)
	{
		const size_t i = static_cast<size_t>(button);
		if (i >= 3 || m_MouseDown[i] == down)
			return;
		m_MouseDown[i] = down;
		(down ? m_MousePressed : m_MouseReleased)[i] = true;
	}

	void InputState::SetMousePosition(const glm::vec2& position)
	{
		if (m_HasMousePosition)
			m_MouseDelta += position - m_MousePosition; // the first event only establishes the position
		m_MousePosition = position;
		m_HasMousePosition = true;
	}

	void InputState::AddScroll(float amount) { m_Scroll += amount; }

	void InputState::ReleaseAll()
	{
		for (size_t i = 1; i < s_KeyCount; i++)
			if (m_Down[i])
			{
				m_Down[i] = false;
				m_Released[i] = true;
			}
		for (size_t i = 0; i < 3; i++)
			if (m_MouseDown[i])
			{
				m_MouseDown[i] = false;
				m_MouseReleased[i] = true;
			}
	}

	bool InputState::IsKeyDown(Key key) const { return Valid(key) && m_Down[Index(key)]; }
	bool InputState::WasKeyPressed(Key key) const { return Valid(key) && m_Pressed[Index(key)]; }
	bool InputState::WasKeyReleased(Key key) const { return Valid(key) && m_Released[Index(key)]; }
	bool InputState::IsMouseButtonDown(MouseButton button) const { return static_cast<size_t>(button) < 3 && m_MouseDown[static_cast<size_t>(button)]; }
	bool InputState::WasMouseButtonPressed(MouseButton button) const { return static_cast<size_t>(button) < 3 && m_MousePressed[static_cast<size_t>(button)]; }
	bool InputState::WasMouseButtonReleased(MouseButton button) const { return static_cast<size_t>(button) < 3 && m_MouseReleased[static_cast<size_t>(button)]; }

	std::optional<Key> InputState::KeyFromName(std::string_view name)
	{
		std::string lower(name);
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		for (const NameEntry& entry : s_Names)
			if (lower == entry.Name)
				return entry.Value;
		for (const NameEntry& entry : s_Aliases)
			if (lower == entry.Name)
				return entry.Value;
		return std::nullopt;
	}

	std::optional<MouseButton> InputState::MouseButtonFromName(std::string_view name)
	{
		std::string lower(name);
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (lower == "left") return MouseButton::Left;
		if (lower == "right") return MouseButton::Right;
		if (lower == "middle") return MouseButton::Middle;
		return std::nullopt;
	}

	const char* InputState::KeyName(Key key)
	{
		for (const NameEntry& entry : s_Names)
			if (entry.Value == key)
				return entry.Name;
		return "unknown";
	}

}
