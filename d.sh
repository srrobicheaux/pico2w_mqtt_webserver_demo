#!/bin/bash

# The serial port your Pico is connected to
PORT="/dev/ttyACM0"

echo "Connecting to $PORT... Press [CTRL+C] to stop."

while true; do
    if [ -c "$PORT" ]; then
        # Read the port. This stays open until the device disconnects.
        cat "$PORT"
    fi
    
    # Wait 1 second before attempting to reconnect/restart
    sleep 1
    echo "--- Reconnecting to beginning of stream ---"
done
