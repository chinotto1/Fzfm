// SPDX-License-Identifier: GPL-3.0-or-later
// A wire-level device model. Used only by tests; never shipped by the installer.
export function wirePack(bytes) {
  const result = [];
  for (let bit = 0; bit < bytes.length * 8; bit += 7) {
    let value = 0;
    for (let n = 0; n < 7 && bit + n < bytes.length * 8; n++)
      value |= ((bytes[(bit + n) >>> 3] >>> ((bit + n) & 7)) & 1) << n;
    result.push(value);
  }
  return result;
}
export function wireUnpack(bytes) {
  const result = new Uint8Array(Math.floor(bytes.length * 7 / 8));
  for (let bit = 0; bit < result.length * 8; bit++)
    result[bit >>> 3] |= ((bytes[Math.floor(bit / 7)] >>> (bit % 7)) & 1) << (bit & 7);
  return [...result];
}
export function message(command, body, corrupt = false) {
  const checksum = (~body.reduce((a, b) => a + b, 0) & 255) ^ Number(corrupt);
  return [240, ...wirePack([0, 89, command, body.length & 255, body.length >>> 8, 0, ...body, checksum]), 247];
}
export function readRequest(addr, len = 64) {
  return message(48, [0, addr & 255, addr >>> 8 & 255, addr >>> 16 & 255, addr >>> 24 & 255, len & 255, len >>> 8 & 255, len >>> 16 & 255]);
}
export function fixturePackage(product = 'FM-1_713') {
  const bytes = Uint8Array.from({length:1200}, (_, i) => (i * 37 + 13) & 255);
  for (let i = 0; i < 20; i++) bytes[i * 48 + 47] = i < product.length ? product.charCodeAt(i) + i + 1 : 125;
  return bytes;
}
export class FakeFM1 {
  constructor(image, options = {}) {
    this.image = image;
    this.options = options;
    this.access = {inputs:new Map(), outputs:new Map(), sysexEnabled:true, onstatechange:null};
    this.upgrades = 0; this.queries = 0; this.responses = 0; this.bad = 0; this.dropped = 0; this.sequence = 0;
    this.boot(options.identity || 'FM-1_713');
  }
  boot(identity) {
    for (const port of [this.input, this.output]) if (port) {
      port.state = 'disconnected';
      this.access.onstatechange?.({port});
    }
    this.identity = identity;
    const number = ++this.sequence, name = identity.startsWith('ota-') ? 'Skiff Update' : (this.options.name || 'Skiff');
    const base = {name, state:'connected', connection:'closed', manufacturer:'FM1 test device',
      async open() { this.connection = 'open'; }, async close() { this.connection = 'closed'; }};
    this.input = {...base, id:`in-${number}`, type:'input', onmidimessage:null};
    this.output = {...base, id:`out-${number}`, type:'output'};
    const output = this.output;
    output.send = bytes => {
      if (output.state === 'disconnected' || (this.options.failUpgrade && bytes[1] === 34)) throw new Error('Output lost');
      const packet = [...bytes];
      // Native Web MIDI queues output. Closing a port can discard pending data;
      // a completion reply must survive Link cleanup and reach the updater.
      setTimeout(() => {
        if (output.connection !== 'open' || output.state === 'disconnected') { this.dropped++; return; }
        this.send(packet);
      }, 0);
    };
    this.access.inputs.set(this.input.id, this.input); this.access.outputs.set(this.output.id, this.output);
  }
  emit(bytes) { this.input.onmidimessage?.({data:new Uint8Array(bytes)}); }
  send(bytes) {
    if (this.output.state === 'disconnected') throw new Error('Disconnected test output');
    if (bytes.length === 10 && bytes[7] === 64) {
      this.queries++;
      const identity = [...new TextEncoder().encode(this.identity)];
      const frame = message(17, [...identity, ...new Array(27 - identity.length).fill(0)], this.options.badIdentityChecksum);
      setTimeout(() => this.emit(frame), 0);
      return;
    }
    if (bytes.length === 6 && bytes[1] === 34) {
      if (this.options.failUpgrade) throw new Error('Output lost');
      this.upgrades++;
      const writing = this.identity.startsWith('ota-');
      this.phase = writing ? 'write' : 'prepare';
      this.phaseResponses = 0;
      this.queue = writing
        ? [...Array.from({length:Math.ceil(this.image.length / 512)}, (_, i) => [i * 512, Math.min(512, this.image.length - i * 512)]), [0xF0000000, 8]]
        : [[0, 64], [64, 160], [this.options.badRange ? this.image.length - 1 : 224, this.options.badLength || (this.options.badRange ? 2 : 128)], [this.options.wrongStage ? 0xF0000000 : 0xE0000000, 8]];
      this.next(); return;
    }
    const decoded = wireUnpack(bytes.slice(1, -1));
    if (decoded[2] !== 48) { this.bad++; return; }
    const addr = (decoded[7] | decoded[8] << 8 | decoded[9] << 16 | decoded[10] << 24) >>> 0;
    const len = decoded[11] | decoded[12] << 8;
    const expected = addr >= 0xE0000000 ? [...new TextEncoder().encode('success'), 0] : [...this.image.slice(addr, addr + len)];
    if (decoded.at(-1) !== (~decoded.slice(6, -1).reduce((a, b) => a + b, 0) & 255) ||
        decoded.slice(14, -1).join() !== expected.join() || addr !== this.waiting?.[0] || len !== expected.length) this.bad++;
    this.responses++; this.phaseResponses++;
    if (this.options.unplugPhase === this.phase && this.phaseResponses >= (this.options.unplugAfter || 1)) {
      this.disconnect(); return;
    }
    if (addr === 0xE0000000) {
      this.boot(this.options.loaderIdentity || `ota-${this.options.target || 'FM-1_713'}`);
    } else if (addr === 0xF0000000) {
      this.boot(this.options.finalIdentity || this.options.target || 'FM-1_713');
    } else this.next();
  }
  next() {
    this.waiting = this.queue.shift();
    if (this.waiting) setTimeout(() => this.emit(readRequest(...this.waiting)), 0);
  }
  disconnect() {
    for (const port of [this.input, this.output]) { port.state = 'disconnected'; this.access.onstatechange?.({port}); }
  }
}
