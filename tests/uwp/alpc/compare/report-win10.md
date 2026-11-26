# Windows 10 versus Windows 11 (golden files)

Windows 10 build as reported by the guest; Windows 11 is the host.

| probe | result | what it checks |
|---|---|---|
| kbd-msg-probe | same as Windows 11 | posted key messages: TranslateMessage/WM_CHAR with Shift/Caps/Ctrl state, SetKeyboardState, hook order, layout queries |

