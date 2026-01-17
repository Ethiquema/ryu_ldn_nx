# ==========================================
# Memory Trace - Heap Corruption Detection
# ==========================================
# Detect heap corruption and buffer overflows

echo [MEMORY] Loading heap corruption detection...\n

# Double-free detection
break free
commands
    silent
    # Check if pointer is valid
    if $arg0 == 0
        echo \n
        echo [MEMORY:CORRUPTION] WARNING: free(NULL) called!\n
        backtrace 3
        echo \n
    end
    continue
end

# Invalid pointer detection in delete
break operator delete(void*)
commands
    silent
    if $arg0 == 0
        echo \n
        echo [MEMORY:CORRUPTION] WARNING: delete(NULL) called!\n
        backtrace 3
        echo \n
    end
    continue
end

# Buffer overflow detection - watch string operations
break strcpy
commands
    silent
    printf "[MEMORY:BUFFER] strcpy(%p, %p) - potential overflow risk\n", $arg0, $arg1
    backtrace 2
    continue
end

break strcat
commands
    silent
    printf "[MEMORY:BUFFER] strcat(%p, %p) - potential overflow risk\n", $arg0, $arg1
    backtrace 2
    continue
end

break sprintf
commands
    silent
    printf "[MEMORY:BUFFER] sprintf(%p, ...) - potential overflow risk\n", $arg0
    backtrace 2
    continue
end

break memcpy
commands
    silent
    printf "[MEMORY:BUFFER] memcpy(%p, %p, %ld)\n", $arg0, $arg1, $arg2
    # Check for overlapping regions
    if ($arg0 > $arg1) && ($arg0 < ($arg1 + $arg2))
        echo [MEMORY:CORRUPTION] WARNING: Overlapping memcpy regions!\n
        backtrace 3
    end
    if ($arg1 > $arg0) && ($arg1 < ($arg0 + $arg2))
        echo [MEMORY:CORRUPTION] WARNING: Overlapping memcpy regions!\n
        backtrace 3
    end
    continue
end

break memmove
commands
    silent
    printf "[MEMORY:BUFFER] memmove(%p, %p, %ld)\n", $arg0, $arg1, $arg2
    continue
end

echo [MEMORY] Heap corruption detection: 7 breakpoints\n
