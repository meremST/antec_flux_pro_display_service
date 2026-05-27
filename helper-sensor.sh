#!/usr/bin/env bash

# ANSI colors
BOLD="\033[1m"
CYAN="\033[1;36m"
GREEN="\033[32m"
YELLOW="\033[33m"
RED="\033[31m"
DIM="\033[2m"
RESET="\033[0m"

temp_color() {
    local val=$1
    if   awk "BEGIN {exit !($val >= 80)}"; then echo -n "$RED"
    elif awk "BEGIN {exit !($val >= 60)}"; then echo -n "$YELLOW"
    else echo -n "$GREEN"
    fi
}

echo
printf "${BOLD}  ══════════════════════════════════════${RESET}\n"
printf "${BOLD}        HWMON SENSOR DISCOVERY TOOL     ${RESET}\n"
printf "${BOLD}  ══════════════════════════════════════${RESET}\n"

for hw in /sys/class/hwmon/hwmon*; do
    [ -d "$hw" ] || continue

    chip=$(cat "$hw/name" 2>/dev/null)

    found=0
    for temp in "$hw"/temp*_input; do
        [ -f "$temp" ] || continue
        found=1
        break
    done
    [ "$found" -eq 0 ] && continue

    echo
    printf "  ${CYAN}● %s${RESET}\n" "$chip"

    for temp in "$hw"/temp*_input; do
        [ -f "$temp" ] || continue

        base="${temp%_input}"
        label_file="${base}_label"

        label="(no label)"
        name=""
        if [ -f "$label_file" ]; then
            label=$(cat "$label_file")
            name="$label"
        fi

        value=$(cat "$temp" 2>/dev/null)
        if [ -z "$value" ]; then
            printf "    ${DIM}%-20s (unavailable)${RESET}\n" "$label"
            continue
        fi

        value_c=$(awk "BEGIN {printf \"%.1f\", $value/1000}")
        color=$(temp_color "$value_c")

        printf "    ${BOLD}%-20s${RESET} ${color}%s°C${RESET}\n" "$label" "$value_c"
        printf "    ${DIM}sensor = %s${RESET}\n"  "$chip"
        printf "    ${DIM}name   = %s${RESET}\n"  "$name"
    done
done

echo
printf "${BOLD}  ══════════════════════════════════════${RESET}\n"
echo
