#include "Starfall/Platform/Input.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace Starfall {

	namespace {

		struct InputState
		{
			std::array<bool, MaxKeyCode + 1> KeyDown{};
			std::array<bool, MaxKeyCode + 1> KeyPressed{};
			std::array<bool, MaxKeyCode + 1> KeyReleased{};
			std::array<bool, 3> MouseDown{};
			std::array<bool, 3> MousePressed{};
			std::array<bool, 3> MouseReleased{};
			glm::vec2 MousePosition{ 0.0f };
			glm::vec2 MouseDelta{ 0.0f };
			glm::vec2 Scroll{ 0.0f };
			bool HasMousePosition = false;
			bool Enabled = true;
		};

		InputState s_State;

		bool ValidKey(KeyCode key) { return static_cast<uint16_t>(key) <= MaxKeyCode; }

		struct KeyName
		{
			const char* Name;
			KeyCode Key;
		};

		constexpr KeyName s_KeyNames[] = {
			{ "Space", KeyCode::Space }, { "Escape", KeyCode::Escape }, { "Enter", KeyCode::Enter }, { "Tab", KeyCode::Tab },
			{ "Backspace", KeyCode::Backspace }, { "Insert", KeyCode::Insert }, { "Delete", KeyCode::Delete },
			{ "Right", KeyCode::Right }, { "Left", KeyCode::Left }, { "Down", KeyCode::Down }, { "Up", KeyCode::Up },
			{ "PageUp", KeyCode::PageUp }, { "PageDown", KeyCode::PageDown }, { "Home", KeyCode::Home }, { "End", KeyCode::End },
			{ "LeftShift", KeyCode::LeftShift }, { "LeftControl", KeyCode::LeftControl }, { "LeftAlt", KeyCode::LeftAlt },
			{ "RightShift", KeyCode::RightShift }, { "RightControl", KeyCode::RightControl }, { "RightAlt", KeyCode::RightAlt },
			{ "Comma", KeyCode::Comma }, { "Period", KeyCode::Period }, { "Minus", KeyCode::Minus }, { "Equal", KeyCode::Equal },
			{ "Slash", KeyCode::Slash }, { "Semicolon", KeyCode::Semicolon }, { "Apostrophe", KeyCode::Apostrophe },
			{ "LeftBracket", KeyCode::LeftBracket }, { "RightBracket", KeyCode::RightBracket }, { "Backslash", KeyCode::Backslash },
			{ "GraveAccent", KeyCode::GraveAccent }, { "CapsLock", KeyCode::CapsLock },
		};

		bool EqualsIgnoreCase(std::string_view a, std::string_view b)
		{
			if(a.size() != b.size())
				return false;
			for(size_t i = 0; i < a.size(); i++)
				if(std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
					return false;
			return true;
		}

	}

	KeyCode KeyCodeFromString(std::string_view name)
	{
		if(name.size() == 1)
		{
			char c = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
			if((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
				return static_cast<KeyCode>(c);
		}
		if((name.size() == 2 || name.size() == 3) && (name[0] == 'F' || name[0] == 'f'))
		{
			int n = 0;
			for(size_t i = 1; i < name.size(); i++)
			{
				if(!std::isdigit(static_cast<unsigned char>(name[i])))
				{
					n = -1;
					break;
				}
				n = n * 10 + (name[i] - '0');
			}
			if(n >= 1 && n <= 12)
				return static_cast<KeyCode>(static_cast<int>(KeyCode::F1) + n - 1);
		}
		for(const KeyName& entry : s_KeyNames)
			if(EqualsIgnoreCase(name, entry.Name))
				return entry.Key;
		return KeyCode::Unknown;
	}

	const char* KeyCodeToString(KeyCode key)
	{
		for(const KeyName& entry : s_KeyNames)
			if(entry.Key == key)
				return entry.Name;
		static thread_local char buffer[4];
		uint16_t value = static_cast<uint16_t>(key);
		if((value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9'))
		{
			buffer[0] = static_cast<char>(value);
			buffer[1] = 0;
			return buffer;
		}
		if(value >= static_cast<uint16_t>(KeyCode::F1) && value <= static_cast<uint16_t>(KeyCode::F12))
		{
			int n = value - static_cast<uint16_t>(KeyCode::F1) + 1;
			if(n < 10)
			{
				buffer[0] = 'F';
				buffer[1] = static_cast<char>('0' + n);
				buffer[2] = 0;
			}
			else
			{
				buffer[0] = 'F';
				buffer[1] = static_cast<char>('0' + n / 10);
				buffer[2] = static_cast<char>('0' + n % 10);
				buffer[3] = 0;
			}
			return buffer;
		}
		return "Unknown";
	}

	bool Input::IsKeyDown(KeyCode key) { return s_State.Enabled && ValidKey(key) && s_State.KeyDown[static_cast<size_t>(key)]; }
	bool Input::IsKeyPressed(KeyCode key) { return s_State.Enabled && ValidKey(key) && s_State.KeyPressed[static_cast<size_t>(key)]; }
	bool Input::IsKeyReleased(KeyCode key) { return s_State.Enabled && ValidKey(key) && s_State.KeyReleased[static_cast<size_t>(key)]; }

	bool Input::IsMouseButtonDown(MouseButton b) { return s_State.Enabled && static_cast<size_t>(b) < 3 && s_State.MouseDown[static_cast<size_t>(b)]; }
	bool Input::IsMouseButtonPressed(MouseButton b) { return s_State.Enabled && static_cast<size_t>(b) < 3 && s_State.MousePressed[static_cast<size_t>(b)]; }
	bool Input::IsMouseButtonReleased(MouseButton b) { return s_State.Enabled && static_cast<size_t>(b) < 3 && s_State.MouseReleased[static_cast<size_t>(b)]; }

	glm::vec2 Input::GetMousePosition() { return s_State.MousePosition; }
	glm::vec2 Input::GetMouseDelta() { return s_State.Enabled ? s_State.MouseDelta : glm::vec2(0.0f); }
	glm::vec2 Input::GetScrollDelta() { return s_State.Enabled ? s_State.Scroll : glm::vec2(0.0f); }

	void Input::SetEnabled(bool enabled) { s_State.Enabled = enabled; }
	bool Input::IsEnabled() { return s_State.Enabled; }

	void Input::BeginFrame()
	{
		s_State.KeyPressed.fill(false);
		s_State.KeyReleased.fill(false);
		s_State.MousePressed.fill(false);
		s_State.MouseReleased.fill(false);
		s_State.MouseDelta = glm::vec2(0.0f);
		s_State.Scroll = glm::vec2(0.0f);
	}

	void Input::Reset()
	{
		bool enabled = s_State.Enabled;
		s_State = InputState{};
		s_State.Enabled = enabled;
	}

	void Input::OnKey(KeyCode key, bool down)
	{
		if(!ValidKey(key))
			return;
		size_t i = static_cast<size_t>(key);
		if(down && !s_State.KeyDown[i])
			s_State.KeyPressed[i] = true;
		if(!down && s_State.KeyDown[i])
			s_State.KeyReleased[i] = true;
		s_State.KeyDown[i] = down;
	}

	void Input::OnMouseButton(MouseButton button, bool down)
	{
		size_t i = static_cast<size_t>(button);
		if(i >= 3)
			return;
		if(down && !s_State.MouseDown[i])
			s_State.MousePressed[i] = true;
		if(!down && s_State.MouseDown[i])
			s_State.MouseReleased[i] = true;
		s_State.MouseDown[i] = down;
	}

	void Input::OnMouseMoved(float x, float y)
	{
		glm::vec2 position(x, y);
		if(s_State.HasMousePosition)
			s_State.MouseDelta += position - s_State.MousePosition;
		s_State.MousePosition = position;
		s_State.HasMousePosition = true;
	}

	void Input::OnScroll(float dx, float dy)
	{
		s_State.Scroll += glm::vec2(dx, dy);
	}

}
