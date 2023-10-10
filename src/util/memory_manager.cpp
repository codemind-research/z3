
/*++
Copyright (c) 2015 Microsoft Corporation

--*/

#include<iostream>
#include<stdlib.h>
#include<climits>
#include "util/mutex.h"
#include "util/trace.h"
#include "util/memory_manager.h"
#include "util/error_codes.h"
#include "util/debug.h"
#include "util/scoped_timer.h"
#ifdef __GLIBC__
# include <malloc.h>
# define HAS_MALLOC_USABLE_SIZE
#elif defined(__APPLE__)
# include <malloc/malloc.h>
# define HAS_MALLOC_USABLE_SIZE
# define malloc_usable_size malloc_size
#elif defined(__FreeBSD__)
# include <malloc_np.h>
# define HAS_MALLOC_USABLE_SIZE
#elif defined(_WINDOWS)
# include <malloc.h>
# define HAS_MALLOC_USABLE_SIZE
# define malloc_usable_size _msize
#endif

// The following two function are automatically generated by the mk_make.py script.
// The script collects ADD_INITIALIZER and ADD_FINALIZER commands in the .h files.
// For example, rational.h contains
//      ADD_INITIALIZER('rational::initialize();')
//      ADD_FINALIZER('rational::finalize();')
// Thus, any executable or shared object (DLL) that depends on rational.h
// will have an automatically generated file mem_initializer.cpp containing
//    mem_initialize() 
//    mem_finalize()
// and these functions will include the statements:
//    rational::initialize();
//    
//    rational::finalize();
void mem_initialize();
void mem_finalize();

// If PROFILE_MEMORY is defined, Z3 will display the amount of memory used, and the number of synchronization steps during finalization
// #define PROFILE_MEMORY

out_of_memory_error::out_of_memory_error():z3_error(ERR_MEMOUT) {
}


static DECLARE_INIT_MUTEX(g_memory_mux);
static atomic<bool> g_memory_out_of_memory(false);
static bool       g_memory_initialized       = false;
static long long  g_memory_alloc_size        = 0;
static long long  g_memory_max_size          = 0;
static long long  g_memory_max_used_size     = 0;
static long long  g_memory_watermark         = 0;
static long long  g_memory_alloc_count       = 0;
static long long  g_memory_max_alloc_count   = 0;
static bool       g_exit_when_out_of_memory  = false;
static char const * g_out_of_memory_msg      = "ERROR: out of memory";

void memory::exit_when_out_of_memory(bool flag, char const * msg) {
    g_exit_when_out_of_memory = flag;
    if (flag && msg)
        g_out_of_memory_msg = msg;
}

static void throw_out_of_memory() {
    g_memory_out_of_memory = true;

    if (g_exit_when_out_of_memory) {
        std::cerr << g_out_of_memory_msg << "\n";
        exit(ERR_MEMOUT);
    }
    else {
        throw out_of_memory_error();
    }
}

static void throw_alloc_counts_exceeded() {
    std::cout << "Maximal allocation counts " << g_memory_max_alloc_count << " have been exceeded\n";
    exit(ERR_ALLOC_EXCEEDED);
}


#ifdef PROFILE_MEMORY
static unsigned g_synch_counter = 0;
class mem_usage_report {
public:
    ~mem_usage_report() { 
        std::cerr << "(memory :max " << g_memory_max_used_size 
                  << " :allocs " << g_memory_alloc_count
                  << " :final " << g_memory_alloc_size 
                  << " :synch " << g_synch_counter << ")" << std::endl; 
    }
};
mem_usage_report g_info;
#endif

void memory::initialize(size_t max_size) {
    static mutex init_mux;
    lock_guard lock(init_mux);

    // only update the maximum size if max_size != UINT_MAX
    if (max_size != UINT_MAX)
        g_memory_max_size = max_size;

    if (g_memory_initialized)
        return;

    g_memory_out_of_memory = false;
    mem_initialize();
    g_memory_initialized = true;
}

bool memory::is_out_of_memory() {
    return g_memory_out_of_memory;
}

void memory::set_high_watermark(size_t watermark) {
    // This method is only safe to invoke at initialization time, that is, before the threads are created.
    g_memory_watermark = watermark;
}

bool memory::above_high_watermark() {
    if (g_memory_watermark == 0)
        return false;
    lock_guard lock(*g_memory_mux);
    return g_memory_watermark < g_memory_alloc_size;
}

// The following methods are only safe to invoke at 
// initialization time, that is, before threads are created.

void memory::set_max_size(size_t max_size) {
    g_memory_max_size = max_size;
}

void memory::set_max_alloc_count(size_t max_count) {
    g_memory_max_alloc_count = max_count;
}

static bool g_finalizing = false;

void memory::finalize(bool shutdown) {
    if (g_memory_initialized) {
        g_finalizing = true;
        mem_finalize();
        // we leak the mutex since we need it to be always live since memory may
        // be reinitialized again
        //delete g_memory_mux;
        g_memory_initialized = false;
        g_finalizing = false;

        if (shutdown) {
            scoped_timer::finalize();
        }
    }
}

unsigned long long memory::get_allocation_size() {
    long long r;
    {
        lock_guard lock(*g_memory_mux);
        r = g_memory_alloc_size;
    }
    if (r < 0)
        r = 0;
    return r;
}

unsigned long long memory::get_max_used_memory() {
    unsigned long long r;
    {
        lock_guard lock(*g_memory_mux);
        r = g_memory_max_used_size;
    }
    return r;
}

#if defined(_WINDOWS)
#include <windows.h>
#endif

unsigned long long memory::get_max_memory_size() {
#if defined(_WINDOWS)    
    MEMORYSTATUSEX statex;    
    statex.dwLength = sizeof (statex);    
    GlobalMemoryStatusEx (&statex);
    return statex.ullTotalPhys;
#else
    // 16 GB
    return 1ull << 34ull;
#endif
}

unsigned long long memory::get_allocation_count() {
    return g_memory_alloc_count;
}

unsigned long long memory::get_configured_max_size() {
    return g_memory_max_size;
}


void memory::display_max_usage(std::ostream & os) {
    unsigned long long mem = get_max_used_memory();
    os << "max. heap size:     " 
       << static_cast<double>(mem)/static_cast<double>(1024*1024) 
       << " Mbytes\n";
}

void memory::display_i_max_usage(std::ostream & os) {
    unsigned long long mem = get_max_used_memory();
    std::cout << "MEMORY " 
              << static_cast<double>(mem)/static_cast<double>(1024*1024) 
              << "\n";
}

#if Z3DEBUG
void memory::deallocate(char const * file, int line, void * p) {
    deallocate(p);
    TRACE_CODE(if (!g_finalizing) TRACE("memory", tout << "dealloc " << std::hex << p << std::dec << " " << file << ":" << line << "\n";););
}

void * memory::allocate(char const* file, int line, char const* obj, size_t s) {
    void * r = allocate(s);
    TRACE("memory", tout << "alloc " << std::hex << r << std::dec << " " << file << ":" << line << " " << obj << " " << s << "\n";);
    return r;
}
#endif

#ifndef SINGLE_THREAD
// ==================================
// ==================================
// THREAD LOCAL VERSION
// ==================================
// ==================================


// We only integrate the local thread counters with the global one
// when the local counter > SYNCH_THRESHOLD 
#define SYNCH_THRESHOLD 100000

thread_local long long g_memory_thread_alloc_size    = 0;
thread_local long long g_memory_thread_alloc_count   = 0;

static void synchronize_counters(bool allocating) {
#ifdef PROFILE_MEMORY
    g_synch_counter++;
#endif

    bool out_of_mem = false;
    bool counts_exceeded = false;
    {
        lock_guard lock(*g_memory_mux);
        g_memory_alloc_size += g_memory_thread_alloc_size;
        g_memory_alloc_count += g_memory_thread_alloc_count;
        if (g_memory_alloc_size > g_memory_max_used_size)
            g_memory_max_used_size = g_memory_alloc_size;
        if (g_memory_max_size != 0 && g_memory_alloc_size > g_memory_max_size)
            out_of_mem = true;
        if (g_memory_max_alloc_count != 0 && g_memory_alloc_count > g_memory_max_alloc_count)
            counts_exceeded = true;
    }
    g_memory_thread_alloc_size = 0;
    if (out_of_mem && allocating) {
        throw_out_of_memory();
    }
    if (counts_exceeded && allocating) {
        throw_alloc_counts_exceeded();
    }
}

void memory::deallocate(void * p) {
#ifdef HAS_MALLOC_USABLE_SIZE
    size_t sz      = malloc_usable_size(p);
    void * real_p  = p;
#else
    size_t * sz_p  = reinterpret_cast<size_t*>(p) - 1;
    size_t sz      = *sz_p;
    void * real_p  = reinterpret_cast<void*>(sz_p);
#endif
    g_memory_thread_alloc_size -= sz;
    free(real_p);
    if (g_memory_thread_alloc_size < -SYNCH_THRESHOLD) {
        synchronize_counters(false);
    }
}

void * memory::allocate(size_t s) {
#ifndef HAS_MALLOC_USABLE_SIZE
    s = s + sizeof(size_t); // we allocate an extra field!
#endif
    g_memory_thread_alloc_size += s;
    g_memory_thread_alloc_count += 1;
    if (g_memory_thread_alloc_size > SYNCH_THRESHOLD) {
        synchronize_counters(true);
    }
    void * r = malloc(s);
    if (r == nullptr) {
        throw_out_of_memory();
        return nullptr;
    }
#ifdef HAS_MALLOC_USABLE_SIZE
    g_memory_thread_alloc_size += malloc_usable_size(r) - s;
    return r;
#else
    *(static_cast<size_t*>(r)) = s;
    return static_cast<size_t*>(r) + 1; // we return a pointer to the location after the extra field
#endif
}

void* memory::reallocate(void *p, size_t s) {
#ifdef HAS_MALLOC_USABLE_SIZE
    size_t sz      = malloc_usable_size(p);
    void * real_p  = p;
    // We may be lucky and malloc gave us enough space
    if (sz >= s)
        return p;
#else
    size_t *sz_p = reinterpret_cast<size_t*>(p)-1;
    size_t sz = *sz_p;
    void *real_p = reinterpret_cast<void*>(sz_p);
    s = s + sizeof(size_t); // we allocate an extra field!
#endif
    g_memory_thread_alloc_size += s - sz;
    g_memory_thread_alloc_count += 1;
    if (g_memory_thread_alloc_size > SYNCH_THRESHOLD) {
        synchronize_counters(true);
    }

    void *r = realloc(real_p, s);
    if (r == nullptr) {
        throw_out_of_memory();
        return nullptr;
    }
#ifdef HAS_MALLOC_USABLE_SIZE
    g_memory_thread_alloc_size += malloc_usable_size(r) - s;
    return r;
#else
    *(static_cast<size_t*>(r)) = s;
    return static_cast<size_t*>(r) + 1; // we return a pointer to the location after the extra field
#endif
}

#else
// ==================================
// ==================================
// SINGLE-THREAD MODE
// ==================================
// ==================================
// allocate & deallocate without locking

void memory::deallocate(void * p) {
#ifdef HAS_MALLOC_USABLE_SIZE
    size_t sz      = malloc_usable_size(p);
    void * real_p  = p;
#else
    size_t * sz_p  = reinterpret_cast<size_t*>(p) - 1;
    size_t sz      = *sz_p;
    void * real_p  = reinterpret_cast<void*>(sz_p);
#endif
    g_memory_alloc_size -= sz;
    free(real_p);
}

void * memory::allocate(size_t s) {
#ifndef HAS_MALLOC_USABLE_SIZE
    s = s + sizeof(size_t); // we allocate an extra field!
#endif
    g_memory_alloc_size += s;
    g_memory_alloc_count += 1;
    if (g_memory_alloc_size > g_memory_max_used_size)
        g_memory_max_used_size = g_memory_alloc_size;
    if (g_memory_max_size != 0 && g_memory_alloc_size > g_memory_max_size)
        throw_out_of_memory();
    if (g_memory_max_alloc_count != 0 && g_memory_alloc_count > g_memory_max_alloc_count)
        throw_alloc_counts_exceeded();

    void * r = malloc(s);
    if (r == nullptr) {
        throw_out_of_memory();
        return nullptr;
    }
#ifdef HAS_MALLOC_USABLE_SIZE
    g_memory_alloc_size += malloc_usable_size(r) - s;
    return r;
#else
    *(static_cast<size_t*>(r)) = s;
    return static_cast<size_t*>(r) + 1; // we return a pointer to the location after the extra field
#endif
}

void* memory::reallocate(void *p, size_t s) {
#ifdef HAS_MALLOC_USABLE_SIZE
    size_t sz      = malloc_usable_size(p);
    void * real_p  = p;
    // We may be lucky and malloc gave us enough space
    if (sz >= s)
        return p;
#else
    size_t * sz_p  = reinterpret_cast<size_t*>(p) - 1;
    size_t sz      = *sz_p;
    void * real_p  = reinterpret_cast<void*>(sz_p);
    s = s + sizeof(size_t); // we allocate an extra field!
#endif
    g_memory_alloc_size += s - sz;
    g_memory_alloc_count += 1;
    if (g_memory_alloc_size > g_memory_max_used_size)
        g_memory_max_used_size = g_memory_alloc_size;
    if (g_memory_max_size != 0 && g_memory_alloc_size > g_memory_max_size)
        throw_out_of_memory();
    if (g_memory_max_alloc_count != 0 && g_memory_alloc_count > g_memory_max_alloc_count)
        throw_alloc_counts_exceeded();

    void *r = realloc(real_p, s);
    if (r == nullptr) {
        throw_out_of_memory();
        return nullptr;
    }
#ifdef HAS_MALLOC_USABLE_SIZE
    g_memory_alloc_size += malloc_usable_size(r) - s;
    return r;
#else
    *(static_cast<size_t*>(r)) = s;
    return static_cast<size_t*>(r) + 1; // we return a pointer to the location after the extra field
#endif
}
 
#endif
