# BLE Console Test Plan

## Test Environment

- **Hardware**: ESP32-C3-DevKitM-1-N4X
- **ESP-IDF**: v5.4.1
- **ESP-Matter**: v1.4.2
- **BLE Scanner**: nRF Connect for Mobile
- **Matter Controller**: chip-tool or Google Home

## Test Configurations

### Config A: Feature OFF (Stock Behavior)
```bash
idf.py set-target esp32c3
idf.py build
# CONFIG_BLE_SHELL_DEBUGGING_ENCRYPTED is not set
```

### Config B: Feature ON (BLE Console Enabled)
```bash
idf.py -DSDKCONFIG=sdkconfig.debug set-target esp32c3
idf.py -DSDKCONFIG=sdkconfig.debug build
# CONFIG_BLE_SHELL_DEBUGGING_ENCRYPTED=y
# CONFIG_BLE_CONSOLE_REQUIRE_BOND=y
# CONFIG_BLE_CONSOLE_RATE_LIMIT=y
```

## Critical Test Cases

### TC1: Feature OFF - Stock Behavior Unchanged

**Objective**: Verify that disabling the feature results in zero side effects.

**Procedure**:
1. Build with Config A (feature OFF)
2. Flash and monitor device
3. Commission device using chip-tool
4. Scan for BLE devices

**Expected Results**:
- ✅ Device boots normally
- ✅ Matter commissioning works unchanged
- ✅ No BLE advertising detected (no `LIGHT-DBG-*` device)
- ✅ Binary size same as before feature was added
- ✅ No BLE-related logs appear

**Pass Criteria**: All expected results met.

---

### TC2: Feature ON - No Console Before Commissioning

**Objective**: Verify console does not advertise before device is commissioned.

**Procedure**:
1. Build with Config B (feature ON)
2. Flash device
3. Immediately scan for BLE devices
4. Wait 60 seconds, continue scanning

**Expected Results**:
- ✅ No `LIGHT-DBG-*` device advertising
- ✅ No console-related logs in UART
- ✅ Device in commissioning mode (Matter BLE advertising visible)

**Pass Criteria**: No BLE console advertising before commissioning.

---

### TC3: Feature ON - Console Available After Commissioning

**Objective**: Verify console starts advertising after commissioning completes.

**Procedure**:
1. Continue from TC2
2. Commission device using chip-tool:
   ```bash
   chip-tool pairing ble-wifi 0x7283 <SSID> <PASSWORD> 20202021 3840
   ```
3. Wait for commissioning to complete
4. Scan for BLE devices

**Expected Results**:
- ✅ Commissioning completes successfully
- ✅ UART shows: "Commissioning complete"
- ✅ UART shows: "Commissioning window closed - starting BLE console ADV"
- ✅ `LIGHT-DBG-XXXX` device appears in BLE scan (XXXX = last 2 MAC bytes)
- ✅ Service UUID `6E400001-...` visible

**Pass Criteria**: Console advertising starts within 5 seconds of commissioning completion.

---

### TC4: Pairing and Encryption

**Objective**: Verify passkey-based pairing and encryption work correctly.

**Procedure**:
1. Continue from TC3
2. In nRF Connect, connect to `LIGHT-DBG-XXXX`
3. When pairing prompt appears, check UART output
4. Enter passkey shown in UART (format: `=== Passkey: 123456 ===`)
5. Complete pairing

**Expected Results**:
- ✅ Connection established
- ✅ Pairing request appears in nRF Connect
- ✅ 6-digit passkey visible in UART logs
- ✅ Passkey accepted
- ✅ Device bonded successfully
- ✅ UART shows: "Security: enc=1 bond=1"

**Pass Criteria**: Pairing completes successfully with encryption enabled.

---

### TC5: Basic Console Commands

**Objective**: Verify console commands work over encrypted BLE connection.

**Procedure**:
1. Continue from TC4 (connected and bonded)
2. Enable notifications on TX characteristic (`6E400003-...`)
3. Write `help\n` to RX characteristic (`6E400002-...`)
4. Write `status\n`
5. Write `logs on\n`
6. Write `logs off\n`

**Expected Results**:
- ✅ `help` returns command list
- ✅ `status` returns: `status: logs=off, require_bond=on, rate_limit=on`
- ✅ `logs on` returns: `OK: logs ON`
- ✅ ESP_LOG* output appears in BLE after `logs on`
- ✅ `logs off` returns: `OK: logs OFF`
- ✅ ESP_LOG* stops appearing in BLE after `logs off`

**Pass Criteria**: All commands execute and respond correctly.

---

### TC6: Encryption Gating

**Objective**: Verify console requires encrypted connection.

**Procedure**:
1. Flash device with Config B
2. Commission device
3. Connect to `LIGHT-DBG-XXXX` but **do not pair** (if your BLE tool supports this)
4. Try to enable notifications on TX characteristic
5. Try to write to RX characteristic

**Expected Results**:
- ✅ Cannot enable notifications (or notifications don't work)
- ✅ Cannot write commands (or writes are rejected)
- ✅ UART shows no command processing

**Pass Criteria**: Unencrypted access is blocked.

**Note**: Some BLE tools auto-pair on connection. In that case, use a tool like `gatttool` or `bluetoothctl` for this test.

---

### TC7: Bonding Requirement

**Objective**: Verify `bond strict on` (default) blocks unbonded access.

**Procedure**:
1. Continue from TC4 (bonded connection)
2. In console, write `bond strict off\n`
3. Disconnect
4. In nRF Connect, forget/unpair device
5. Reconnect **without bonding** (if tool supports)
6. Try commands
7. Write `bond strict on\n`
8. Try commands again

**Expected Results**:
- ✅ With `bond strict off`: Can access console when encrypted but not bonded
- ✅ With `bond strict on`: Cannot access console until bonded
- ✅ Status reflects current setting

**Pass Criteria**: Bonding requirement is enforced when enabled.

---

### TC8: Commissioning Window Opens - Console Stops

**Objective**: Verify console stops when commissioning window is reopened.

**Procedure**:
1. Continue from TC4 (console connected and working)
2. Open commissioning window:
   ```bash
   chip-tool pairing open-commissioning-window 0x7283 1 300 2000 3840
   ```
   OR press button to open commissioning window (if configured)
3. Observe BLE connection and UART

**Expected Results**:
- ✅ UART shows: "Commissioning window opened - stopping BLE console ADV"
- ✅ BLE connection terminates
- ✅ Console advertising stops
- ✅ `LIGHT-DBG-*` no longer visible in scan

**Pass Criteria**: Console automatically disconnects and stops advertising.

---

### TC9: Commissioning Window Closes - Console Restarts

**Objective**: Verify console restarts after commissioning window closes.

**Procedure**:
1. Continue from TC8
2. Wait for commissioning window to timeout (300 seconds) OR close it:
   ```bash
   chip-tool pairing close-commissioning-window 0x7283
   ```
3. Scan for BLE devices

**Expected Results**:
- ✅ UART shows: "Commissioning window closed - starting BLE console ADV"
- ✅ `LIGHT-DBG-XXXX` reappears in BLE scan
- ✅ Can reconnect and use console

**Pass Criteria**: Console advertising resumes automatically.

---

### TC10: Bonded Device Reconnection

**Objective**: Verify bonded device can reconnect without re-pairing.

**Procedure**:
1. Bond device (TC4)
2. Disconnect
3. Reconnect from nRF Connect

**Expected Results**:
- ✅ No pairing prompt appears
- ✅ Connection establishes immediately
- ✅ Commands work without re-entering passkey
- ✅ UART shows: "Security: enc=1 bond=1"

**Pass Criteria**: Bonded devices reconnect seamlessly.

---

### TC11: Rate Limiting

**Objective**: Verify rate limiter prevents console from flooding BLE stack.

**Procedure**:
1. Connect console (TC4)
2. Enable `logs on`
3. Trigger high-frequency logging (e.g., rapid button presses or generate logs in firmware)
4. Observe BLE notification rate
5. Write `rate off\n`
6. Trigger high-frequency logging again
7. Write `rate on\n`

**Expected Results**:
- ✅ With `rate on`: Notifications throttled, no BLE stack errors
- ✅ With `rate off`: All logs sent immediately (may be faster but risks overload)
- ✅ Status reflects current setting
- ✅ Device remains responsive to Matter commands

**Pass Criteria**: Rate limiter prevents BLE stack saturation when enabled.

---

### TC12: MTU Negotiation and Chunking

**Objective**: Verify console handles different MTU sizes correctly.

**Procedure**:
1. Connect with default MTU (usually 23 bytes, payload 20)
2. Write `help\n` (response > 100 bytes)
3. If possible, request MTU=185 (payload 182)
4. Write `help\n` again

**Expected Results**:
- ✅ Help text received completely at MTU=23 (multiple notifications)
- ✅ Help text received completely at MTU=185 (fewer notifications)
- ✅ No truncation or corruption
- ✅ UART shows: "MTU update: X"

**Pass Criteria**: Large outputs transmitted correctly regardless of MTU.

---

### TC13: Long Output Streaming

**Objective**: Verify console can stream long outputs without crashes or leaks.

**Procedure**:
1. Connect console (TC4)
2. Enable `logs on`
3. Trigger continuous logging for 60 seconds (e.g., periodic timer logs)
4. Monitor free heap: `free` command or UART logs

**Expected Results**:
- ✅ Logs stream continuously over BLE
- ✅ No crashes or disconnections
- ✅ Free heap remains stable (no memory leak)
- ✅ Device responsive to Matter commands

**Pass Criteria**: Console streams for 60+ seconds without issues.

---

### TC14: Rapid Disconnect/Reconnect Cycles

**Objective**: Verify console handles connection instability.

**Procedure**:
1. Connect console (TC4)
2. Disconnect
3. Reconnect
4. Repeat 10 times rapidly

**Expected Results**:
- ✅ All reconnections successful
- ✅ No crashes or panics
- ✅ Console state resets properly each time
- ✅ Free heap remains stable

**Pass Criteria**: 10 disconnect/reconnect cycles without failure.

---

### TC15: Factory Reset Behavior

**Objective**: Verify console stops after factory reset.

**Procedure**:
1. Commission device with console connected (TC4)
2. Trigger factory reset (e.g., long button press or `chip-tool pairing unpair`)
3. Observe BLE connection and advertising

**Expected Results**:
- ✅ UART shows: "Fabric removed - stopping BLE console"
- ✅ BLE connection terminates
- ✅ Console advertising stops
- ✅ `LIGHT-DBG-*` no longer visible
- ✅ Console does NOT restart until device is re-commissioned

**Pass Criteria**: Console remains off after factory reset.

---

### TC16: Concurrent Matter Operations

**Objective**: Verify console doesn't interfere with Matter functionality.

**Procedure**:
1. Commission device (TC3)
2. Connect console (TC4)
3. Enable `logs on`
4. Send Matter commands from chip-tool:
   ```bash
   chip-tool onoff toggle 0x7283 1
   chip-tool levelcontrol move-to-level 128 0 0 0 0x7283 1
   chip-tool colorcontrol move-to-hue-and-saturation 180 128 0 0 0 0x7283 1
   ```
5. Observe Matter response times and console responsiveness

**Expected Results**:
- ✅ All Matter commands execute successfully
- ✅ Matter response times < 500ms
- ✅ Console remains connected and responsive
- ✅ Logs show Matter operations
- ✅ No errors in UART or BLE

**Pass Criteria**: Console doesn't slow down or interfere with Matter operations.

---

### TC17: Build Size Comparison

**Objective**: Verify feature has zero overhead when disabled.

**Procedure**:
1. Build with Config A (feature OFF)
2. Note binary size: `ls -lh build/*.bin`
3. Clean: `rm -rf build sdkconfig`
4. Build with Config B (feature ON)
5. Note binary size
6. Calculate difference

**Expected Results**:
- ✅ Feature OFF: Binary size = baseline
- ✅ Feature ON: Binary size = baseline + ~15KB
- ✅ Feature OFF build: No debug_console files compiled
- ✅ Feature ON build: debug_console files present

**Pass Criteria**: Feature OFF has ≤ 100 bytes overhead (linker artifacts only).

---

### TC18: ESP Console Integration

**Objective**: Verify standard ESP console commands work over BLE.

**Procedure**:
1. Connect console (TC4)
2. Try standard commands:
   - `restart\n` (if safe to reboot)
   - `free\n`
   - `version\n`
   - `help\n` (should show both BLE and ESP commands)

**Expected Results**:
- ✅ `restart`: Device reboots
- ✅ `free`: Heap info displayed
- ✅ `version`: Version info displayed
- ✅ Unknown command: Echo back or error response

**Pass Criteria**: ESP console commands execute via BLE.

---

## Test Result Template

| TC | Test Case | Config | Result | Notes |
|----|-----------|--------|--------|-------|
| TC1 | Feature OFF | A | ⬜ PASS / ⬜ FAIL | |
| TC2 | No Console Before Commissioning | B | ⬜ PASS / ⬜ FAIL | |
| TC3 | Console After Commissioning | B | ⬜ PASS / ⬜ FAIL | |
| TC4 | Pairing and Encryption | B | ⬜ PASS / ⬜ FAIL | |
| TC5 | Basic Commands | B | ⬜ PASS / ⬜ FAIL | |
| TC6 | Encryption Gating | B | ⬜ PASS / ⬜ FAIL | |
| TC7 | Bonding Requirement | B | ⬜ PASS / ⬜ FAIL | |
| TC8 | Commissioning Opens - Stop | B | ⬜ PASS / ⬜ FAIL | |
| TC9 | Commissioning Closes - Restart | B | ⬜ PASS / ⬜ FAIL | |
| TC10 | Bonded Reconnection | B | ⬜ PASS / ⬜ FAIL | |
| TC11 | Rate Limiting | B | ⬜ PASS / ⬜ FAIL | |
| TC12 | MTU Negotiation | B | ⬜ PASS / ⬜ FAIL | |
| TC13 | Long Output Streaming | B | ⬜ PASS / ⬜ FAIL | |
| TC14 | Rapid Reconnect Cycles | B | ⬜ PASS / ⬜ FAIL | |
| TC15 | Factory Reset | B | ⬜ PASS / ⬜ FAIL | |
| TC16 | Concurrent Matter Ops | B | ⬜ PASS / ⬜ FAIL | |
| TC17 | Build Size | A+B | ⬜ PASS / ⬜ FAIL | |
| TC18 | ESP Console Integration | B | ⬜ PASS / ⬜ FAIL | |

## Regression Test Suite

Run these tests on every firmware change:
- TC1 (Feature OFF)
- TC2 (No console before commissioning)
- TC3 (Console after commissioning)
- TC4 (Pairing)
- TC5 (Basic commands)
- TC8 (Commissioning window opens)

## Acceptance Criteria

**Minimum to PASS**:
- All Critical Test Cases (TC1-TC10) must PASS
- No crashes, panics, or memory leaks
- Matter commissioning unaffected by BLE console
- Console never interferes with commissioning window

**Recommended to PASS**:
- TC11-TC16 should PASS for production-quality implementation
- TC17-TC18 should PASS for complete validation

## Known Issues / Limitations

*Document any known issues discovered during testing here.*

## Test Sign-Off

- **Tester Name**: ________________
- **Date**: ________________
- **Firmware Version**: ________________
- **Overall Result**: ⬜ PASS / ⬜ FAIL
- **Notes**: ________________

---

**Test Plan Version**: 1.0  
**Last Updated**: October 3, 2025
