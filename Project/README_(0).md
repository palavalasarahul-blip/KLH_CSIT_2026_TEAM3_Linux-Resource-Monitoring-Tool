# Linux Resource Monitoring Tool

A GTK-based Linux desktop application that displays live system resource usage.

## Features
- CPU usage monitoring
- RAM and swap usage monitoring
- Disk usage monitoring
- Total RAM and CPU core information
- System uptime and running process count
- Top CPU and memory-consuming processes
- CPU and memory usage history graphs
- Automatic refresh every 2 seconds

## Technology
- C++
- GTK 3
- Cairo
- Linux `/proc` virtual filesystem
- Linux system utilities (`ps`, `df`, `awk`)

> Note: The provided source uses GTK 3 APIs (for example `gtk_container_add`,
> `gtk_widget_show_all`, and the GTK 3 drawing-area `draw` signal), so build
> against `gtk+-3.0`, not `gtk4`.

## Requirements (Ubuntu / WSL with GUI support)
```bash
sudo apt update
sudo apt install build-essential pkg-config libgtk-3-dev
```

## Build
```bash
make
```

## Run
```bash
./linux-resource-monitor
```

## Build manually
```bash
g++ -std=c++17 main.cpp gui.cpp system_monitor.cpp \
    -o linux-resource-monitor $(pkg-config --cflags --libs gtk+-3.0)
```

## Project files
- `main.cpp` — application entry point
- `gui.cpp` — GTK window, cards, graphs, callbacks, and interface
- `system_monitor.h` — shared structures and function declarations
- `system_monitor.cpp` — CPU, memory, disk, swap, uptime, and process metrics
- `Makefile` — build instructions
