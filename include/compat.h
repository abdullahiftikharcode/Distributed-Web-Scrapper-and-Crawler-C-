/**
 * Compatibility layer for older MinGW versions
 * that lack full C++11 support
 */

#ifndef COMPAT_H
#define COMPAT_H

// Only use the compatibility layer if we're using MinGW and not Visual Studio
#if defined(__MINGW32__) && !defined(_MSC_VER) && !defined(__MINGW64_VERSION_MAJOR)

#include <windows.h>

// Fallback for inet_pton if not available
#ifndef HAVE_INET_PTON
// Note: This is a simple implementation that only supports IPv4
static int inet_pton_fallback(int af, const char* src, void* dst) {
    if (af != AF_INET) {
        return -1;  // Only IPv4 supported
    }
    
    unsigned long result = inet_addr(src);
    
    if (result == INADDR_NONE) {
        return 0;  // Invalid address
    }
    
    memcpy(dst, &result, sizeof(result));
    return 1;  // Success
}

// Use our fallback if not defined
#ifndef inet_pton
#define inet_pton inet_pton_fallback
#endif
#endif  // HAVE_INET_PTON

// Basic mutex implementation for older MinGW
#ifndef _GLIBCXX_HAS_GTHREADS
    #include <mutex>
    
    // Only redefine these if they're not already defined
    #if !defined(_STD_MUTEX_DEFINED) && !defined(_GLIBCXX_MUTEX)
    namespace std {
        class mutex {
        private:
            CRITICAL_SECTION cs;
        public:
            mutex() {
                InitializeCriticalSection(&cs);
            }
            ~mutex() {
                DeleteCriticalSection(&cs);
            }
            void lock() {
                EnterCriticalSection(&cs);
            }
            void unlock() {
                LeaveCriticalSection(&cs);
            }
            
            // Friend declaration for unique_lock to access cs
            friend class condition_variable;
            template<class T> friend class unique_lock;
        };
        
        template <class Mutex>
        class lock_guard {
        private:
            Mutex& m;
        public:
            explicit lock_guard(Mutex& mutex) : m(mutex) {
                m.lock();
            }
            ~lock_guard() {
                m.unlock();
            }
            // Prevent copying
            lock_guard(const lock_guard&) = delete;
            lock_guard& operator=(const lock_guard&) = delete;
        };
        
        template <class Mutex>
        class unique_lock {
        private:
            Mutex* m;
            bool locked;
        public:
            explicit unique_lock(Mutex& mutex) : m(&mutex), locked(true) {
                m->lock();
            }
            ~unique_lock() {
                if (locked) {
                    m->unlock();
                }
            }
            void unlock() {
                if (locked) {
                    m->unlock();
                    locked = false;
                }
            }
            // Added mutex() accessor for condition_variable
            Mutex& mutex() const { return *m; }
            
            // Prevent copying
            unique_lock(const unique_lock&) = delete;
            unique_lock& operator=(const unique_lock&) = delete;
        };
        
        class condition_variable {
        private:
            CONDITION_VARIABLE cv;
        public:
            condition_variable() {
                InitializeConditionVariable(&cv);
            }
            void notify_all() {
                WakeAllConditionVariable(&cv);
            }
            template <class Lock, class Pred>
            void wait_for(Lock& lock, const std::chrono::seconds& rel_time, Pred pred) {
                while (!pred()) {
                    SleepConditionVariableCS(&cv, &(lock.mutex().cs), rel_time.count() * 1000);
                    if (pred()) return;
                }
            }
        };
        
        // Simplified atomic for older compilers
        template <typename T>
        class atomic {
        private:
            T value;
            CRITICAL_SECTION cs;
        public:
            atomic(T initial = T()) : value(initial) {
                InitializeCriticalSection(&cs);
            }
            ~atomic() {
                DeleteCriticalSection(&cs);
            }
            T load() const {
                EnterCriticalSection(&cs);
                T result = value;
                LeaveCriticalSection(&cs);
                return result;
            }
            void store(T desired) {
                EnterCriticalSection(&cs);
                value = desired;
                LeaveCriticalSection(&cs);
            }
            // Increment operator
            T operator++() {
                EnterCriticalSection(&cs);
                T result = ++value;
                LeaveCriticalSection(&cs);
                return result;
            }
            T operator++(int) {
                EnterCriticalSection(&cs);
                T result = value++;
                LeaveCriticalSection(&cs);
                return result;
            }
            // Conversion to T
            operator T() const {
                return load();
            }
        };
        
        // Basic thread implementation
        class thread {
        private:
            HANDLE handle;
            bool joinable_;
            
            struct ThreadData {
                void (*func)();
            };
            
            static DWORD WINAPI ThreadProc(LPVOID lpParameter) {
                ThreadData* data = static_cast<ThreadData*>(lpParameter);
                data->func();
                delete data;
                return 0;
            }
            
        public:
            thread() : handle(NULL), joinable_(false) {}
            
            template <typename Function>
            thread(Function f) : joinable_(true) {
                ThreadData* data = new ThreadData{reinterpret_cast<void(*)()>(f)};
                handle = CreateThread(NULL, 0, ThreadProc, data, 0, NULL);
                if (handle == NULL) {
                    joinable_ = false;
                    delete data;
                    throw std::runtime_error("Failed to create thread");
                }
            }
            
            bool joinable() const {
                return joinable_;
            }
            
            void join() {
                if (joinable_) {
                    WaitForSingleObject(handle, INFINITE);
                    CloseHandle(handle);
                    joinable_ = false;
                }
            }
            
            void detach() {
                if (joinable_) {
                    CloseHandle(handle);
                    joinable_ = false;
                }
            }
            
            ~thread() {
                if (joinable_) {
                    std::terminate(); // Same behavior as standard thread
                }
            }
        };
        
        // Helper for thread
        namespace this_thread {
            inline void sleep_for(const std::chrono::milliseconds& duration) {
                Sleep(duration.count());
            }
        }
    } // namespace std
    #endif // !defined(_STD_MUTEX_DEFINED) && !defined(_GLIBCXX_MUTEX)
#endif // _GLIBCXX_HAS_GTHREADS

#endif // defined(__MINGW32__) && !defined(_MSC_VER) && !defined(__MINGW64_VERSION_MAJOR)

#endif // COMPAT_H 