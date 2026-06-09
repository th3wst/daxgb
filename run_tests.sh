#!/bin/bash

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

echo "==================================="
echo "  daxgb Automated Test Suite       "
echo "==================================="

# Rebuild the emulator to ensure latest code in being tested
make clean > /dev/null
make > /dev/null
if [ $? -ne 0 ]; then
    echo -e "${RED}Build failed. Aborting tests.${NC}"
    exit 1
fi

passed=0
failed=0

# Iterate over every ROM in the tests directory
for rom in tests/*.gb; do
    echo -n "Testing $(basename "$rom")... "
    
    # Run the emulator in test mode, throw standard output in the trash
    ./daxgb_emulator -t "$rom" > /dev/null 2>&1
    
    # Capture the exit code (0 = Passed, 1 = Failed/Crash)
    if [ $? -eq 0 ]; then
        echo -e "[${GREEN}PASS${NC}]"
        ((passed++))
    else
        echo -e "[${RED}FAIL${NC}]"
        ((failed++))
    fi
done

echo "==================================="
echo "Results: $passed Passed | $failed Failed"
echo "==================================="