# Lua expression queries

`luaquery <single-line expression>` evaluates `return (<expression>)` through
loadstring + pcall on the game main thread. Example: `luaquery UnitHealth('target')`.
Returns `{"ok":true,"type":"number","value":123}`. Supported types: nil
(JSON null), boolean, finite number, string. Tables/functions are rejected;
serialize tables explicitly in the expression if needed. Only the first value
is returned. Syntax and evaluation failures return `{"ok":false,"error":"..."}`.

The unique per-request temporary global is read with the existing guarded Lua
5.0 stack reader and cleared immediately. No unverified GetText call is used.
Strings above 16000 bytes or containing NUL are explicitly rejected. JSON control
characters and quotes are escaped. Input retains the pipe's 1023 UTF-8 byte limit.
This is an execution interface like lua, not a sandbox: expressions can call
functions with side effects, and unbounded expressions can block the game thread.
Timeouts do not imply cancellation or authorize automatic retries.

The existing `lua` command remains a one-way execution command.
