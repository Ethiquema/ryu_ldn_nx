# ==========================================
# Memory Trace - Crash Detection
# ==========================================
# Detect and diagnose memory-related crashes

echo [MEMORY] Loading crash detection...\n

# Segmentation faults
catch signal SIGSEGV
commands
    silent
    echo \n
    echo ==========================================\n
    echo SEGMENTATION FAULT DETECTED\n
    echo ==========================================\n
    echo \n
    
    # Register state
    echo [CRASH] Register state:\n
    info registers
    echo \n
    
    # Backtrace
    echo [CRASH] Call stack:\n
    backtrace full
    echo \n
    
    # Faulting instruction
    echo [CRASH] Faulting instruction:\n
    x/10i $pc-20
    echo \n
    
    # Memory around PC
    echo [CRASH] Memory around PC:\n
    x/32xb $pc-16
    echo \n
    
    # Stack dump
    echo [CRASH] Stack dump (64 bytes):\n
    x/64xb $sp
    echo \n
    
    # Local variables
    echo [CRASH] Local variables:\n
    info locals
    echo \n
    
    # Arguments
    echo [CRASH] Function arguments:\n
    info args
    echo \n
    
    echo ==========================================\n
    echo END CRASH REPORT\n
    echo ==========================================\n
    echo \n
    
    # Don't continue - let user examine
end

# Bus errors
catch signal SIGBUS
commands
    silent
    echo \n
    echo ==========================================\n
    echo BUS ERROR DETECTED\n
    echo ==========================================\n
    backtrace full
    info registers
    echo ==========================================\n
    # Don't continue
end

# Illegal instructions
catch signal SIGILL
commands
    silent
    echo \n
    echo ==========================================\n
    echo ILLEGAL INSTRUCTION DETECTED\n
    echo ==========================================\n
    backtrace full
    info registers
    x/10i $pc
    echo ==========================================\n
    # Don't continue
end

# Floating point exceptions
catch signal SIGFPE
commands
    silent
    echo \n
    echo ==========================================\n
    echo FLOATING POINT EXCEPTION DETECTED\n
    echo ==========================================\n
    backtrace full
    info registers
    echo ==========================================\n
    # Don't continue
end

# Abort signals
catch signal SIGABRT
commands
    silent
    echo \n
    echo ==========================================\n
    echo ABORT SIGNAL RECEIVED\n
    echo ==========================================\n
    backtrace full
    info registers
    echo ==========================================\n
    # Don't continue
end

echo [MEMORY] Crash detection: 5 signal handlers\n
