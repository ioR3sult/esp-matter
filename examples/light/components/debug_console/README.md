# Debug Console (BLE GATT) - Security Configuration

This component provides a BLE GATT-based debug console for ESP-Matter projects.

## Configuration

### Enable/Disable Encrypted Mode

The console can operate in two modes:

**Encrypted Mode** (recommended for production):
```
CONFIG_DEBUG_CONSOLE_GATT=y
CONFIG_DEBUG_CONSOLE_GATT_ENCRYPTED=y
```

**Plain Mode** (development only):
```
CONFIG_DEBUG_CONSOLE_GATT=y
CONFIG_DEBUG_CONSOLE_GATT_ENCRYPTED=n
```

Note: CONFIG_DEBUG_CONSOLE_GATT defaults to `n` for production safety. Enable explicitly via menuconfig when needed.

Use `idf.py menuconfig` → "Debug Console (GATT)" to configure.

## Pairing Procedure with nRF Connect

When `CONFIG_DEBUG_CONSOLE_GATT_ENCRYPTED=y`:

1. **Connect**: Open nRF Connect, scan for `LIGHT-DBG-XXXX`, tap to connect
2. **Trigger Pairing**: Subscribe (⇅ icon) to TX characteristic (`...9D12`)
3. **Accept Pairing**: 
   - iOS: Tap "Pair" when prompted
   - Android: Accept numeric comparison dialog (will be auto-accepted on device)
4. **Verify**: Check device logs for `ENC_CHANGE encrypted=1 bonded=1`
5. **Test Console**: Write to RX characteristic (`...9D11`), e.g., `help\r\n` (hex: `68 65 6C 70 0D 0A`)

### Reconnection Test

After pairing once:
1. Disconnect and reboot the device
2. Reconnect from the same phone
3. Write to RX should work immediately without re-pairing

## Expected Boot Logs

With `CONFIG_DEBUG_CONSOLE_GATT_ENCRYPTED=y`:

```
I (xxx) DeviceLayer: SM configured: MITM=1 SC=1 IO_CAP=DISPLAY_YESNO
I (xxx) DeviceLayer: gatt_init: count_cfg rc=0
I (xxx) DeviceLayer: gatt_init: add_svcs rc=0
I (xxx) DeviceLayer: gatt_init: gatts_start rc=0
I (xxx) dbg_console: Console GATT registered=1 RX=<handle> TX=<handle>
```

During pairing and subscription:
```
I (xxx) dbg_console: Numeric compare: 123456 -> accepting
I (xxx) dbg_console: ENC_CHANGE status=0 encrypted=1 bonded=1
I (xxx) dbg_console: SUBSCRIBE: attr=<handle> -> notify=0 indicate=1
I (xxx) dbg_console: console online
```

## Security Notes

- **MITM Protection**: Enabled when encrypted mode is active
- **LE Secure Connections**: Uses ECDH key exchange (SC=1)
- **Bond Persistence**: Bonds stored in NVS, survive reboots
- **IO Capabilities**: Set to `DISPLAY_YESNO` for numeric comparison (auto-accept in dev)
- **Encryption Check**: RX writes return `BLE_ATT_ERR_INSUFFICIENT_ENCRYPTION` when not encrypted
- **Key Distribution**: Both sides exchange encryption and identity keys for reconnection

## Service UUIDs

- **Service**: `18EE2EF5-263D-4559-959F-4F9C429F9D10`
- **RX (Write)**: `18EE2EF5-263D-4559-959F-4F9C429F9D11`
- **TX (Indicate)**: `18EE2EF5-263D-4559-959F-4F9C429F9D12`

## Testing Matrix

### Encrypted Mode (CONFIG_DEBUG_CONSOLE_GATT_ENCRYPTED=y)

| Test | Expected Behavior |
|------|------------------|
| First write to RX | Triggers pairing, numeric comparison visible |
| After pairing | `ENC_CHANGE status=0 encrypted=1 bonded=1` logged |
| Subscribe to TX | `SUBSCRIBE: attr=X -> notify=0 indicate=1` logged |
| TX self-test | "console online" message sent after subscription |
| TX path | Indications/notifications delivered |
| Reboot + reconnect | No pairing prompt, RX writes work immediately |

### Plain Mode (CONFIG_DEBUG_CONSOLE_GATT_ENCRYPTED=n)

| Test | Expected Behavior |
|------|------------------|
| Write to RX | Works immediately without pairing |
| Encryption | No `ENC_CHANGE` events unless client initiates |
| Logs | No SM configuration logs |

### Commissioning Compatibility

| Test | Expected Behavior |
|------|------------------|
| Matter commissioning (0xFFF6) | Works normally, unaffected by console |
| Console during commissioning | Blocked by advertising guard |
| Console after commissioning | Starts advertising after window closes |

## Troubleshooting

**Pairing not triggered:**
- Check that `CONFIG_DEBUG_CONSOLE_GATT_ENCRYPTED=y` is set
- Verify SM configuration log appears in boot logs
- Ensure GATT services registered successfully (check handles are non-zero)

**RX writes fail after pairing:**
- Check encryption state: `ENC_CHANGE status=0 encrypted=1 bonded=1`
- Verify subscription: `SUBSCRIBE: attr=X -> notify=0 indicate=1`
- Look for "console online" self-test message
- Check MTU negotiation completed

**Bond not persisting:**
- Ensure `ble_store_config_init()` is called
- Check NVS partition is not corrupted
- Verify NVS is not being erased between reboots

**iOS vs Android differences:**
- iOS may auto-pair on first connection
- Android shows explicit numeric comparison dialog
- Both should bond and persist correctly
