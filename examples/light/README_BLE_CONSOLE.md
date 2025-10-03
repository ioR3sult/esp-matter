# BLE Console for ESP-Matter Light Example

## Overview

The BLE Console is an **optional, security-focused debug feature** that provides wireless shell access to the ESP-Matter light example via Bluetooth Low Energy. It implements a Nordic UART Service (NUS)-style interface with mandatory encryption and bonding.

**⚠️ IMPORTANT: This feature is DISABLED by default and should ONLY be enabled during development/debugging. Never enable in production builds.**

## Security Model

The BLE console enforces strict security requirements:

- **Encryption Required**: All console communication requires an encrypted BLE link
- **Bonding Required (default)**: Device must be bonded with MITM protection before console access
- **Passkey Authentication**: 6-digit passkey displayed on UART must be entered during pairing
- **Commissioning Protection**: Console automatically stops when Matter commissioning window opens
- **Rate Limiting (default)**: TX traffic throttled to ~200 notifications/sec to avoid starving Matter stack

## When Console is Available

The BLE console follows strict availability rules to never interfere with Matter commissioning:

| Device State | Console Advertising | Console Connectivity |
|-------------|-------------------|---------------------|
| Before commissioning | ❌ OFF | ❌ Not available |
| During commissioning | ❌ OFF | ❌ Not available |
| After commissioning (window closed) | ✅ ON | ✅ Available |
| Commissioning window re-opened | ❌ OFF | ❌ Disconnects active sessions |
| After factory reset | ❌ OFF | ❌ Not available until re-commissioned |

## Building with BLE Console

### Option 1: Using sdkconfig.debug (Recommended)

```bash
cd ~/repos/esp-matter/examples/light
rm -rf build managed_components sdkconfig
idf.py -DSDKCONFIG=sdkconfig.debug set-target esp32c3
idf.py -DSDKCONFIG=sdkconfig.debug build flash monitor
```

### Option 2: Manual menuconfig

```bash
cd ~/repos/esp-matter/examples/light
idf.py set-target esp32c3
idf.py menuconfig
```

Navigate to: `Debugging > Enable BLE shell debugging (encrypted)` and enable it.

Additional options:
- `Require bonded peer`: ON by default (recommended)
- `Enable TX rate limiter`: ON by default (recommended)

```bash
idf.py build flash monitor
```

### Building WITHOUT BLE Console (Default)

```bash
cd ~/repos/esp-matter/examples/light
rm -rf build managed_components sdkconfig
idf.py set-target esp32c3
idf.py build flash monitor
```

The default `sdkconfig.defaults` has the feature disabled, resulting in zero code overhead.

## Connecting to BLE Console

### Prerequisites

- ESP32-C3 board with BLE console enabled firmware
- BLE scanner app: [nRF Connect](https://www.nordicsemi.com/Products/Development-tools/nrf-connect-for-mobile) (recommended), LightBlue, or similar
- Device must be commissioned first (console won't advertise until commissioning completes)

### Connection Procedure

1. **Commission your device** using Matter controller (chip-tool, Google Home, Apple Home, etc.)
   
2. **Wait for commissioning to complete** - console will start advertising automatically

3. **Open BLE scanner app** and scan for devices

4. **Look for device named**: `LIGHT-DBG-XXXX` where XXXX is the last 2 bytes of MAC address
   - Example: `LIGHT-DBG-A4B2`

5. **Connect** to the device

6. **Pair when prompted**:
   - Pairing request will appear
   - Check UART console output for passkey
   - Example UART output: `=== Passkey: 123456 ===`
   - Enter the 6-digit passkey in your BLE app

7. **Find Nordic UART Service**:
   - Service UUID: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
   - RX Characteristic (Write): `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
   - TX Characteristic (Notify): `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`

8. **Enable Notifications** on TX characteristic

9. **Send commands** by writing to RX characteristic (include `\n` at end)

### First Test

Write `help\n` to RX characteristic. You should receive:

```
Commands:
  help            - show this help
  status          - show console status
  logs on|off     - mirror ESP_LOG* to BLE
  bond strict on|off - require bonded peer (default ON)
  rate on|off     - enable/disable TX rate limiter (default ON)
OK
```

## Available Commands

### Built-in Console Commands

| Command | Description |
|---------|-------------|
| `help` | Show available commands |
| `status` | Show console status (logs, bond requirement, rate limiting) |
| `logs on` | Enable ESP_LOG* mirroring to BLE |
| `logs off` | Disable ESP_LOG* mirroring (default) |
| `bond strict on` | Require bonded peer (default, recommended) |
| `bond strict off` | Allow encrypted but unbonded peers (not recommended) |
| `rate on` | Enable TX rate limiting (default, recommended) |
| `rate off` | Disable TX rate limiting (may impact Matter performance) |

### ESP Console Commands

All standard ESP console commands are also available when Matter firmware has them enabled:

- `restart` - Restart the system
- `free` - Get free heap memory
- `version` - Get firmware version
- Matter-specific commands (if enabled in build)

### Example Session

```
> help
Commands:
  help            - show this help
  status          - show console status
  logs on|off     - mirror ESP_LOG* to BLE
  bond strict on|off - require bonded peer (default ON)
  rate on|off     - enable/disable TX rate limiter (default ON)
OK

> status
status: logs=off, require_bond=on, rate_limit=on

> logs on
OK: logs ON

(now all ESP_LOGI, ESP_LOGW, etc. appear in BLE console)

> logs off
OK: logs OFF

> restart
OK
(device restarts)
```

## Troubleshooting

### Console Not Advertising

**Possible causes:**
- Device not commissioned yet - console only starts after commissioning completes
- Commissioning window is open - console stops during commissioning
- Device was factory reset - needs to be re-commissioned

**Solution:** Complete Matter commissioning first, then console will automatically advertise.

### Cannot Pair / Pairing Fails

**Possible causes:**
- Passkey entered incorrectly
- Passkey not visible in UART logs
- BLE app doesn't support numeric passkey entry

**Solution:**
1. Check UART output for: `=== Passkey: XXXXXX ===`
2. Enter the exact 6-digit number in your BLE app
3. Try nRF Connect if your current app doesn't work well

### Console Commands Don't Work

**Possible causes:**
- Notifications not enabled on TX characteristic
- Device not bonded (if `bond strict on`)
- Commands not terminated with `\n`

**Solution:**
1. Verify notifications are enabled
2. Check `status` command works
3. Ensure commands end with newline
4. Try `bond strict off` temporarily (not recommended for production)

### Console Disconnects Unexpectedly

**Possible causes:**
- Commissioning window was opened (by button press or controller command)
- Factory reset was triggered
- Connection timeout

**Expected behavior:** Console automatically disconnects when commissioning window opens to ensure Matter operations aren't affected.

### Cannot Reconnect After Disconnect

**Possible causes:**
- Bonding information lost
- Device still in commissioning mode

**Solution:**
1. Wait for device to exit commissioning mode
2. Forget device in BLE app and re-pair
3. Check device logs for commissioning state

### Logs Not Appearing Over BLE

**Cause:** Log mirroring is OFF by default for safety.

**Solution:** Send `logs on` command first. Logs will then appear over BLE while connected.

## Performance Considerations

### Rate Limiting

The default rate limiter allows ~200 notifications/second in bursts (20 tokens per 100ms window). This prevents the BLE console from starving the Matter stack and NimBLE host.

**When to disable:** Only disable rate limiting (`rate off`) if:
- You need to capture high-throughput logs temporarily
- Matter stack is idle (not commissioning or processing commands)
- You're actively monitoring for performance impact

**Impact of disabling:** May cause Matter operations to slow down or fail if console is sending large amounts of data.

### Log Mirroring

Logs are OFF by default (`logs off`) because:
- Reduces BLE bandwidth usage
- Prevents log flooding during Matter operations
- Improves battery life (if applicable)
- Avoids interference with timing-sensitive Matter protocols

**When to enable:** Enable logs (`logs on`) only when actively debugging.

### Memory Usage

The BLE console adds approximately:
- **Flash**: ~15KB code + data
- **RAM**: ~8KB (buffers + tasks)
- **NimBLE**: Uses existing NimBLE stack from Matter (no additional RAM)

When feature is disabled (`CONFIG_BLE_SHELL_DEBUGGING_ENCRYPTED=n`):
- **Flash**: 0 bytes (compiled out completely)
- **RAM**: 0 bytes

## Security Best Practices

### DO ✅

- Keep `bond strict on` (default)
- Keep `rate on` (default)
- Use strong passkeys (automatically generated)
- Commission device on isolated network first
- Monitor UART logs during pairing
- Disable feature in production builds
- Use console only on development networks

### DON'T ❌

- Don't enable in production firmware
- Don't disable bonding requirement unless necessary
- Don't share passkeys
- Don't leave logs mirroring on permanently
- Don't disable rate limiting without monitoring
- Don't use console during critical Matter operations
- Don't connect over BLE during commissioning

## Architecture

### Component Structure

```
components/debug_console/
├── CMakeLists.txt          # Component registration
├── debug_console.h         # Public API
├── debug_console.c         # GATT service, advertising, security
└── console_bridge.c        # RX/TX bridge, commands, log mirroring
```

### Key Design Points

1. **No BLE Re-initialization**: Reuses NimBLE host/controller from Matter stack
2. **Event-Driven Control**: Uses Matter event system to control advertising
3. **Non-Blocking**: All I/O uses FreeRTOS queues and stream buffers
4. **Zero Overhead When Disabled**: Completely compiled out if feature flag is OFF
5. **Defense in Depth**: Encryption/bonding checked in both GATT flags and runtime

### Matter Event Integration

The console hooks into these Matter events:

- `kCommissioningComplete` → Start advertising
- `kCommissioningWindowClosed` → Start advertising  
- `kCommissioningWindowOpened` → Stop advertising + disconnect
- `kFailSafeTimerArmed` → Stop advertising + disconnect
- `kFabricRemoved` → Stop advertising (factory reset)

## Configuration Reference

### Kconfig Options

```kconfig
CONFIG_BLE_SHELL_DEBUGGING_ENCRYPTED     # Master enable (default: OFF)
CONFIG_BLE_CONSOLE_REQUIRE_BOND          # Require bonding (default: ON)
CONFIG_BLE_CONSOLE_RATE_LIMIT            # Enable rate limiter (default: ON)
```

### NimBLE Configuration (sdkconfig.debug)

```kconfig
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1       # One console connection
CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=185   # Efficient payload size
CONFIG_BT_NIMBLE_MSYS1_BLOCK_COUNT=96    # Adequate buffering
CONFIG_BT_NIMBLE_SM_SC=y                 # Secure Connections
CONFIG_BT_NIMBLE_SM_MITM=y               # Man-in-the-Middle protection
CONFIG_BT_NIMBLE_SM_BONDING=y            # Enable bonding
```

### Service UUIDs

```
Service:     6E400001-B5A3-F393-E0A9-E50E24DCCA9E (Nordic UART Service)
RX (Write):  6E400002-B5A3-F393-E0A9-E50E24DCCA9E
TX (Notify): 6E400003-B5A3-F393-E0A9-E50E24DCCA9E
```

## Testing

See [TESTPLAN.md](TESTPLAN.md) for comprehensive test procedures.

Quick smoke test:
1. Build with feature ON
2. Commission device
3. Connect via BLE, pair, enable notifications
4. Send `help\n` → verify response
5. Send `status\n` → verify settings
6. Send `logs on\n` → verify ESP logs appear
7. Open commissioning window → verify disconnect
8. Close commissioning window → verify can reconnect

## Known Limitations

1. **Single Connection**: Only one BLE console connection at a time
2. **MTU Dependent**: Long outputs may be truncated or chunked
3. **No Command History**: No readline-style command history/editing
4. **No Flow Control**: Sender should wait for response before sending next command
5. **ESP Console Integration**: Only commands enabled in Matter firmware are available
6. **Commissioning Priority**: Console always yields to Matter commissioning

## FAQ

**Q: Can I use the console while commissioning?**  
A: No, the console automatically stops to ensure commissioning isn't affected.

**Q: Will this affect Matter performance?**  
A: Not when properly configured with default settings (bonding + rate limiting ON).

**Q: Can I connect without pairing?**  
A: No, encryption and bonding are mandatory for security.

**Q: How do I get the passkey?**  
A: The passkey appears in the UART console output during pairing.

**Q: Can I use this in production?**  
A: No, this is a development/debugging feature only.

**Q: What happens if I factory reset?**  
A: Console stops advertising until the device is re-commissioned.

**Q: Can I change the device name?**  
A: The name includes MAC address suffix for uniqueness. Base name can be changed in `CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME`.

**Q: Does this drain more power?**  
A: Yes, BLE advertising and connections consume power. Disable in production.

## References

- [ESP-Matter Documentation](https://docs.espressif.com/projects/esp-matter/)
- [NimBLE Documentation](https://mynewt.apache.org/latest/network/index.html)
- [Nordic UART Service Specification](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/libraries/bluetooth_services/services/nus.html)
- [Matter Specification](https://csa-iot.org/developer-resource/specifications-download-request/)

## License

This implementation is part of the ESP-Matter project and follows the same Apache 2.0 license.
