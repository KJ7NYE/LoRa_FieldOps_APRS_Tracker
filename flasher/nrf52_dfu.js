/*
 * nrf52_dfu.js — Web Serial flasher for nRF52840 boards running the Adafruit
 * nRF52 bootloader (Heltec T114).
 *
 * This is a browser port of the exact path `pio run -e heltec_t114 -t upload`
 * takes, so the web flasher can update a T114 without the user double-tapping
 * RESET or dragging a file onto a USB drive:
 *
 *   1. "1200-baud touch": open the running firmware's USB CDC port at 1200
 *      baud and drop DTR. The Adafruit core's TinyUSB line-state callback sees
 *      that and reboots into the bootloader's serial-DFU mode.
 *   2. Legacy Nordic serial DFU (SDK 11 HCI/SLIP transport, "dfu_version 0.5")
 *      over the bootloader's CDC port — the protocol implemented by
 *      adafruit-nrfutil's dfu_transport_serial.py, which PlatformIO invokes.
 *
 * Every packet layout, constant and wait time below mirrors adafruit-nrfutil
 * (site-packages/nordicsemi/dfu/{dfu_transport_serial,util,crc16}.py). If the
 * two ever disagree, adafruit-nrfutil is the reference.
 *
 * The firmware image comes from the same .uf2 the flasher already publishes:
 * UF2 payloads are reassembled into a flat binary starting at the application
 * base, and trailing 0xFF padding (UF2 rounds up to 256-byte blocks) is
 * stripped. That is byte-identical to the firmware.bin inside the
 * adafruit-nrfutil DFU zip, so no extra release artifact is needed.
 *
 * Only the application region is written; the SoftDevice, bootloader and the
 * InternalFS (LittleFS) config area are untouched, so saved configuration
 * survives the update — same as UF2 drag-and-drop.
 *
 * Exposes `window.Nrf52SerialDfu`.
 */
(function () {
  'use strict';

  // ── Board constants (boards/heltec_t114.json) ────────────────────────────
  const UF2_FAMILY_NRF52840 = 0xADA52840;
  const APP_BASE_ADDRESS    = 0x26000;   // first byte after SoftDevice S140 v6.1.1
  const DEVICE_TYPE         = 0x0052;    // genpkg --dev-type 0x0052 (nRF52 family)
  const SOFTDEVICE_REQ      = 0x00B6;    // S140 v6.1.1 firmware ID (sd_fwid)

  // ── adafruit-nrfutil dfu_transport_serial.py constants ───────────────────
  const DFU_BAUD_RATE          = 115200;
  const TOUCH_BAUD_RATE        = 1200;
  const DFU_PACKET_MAX_SIZE    = 512;
  const FLASH_PAGE_SIZE        = 4096;
  const FLASH_PAGE_ERASE_TIME  = 0.0897;                                   // s
  const FLASH_PAGE_WRITE_TIME  = (FLASH_PAGE_SIZE / 4) * 0.000100;         // s
  const SERIAL_PORT_OPEN_WAIT  = 0.1;                                      // s
  const ACK_TIMEOUT_MS         = 1000;
  const MAX_SEND_ATTEMPTS      = 3;

  const DFU_INIT_PACKET      = 1;
  const DFU_START_PACKET     = 3;
  const DFU_DATA_PACKET      = 4;
  const DFU_STOP_DATA_PACKET = 5;
  const DFU_UPDATE_MODE_APP  = 4;   // HexType.APPLICATION

  const HCI_PACKET_TYPE = 14;
  const SLIP_END = 0xC0, SLIP_ESC = 0xDB, SLIP_ESC_END = 0xDC, SLIP_ESC_ESC = 0xDD;

  const UF2_MAGIC_START0 = 0x0A324655;
  const UF2_MAGIC_START1 = 0x9E5D5157;
  const UF2_FLAG_NOT_MAIN_FLASH = 0x00000001;
  const UF2_FLAG_FAMILY_ID      = 0x00002000;

  const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

  // Nordic's CRC16 variant (nordicsemi/dfu/crc16.py), used both for the HCI
  // packet trailer and for the firmware CRC carried in the init packet.
  function crc16(bytes, crc = 0xFFFF) {
    for (let i = 0; i < bytes.length; i++) {
      crc = ((crc >> 8) & 0x00FF) | ((crc << 8) & 0xFF00);
      crc ^= bytes[i];
      crc ^= (crc & 0x00FF) >> 4;
      crc ^= (crc << 12) & 0xFFFF;
      crc ^= ((crc & 0x00FF) << 5) & 0xFFFF;
    }
    return crc & 0xFFFF;
  }

  function uint32le(value) {
    return [value & 0xFF, (value >>> 8) & 0xFF, (value >>> 16) & 0xFF, (value >>> 24) & 0xFF];
  }
  function uint16le(value) {
    return [value & 0xFF, (value >>> 8) & 0xFF];
  }

  // ── UF2 → flat application image ─────────────────────────────────────────
  /**
   * Reassemble a UF2 file into the contiguous application binary the DFU
   * bootloader expects. Throws if the file isn't an nRF52840 image based at
   * the S140 v6 application start address (i.e. the wrong board's firmware).
   */
  function uf2ToAppImage(uf2Buffer) {
    const view = new DataView(uf2Buffer);
    if (uf2Buffer.byteLength === 0 || uf2Buffer.byteLength % 512 !== 0) {
      throw new Error('Firmware file is not a valid UF2 image (size is not a multiple of 512 bytes).');
    }

    const chunks = [];
    let lowAddress = Infinity, highAddress = 0;
    for (let offset = 0; offset < uf2Buffer.byteLength; offset += 512) {
      if (view.getUint32(offset, true) !== UF2_MAGIC_START0 ||
          view.getUint32(offset + 4, true) !== UF2_MAGIC_START1) {
        throw new Error(`Firmware file is not a valid UF2 image (bad magic in block ${offset / 512}).`);
      }
      const flags       = view.getUint32(offset + 8, true);
      const targetAddr  = view.getUint32(offset + 12, true);
      const payloadSize = view.getUint32(offset + 16, true);
      const familyId    = view.getUint32(offset + 28, true);
      if (flags & UF2_FLAG_NOT_MAIN_FLASH) continue;
      if ((flags & UF2_FLAG_FAMILY_ID) && familyId !== UF2_FAMILY_NRF52840) {
        throw new Error(`UF2 image targets family 0x${familyId.toString(16)}, not nRF52840.`);
      }
      if (payloadSize > 476) throw new Error('UF2 block payload exceeds 476 bytes.');
      chunks.push({ addr: targetAddr, data: new Uint8Array(uf2Buffer, offset + 32, payloadSize) });
      lowAddress  = Math.min(lowAddress, targetAddr);
      highAddress = Math.max(highAddress, targetAddr + payloadSize);
    }

    if (!chunks.length) throw new Error('UF2 image contains no flash data.');
    if (lowAddress !== APP_BASE_ADDRESS) {
      throw new Error(`UF2 image starts at 0x${lowAddress.toString(16)}, expected application base `
        + `0x${APP_BASE_ADDRESS.toString(16)}. Refusing to flash an image built for a different layout.`);
    }

    // Gaps (if any) read back as erased flash, so fill with 0xFF.
    const image = new Uint8Array(highAddress - lowAddress).fill(0xFF);
    for (const chunk of chunks) image.set(chunk.data, chunk.addr - lowAddress);

    // Strip UF2's 256-byte-block padding so the image matches firmware.bin
    // from the hex. Erased flash is already 0xFF, so this changes nothing on
    // the device — it only avoids writing padding.
    let end = image.length;
    while (end > 0 && image[end - 1] === 0xFF) end--;
    return image.slice(0, end);
  }

  /**
   * Legacy (dfu_version 0.5) init packet, identical to what
   * `adafruit-nrfutil dfu genpkg --dev-type 0x0052 --sd-req 0x00B6` writes to
   * firmware.dat: device type, device revision (any), app version (any),
   * SoftDevice requirement list, then the CRC16 of the image.
   */
  function buildInitPacket(image) {
    return new Uint8Array([
      ...uint16le(DEVICE_TYPE),
      ...uint16le(0xFFFF),          // device revision: any
      ...uint32le(0xFFFFFFFF),      // application version: any (no downgrade check)
      ...uint16le(1),               // SoftDevice requirement count
      ...uint16le(SOFTDEVICE_REQ),
      ...uint16le(crc16(image)),
    ]);
  }

  // ── HCI / SLIP framing ───────────────────────────────────────────────────
  /** Build one reliable HCI packet (HciPacket in dfu_transport_serial.py). */
  function buildHciPacket(sequenceNumber, payload) {
    const length = payload.length;
    const header = [
      sequenceNumber | (((sequenceNumber + 1) % 8) << 3) | (1 << 6) | (1 << 7), // seq, ack, data-integrity, reliable
      HCI_PACKET_TYPE | ((length & 0x000F) << 4),
      (length & 0x0FF0) >> 4,
    ];
    header.push((~(header[0] + header[1] + header[2]) + 1) & 0xFF);

    const body = new Uint8Array(4 + length + 2);
    body.set(header, 0);
    body.set(payload, 4);
    const crc = crc16(body.subarray(0, 4 + length));
    body[4 + length]     = crc & 0xFF;
    body[4 + length + 1] = (crc >> 8) & 0xFF;

    const framed = [SLIP_END];
    for (const b of body) {
      if (b === SLIP_END)      framed.push(SLIP_ESC, SLIP_ESC_END);
      else if (b === SLIP_ESC) framed.push(SLIP_ESC, SLIP_ESC_ESC);
      else                     framed.push(b);
    }
    framed.push(SLIP_END);
    return new Uint8Array(framed);
  }

  // ── Transport over an open Web Serial port ───────────────────────────────
  class DfuTransport {
    constructor(port) {
      this.port = port;
      this.sequenceNumber = 0;
      this.rxBuffer = [];
      this.reader = null;
      this.writer = null;
      this.readLoopDone = null;
    }

    async open() {
      await this.port.open({ baudRate: DFU_BAUD_RATE, flowControl: 'none' });
      await sleep(SERIAL_PORT_OPEN_WAIT * 1000);
      // adafruit-nrfutil pulses DTR when it didn't do the touch itself; the
      // bootloader ignores it but some USB stacks only start sending once DTR
      // is asserted, so mirror it.
      await this.port.setSignals({ dataTerminalReady: false });
      await sleep(50);
      await this.port.setSignals({ dataTerminalReady: true });
      await sleep(100);

      this.writer = this.port.writable.getWriter();
      this.reader = this.port.readable.getReader();
      this.readLoopDone = (async () => {
        try {
          for (;;) {
            const { value, done } = await this.reader.read();
            if (done) break;
            if (value) for (const b of value) this.rxBuffer.push(b);
          }
        } catch (_) { /* port closed or device reset — expected at the end */ }
      })();
    }

    async close() {
      try { await this.reader?.cancel(); } catch (_) {}
      try { await this.readLoopDone; } catch (_) {}
      try { this.reader?.releaseLock(); } catch (_) {}
      try { this.writer?.releaseLock(); } catch (_) {}
      try { await this.port.close(); } catch (_) {}
    }

    /** Wait for one SLIP-framed ACK; returns its ack number or null on timeout. */
    async waitForAck() {
      const deadline = performance.now() + ACK_TIMEOUT_MS;
      while (performance.now() < deadline) {
        const first = this.rxBuffer.indexOf(SLIP_END);
        if (first !== -1) {
          const second = this.rxBuffer.indexOf(SLIP_END, first + 1);
          if (second !== -1) {
            const frame = this.rxBuffer.slice(first + 1, second);
            this.rxBuffer.splice(0, second + 1);
            if (frame.length === 0) continue;          // back-to-back delimiters
            // Header byte 0 carries the ACK number in bits 3..5; un-SLIP it.
            let headerByte = frame[0];
            if (headerByte === SLIP_ESC && frame.length > 1) {
              headerByte = frame[1] === SLIP_ESC_END ? SLIP_END : SLIP_ESC;
            }
            return (headerByte >> 3) & 0x07;
          }
        }
        await sleep(2);
      }
      return null;
    }

    /**
     * Send one payload as a reliable HCI packet and wait for the bootloader's
     * ACK. On a timeout the same packet (same sequence number) is resent; the
     * bootloader drops duplicates by sequence number, so a retry is safe.
     */
    async sendPacket(payload) {
      this.sequenceNumber = (this.sequenceNumber + 1) % 8;
      const packet = buildHciPacket(this.sequenceNumber, payload);
      const expectedAck = (this.sequenceNumber + 1) % 8;
      for (let attempt = 1; attempt <= MAX_SEND_ATTEMPTS; attempt++) {
        await this.writer.write(packet);
        const ack = await this.waitForAck();
        if (ack === expectedAck) return;
        if (ack !== null) {
          // An ACK for something else (stale data from before we started).
          // adafruit-nrfutil accepts any first ACK; we only warn and retry.
          console.warn(`nRF DFU: unexpected ACK ${ack}, expected ${expectedAck} (attempt ${attempt})`);
        }
      }
      throw new Error('The bootloader stopped responding (no acknowledgement after '
        + `${MAX_SEND_ATTEMPTS} attempts). Is the selected port the bootloader?`);
    }
  }

  // ── Public API ───────────────────────────────────────────────────────────
  /**
   * Reboot a running T114 into its serial-DFU bootloader via a 1200-baud
   * touch. The port disappears and re-enumerates as the bootloader (usually
   * with a different USB product ID, so Web Serial treats it as a new device).
   */
  async function touchToBootloader(port) {
    await port.open({ baudRate: TOUCH_BAUD_RATE });
    try {
      await sleep(SERIAL_PORT_OPEN_WAIT * 1000);
      await port.setSignals({ dataTerminalReady: false, requestToSend: false });
    } catch (_) { /* device may already be resetting */ }
    try { await port.close(); } catch (_) { /* expected: device vanished */ }
  }

  /**
   * Flash an application image through a port that is already in the
   * bootloader. `onProgress(fraction, message)` is called throughout.
   */
  async function flashApplication(port, image, onProgress = () => {}) {
    const transport = new DfuTransport(port);
    onProgress(0, 'Opening bootloader port…');
    await transport.open();
    try {
      // Start packet: mode + [softdevice, bootloader, application] sizes.
      onProgress(0, 'Starting DFU and erasing flash…');
      await transport.sendPacket(new Uint8Array([
        ...uint32le(DFU_START_PACKET),
        ...uint32le(DFU_UPDATE_MODE_APP),
        ...uint32le(0), ...uint32le(0), ...uint32le(image.length),
      ]));
      const eraseWaitSec = Math.max(0.5, (Math.floor(image.length / FLASH_PAGE_SIZE) + 1) * FLASH_PAGE_ERASE_TIME);
      await sleep(eraseWaitSec * 1000);

      // Init packet (+ 2 bytes of padding the bootloader requires).
      onProgress(0, 'Sending init packet…');
      await transport.sendPacket(new Uint8Array([
        ...uint32le(DFU_INIT_PACKET), ...buildInitPacket(image), ...uint16le(0),
      ]));

      // Firmware, 512 bytes per packet; pause for a page write every 8 packets.
      const packetCount = Math.ceil(image.length / DFU_PACKET_MAX_SIZE);
      for (let i = 0; i < packetCount; i++) {
        const chunk = image.subarray(i * DFU_PACKET_MAX_SIZE, (i + 1) * DFU_PACKET_MAX_SIZE);
        const payload = new Uint8Array(4 + chunk.length);
        payload.set(uint32le(DFU_DATA_PACKET), 0);
        payload.set(chunk, 4);
        await transport.sendPacket(payload);
        if (i % 8 === 0) await sleep(FLASH_PAGE_WRITE_TIME * 1000);
        onProgress((i + 1) / packetCount, `Writing firmware… ${Math.round(((i + 1) / packetCount) * 100)}%`);
      }
      await sleep(FLASH_PAGE_WRITE_TIME * 1000);

      onProgress(1, 'Validating and activating firmware…');
      await transport.sendPacket(new Uint8Array(uint32le(DFU_STOP_DATA_PACKET)));
    } finally {
      await transport.close();
    }
    // Bootloader validates the CRC, swaps banks if needed and resets into the
    // new application. Give it the same grace period adafruit-nrfutil does.
    const totalPages = Math.floor(image.length / FLASH_PAGE_SIZE) + 1;
    const activateWaitSec = Math.max(0.5, totalPages * FLASH_PAGE_ERASE_TIME) + totalPages * FLASH_PAGE_WRITE_TIME;
    await sleep(activateWaitSec * 1000);
    onProgress(1, 'Done');
  }

  window.Nrf52SerialDfu = {
    uf2ToAppImage,
    buildInitPacket,
    touchToBootloader,
    flashApplication,
    // exported for offline verification against adafruit-nrfutil output
    _internals: { crc16, buildHciPacket },
  };
})();
