# gipWebsocket
gipWebsocket is an official GlistEngine plugin for real-time WebSocket communication with built-in JSON parsing, time-interval recording, and local caching support.

This plugin should be cloned under `~/dev/glist/glistplugins/gipWebsocket`.

## Features
- Real-time WebSocket connection (ws:// and wss://)
- Built-in JSON value extraction (getJsonValue)
- Configurable time interval throttling (setInterval)
- Automatic local JSON file caching (setSaveFilePath)

## Usage
Add `gipWebsocket` to the `PLUGINS` list in your GlistApp's `CMakeLists.txt`:
```cmake
set(PLUGINS
    gipWebsocket
)