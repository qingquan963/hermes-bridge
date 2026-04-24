# Hermes Bridge V4 Bug Fix Result

## Fix Applied

**File**: `C:\lobster\hermes_bridge\src\HttpServer.cpp`

**Change**: Moved 413 check to execute AFTER body reception but BEFORE JSON validation.

### Code Flow (After Fix)

```
1. Content-Type check (is_json) → 400 if not JSON
2. [EARLY CHECK] content_length > MAX_BODY_SIZE → 413 (prevents buffer overflow)
3. std::string body; receive body
4. body.size() > MAX_BODY_SIZE → 413   ← NEW: 413 before JSON validation
5. validateJson() → 400 if invalid
```

### Key Implementation

The 413 check is performed TWICE:
1. **Before body reception** (line ~224): Based on Content-Length header. Prevents buffer overflow.
2. **After body reception** (line ~243): Based on actual body.size(). Ensures 413 is returned before JSON validation.

Both checks must pass before JSON validation runs.

## Verification Results

| Test | Body | Expected | Actual | Status |
|------|------|----------|--------|--------|
| V4 70KB | 70,000 bytes | 413 Payload Too Large | HTTP/1.1 413 Payload Too Large | ✅ |
| V1 JSON | 61 bytes | 200 OK | HTTP/1.1 200 OK | ✅ |
| Invalid JSON | "not json at all" | 400 Bad Request | HTTP/1.1 400 Bad Request | ✅ |

## Build Info

- **Exe**: `C:\lobster\hermes_bridge\Release\hermes_bridge.exe`
- **Size**: 419,328 bytes
- **Timestamp**: 2026/4/23 12:57:29
- **Build**: `cmd /c do_build.bat` (MSBuild)

## Root Cause

The original bug: V4 (70KB body) returned 400 (Invalid JSON) instead of 413 (Payload Too Large). The JSON validation was running before the body size check, so invalid JSON in an oversized body triggered 400 instead of 413.

The early content_length check (before body reception) is necessary to prevent buffer overflow when Content-Length exceeds RECV_BUF_SIZE.
