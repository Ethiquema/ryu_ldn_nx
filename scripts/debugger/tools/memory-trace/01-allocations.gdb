# ==========================================
# Memory Trace - Allocation Tracking
# ==========================================
# Track all memory allocations and deallocations

echo [MEMORY] Loading allocation tracking...\n

# Standard allocators
break malloc
commands
    silent
    printf "[MEMORY:ALLOC] malloc(%ld) called from:\n", $arg0
    backtrace 2
    continue
end

break calloc
commands
    silent
    printf "[MEMORY:ALLOC] calloc(%ld, %ld) called from:\n", $arg0, $arg1
    backtrace 2
    continue
end

break realloc
commands
    silent
    printf "[MEMORY:ALLOC] realloc(%p, %ld) called from:\n", $arg0, $arg1
    backtrace 2
    continue
end

break free
commands
    silent
    printf "[MEMORY:ALLOC] free(%p) called from:\n", $arg0
    backtrace 2
    continue
end

# C++ allocators
break operator new(unsigned long)
commands
    silent
    printf "[MEMORY:ALLOC] new(%ld) called from:\n", $arg0
    backtrace 2
    continue
end

break operator new[](unsigned long)
commands
    silent
    printf "[MEMORY:ALLOC] new[](%ld) called from:\n", $arg0
    backtrace 2
    continue
end

break operator delete(void*)
commands
    silent
    printf "[MEMORY:ALLOC] delete(%p) called from:\n", $arg0
    backtrace 2
    continue
end

break operator delete[](void*)
commands
    silent
    printf "[MEMORY:ALLOC] delete[](%p) called from:\n", $arg0
    backtrace 2
    continue
end

break operator delete(void*, unsigned long)
commands
    silent
    printf "[MEMORY:ALLOC] delete(%p, %ld) called from:\n", $arg0, $arg1
    backtrace 2
    continue
end

break operator delete[](void*, unsigned long)
commands
    silent
    printf "[MEMORY:ALLOC] delete[](%p, %ld) called from:\n", $arg0, $arg1
    backtrace 2
    continue
end

echo [MEMORY] Allocation tracking: 10 breakpoints\n
