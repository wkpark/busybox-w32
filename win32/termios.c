#include "libbb.h"

int tcsetattr(int fd UNUSED_PARAM, int mode UNUSED_PARAM,  const struct termios *t UNUSED_PARAM)
{
	return -1;
}

int tcgetattr(int fd UNUSED_PARAM, struct termios *t UNUSED_PARAM)
{
	return -1;
}

int64_t FAST_FUNC read_key(int fd, char *buf, int timeout)
{
	HANDLE cin = GetStdHandle(STD_INPUT_HANDLE);
	INPUT_RECORD record;
	DWORD nevent_out;
	int ret = -1;

	if (fd != 0)
		bb_error_msg_and_die("read_key only works on stdin");
	if (cin == INVALID_HANDLE_VALUE)
		return -1;

	if (timeout > 0) {
		if (WaitForSingleObject(cin, timeout) != WAIT_OBJECT_0)
			goto done;
	}
	while (1) {
		if (!ReadConsoleInput(cin, &record, 1, &nevent_out))
			goto done;
		if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown)
			continue;
		if (!record.Event.KeyEvent.uChar.AsciiChar) {
			DWORD state = record.Event.KeyEvent.dwControlKeyState;

			if (state & (RIGHT_CTRL_PRESSED|LEFT_CTRL_PRESSED) &&
			    (record.Event.KeyEvent.wVirtualKeyCode >= 'A' &&
			     record.Event.KeyEvent.wVirtualKeyCode <= 'Z'))
				return record.Event.KeyEvent.wVirtualKeyCode & ~0x40;

			switch (record.Event.KeyEvent.wVirtualKeyCode) {
			case VK_DELETE: return KEYCODE_DELETE;
			case VK_INSERT: return KEYCODE_INSERT;
			case VK_UP: return KEYCODE_UP;
			case VK_DOWN: return KEYCODE_DOWN;
			case VK_RIGHT:
				if (state & (RIGHT_CTRL_PRESSED|LEFT_CTRL_PRESSED))
					return KEYCODE_CTRL_RIGHT;
				return KEYCODE_RIGHT;
			case VK_LEFT:
				if (state & (RIGHT_CTRL_PRESSED|LEFT_CTRL_PRESSED))
					return KEYCODE_CTRL_LEFT;
				return KEYCODE_LEFT;
			case VK_HOME: return KEYCODE_HOME;
			case VK_END: return KEYCODE_END;
			case VK_PRIOR: return KEYCODE_PAGEUP;
			case VK_NEXT: return KEYCODE_PAGEDOWN;
			case VK_CAPITAL:
			case VK_SHIFT:
			case VK_CONTROL:
			case VK_MENU:
				break;
			}
			continue;
		}
		ret = record.Event.KeyEvent.uChar.AsciiChar;
		break;
	}
 done:
	return ret;
}
