---
name: sprintf-static-buffer-aliasing
description: Never pass a sprintf_static() result into debug_printf/LogBoot — they format into the same static buffer, clobbering the argument.
metadata:
  type: project
---

`sprintf_static()` returns C_General's `static char Buffer[256]` (`common_c.c`), and
`debug_printf` → `debug_vprintf` → `svprintf_static` formats **into that same buffer**.
So passing a `sprintf_static` result as a `%s` argument to anything that itself formats
via `debug_printf` (e.g. `avp::LogBoot`) means `vsnprintf` overwrites its own argument
mid-format.

**Why it matters:** it fails as corrupt output, not a crash. Observed 2026-07-20 on the
plug fleet: the BOOT row's extra field came out empty *and* the entire row was duplicated
— the clobbered `%s` read back the buffer's new contents, whose leading `\n` (from
LogBoot's format) flushed one row early and shipped the copied prefix as a second.

**How to apply:** format into a local buffer and pass that.
`char extra[24]; snprintf(extra, sizeof extra, "rssi=%d", rssi); avp::LogBoot(FW, REV, extra);`

**Fixed in the library since C_General `547339c`** (a project only gets it by bumping):
`debug_vprintf` stages into a per-call stack buffer (`DEBUG_PRINTF_BUFFER_SIZE`, default 256),
so `debug_*` can no longer clobber a `sprintf_static` result at all; and in C++
`sprintf_static` returns `avp::StaticStr` (`StaticStr.hpp`), a move-only claim that makes
nesting or double-use assert instead of corrupt. Converts implicitly to `const char *`, so
call sites are unchanged — except where a **class** parameter is expected (Arduino `String`,
printf-family varargs), which needs an explicit `.c_str()`.
