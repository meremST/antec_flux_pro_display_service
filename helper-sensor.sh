#!/usr/bin/env bash

echo "======================================"
echo "      HWMON SENSOR DISCOVERY TOOL"
echo "======================================"
echo

for hw in /sys/class/hwmon/hwmon*; do
    [ -d "$hw" ] || continue

    name=$(cat "$hw/name" 2>/dev/null)

    echo "--------------------------------------"
    echo "Device: $hw"
    echo "Name  : $name"
    echo "--------------------------------------"

    for temp in "$hw"/temp*_input; do
        [ -f "$temp" ] || continue

        base="${temp%_input}"
        label_file="${base}_label"

        label="unknown"
        if [ -f "$label_file" ]; then
            label=$(cat "$label_file")
        fi

        value=$(cat "$temp")
        value_c=$(awk "BEGIN {printf \"%.2f\", $value/1000}")

        echo "  Label : $label"
        echo "  Path  : $temp"
        echo "  Temp  : ${value_c}°C"
        echo
    done
done

echo "======================================"
echo "Done"
echo "======================================"
