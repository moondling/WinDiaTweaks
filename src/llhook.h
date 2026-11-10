#pragma once
// llhook.h - low-level input hooks (WH_MOUSE_LL, WH_KEYBOARD_LL) on a thread of their own.
//
// Windows calls a low-level hook on the thread that set it, and holds every mouse move and key press on
// the whole system until that thread answers (up to LowLevelHooksTimeout). Set on Dia's UI thread, which
// is often busy (WinUI layout, a page being made, our own ticks), they make the pointer and typing stall
// everywhere (zioder/WinDiaTweaks#39). This thread does nothing but wait for hook calls, so it answers at
// once.
//
// Hook procs run on this thread: they must not touch Dia's UI (XAML) or state only the UI thread may use,
// nor take a lock the UI thread can hold for long. Read atomics, post messages to the UI thread.

#include <windows.h>

class LowLevelHookThread {
public:
    LowLevelHookThread() = default;
    LowLevelHookThread(const LowLevelHookThread&) = delete;
    LowLevelHookThread& operator=(const LowLevelHookThread&) = delete;

    // Sets the hooks (mouse or keys may be null) on a new thread. True once they're set.
    bool Start(HOOKPROC mouse, HOOKPROC keys) {
        if (thread_) return true;
        mouse_ = mouse;
        keys_ = keys;
        ok_ = false;
        HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ready) return false;
        ready_ = ready;
        thread_ = CreateThread(nullptr, 0, &LowLevelHookThread::Main, this, 0, &id_);
        if (!thread_) {
            CloseHandle(ready);
            ready_ = nullptr;
            return false;
        }
        WaitForSingleObject(ready, INFINITE);  // (setting a hook doesn't block)
        CloseHandle(ready);
        ready_ = nullptr;
        if (!ok_) Stop();
        return ok_;
    }

    // Takes the hooks off and ends the thread.
    void Stop() {
        if (!thread_) return;
        PostThreadMessageW(id_, WM_QUIT, 0, 0);
        WaitForSingleObject(thread_, 2000);
        CloseHandle(thread_);
        thread_ = nullptr;
        id_ = 0;
    }

    bool Running() const { return thread_ != nullptr; }
    DWORD Error() const { return error_; }

private:
    static DWORD WINAPI Main(LPVOID arg) {
        auto* self = static_cast<LowLevelHookThread*>(arg);
        MSG msg;
        PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);  // (the queue, before Stop can post to it)
        // Its own module (the procs live in it), held while the hooks are set.
        HMODULE self_mod = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)&LowLevelHookThread::Main, &self_mod);
        HHOOK mouse = self->mouse_ ? SetWindowsHookExW(WH_MOUSE_LL, self->mouse_, self_mod, 0) : nullptr;
        HHOOK keys = self->keys_ ? SetWindowsHookExW(WH_KEYBOARD_LL, self->keys_, self_mod, 0) : nullptr;
        self->ok_ = (!self->mouse_ || mouse) && (!self->keys_ || keys);
        self->error_ = self->ok_ ? 0 : GetLastError();
        SetEvent(self->ready_);
        if (self->ok_)
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);
        if (mouse) UnhookWindowsHookEx(mouse);
        if (keys) UnhookWindowsHookEx(keys);
        return 0;
    }

    HOOKPROC mouse_ = nullptr, keys_ = nullptr;
    HANDLE thread_ = nullptr, ready_ = nullptr;
    DWORD id_ = 0, error_ = 0;
    volatile bool ok_ = false;
};
