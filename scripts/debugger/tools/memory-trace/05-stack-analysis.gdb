# ==========================================
# Memory Trace - Stack Analysis
# ==========================================
# Monitor stack usage and detect overflow

echo [MEMORY] Loading stack analysis...\n

# Define helper commands for stack analysis

define stack-dump
    echo \n
    echo ==========================================\n
    echo STACK DUMP\n
    echo ==========================================\n
    echo \n
    
    echo [STACK] Stack pointer: \n
    print /x $sp
    echo \n
    
    echo [STACK] Frame pointer: \n
    print /x $x29
    echo \n
    
    echo [STACK] Link register: \n
    print /x $x30
    echo \n
    
    echo [STACK] Stack dump (256 bytes):\n
    x/64xg $sp
    echo \n
    
    echo [STACK] Backtrace:\n
    backtrace full
    echo \n
    
    echo ==========================================\n
end

document stack-dump
Dump complete stack information including registers and memory
end

define stack-check
    # Check if SP is aligned
    set $sp_aligned = ($sp & 0xf)
    if $sp_aligned != 0
        echo \n
        echo [STACK:WARNING] Stack pointer not 16-byte aligned!\n
        printf "SP = 0x%lx (alignment = %d)\n", $sp, $sp_aligned
        echo \n
    end
end

document stack-check
Check stack pointer alignment and validity
end

# Recursion detection
define recursion-check
    echo [STACK] Checking for recursion...\n
    backtrace
end

document recursion-check
Display backtrace to check for recursive calls
end

echo [MEMORY] Stack analysis commands loaded\n
echo [MEMORY] Commands available: stack-dump, stack-check, recursion-check\n
