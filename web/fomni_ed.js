// SPDX-License-Identifier: GPL-3.0-only
//
// FoMni-1's editor link in the browser: the SysEx protocol of firmware/src/app/editor.c
// (web/EDITOR_PROTOCOL.md), over anything that can carry a message (Web MIDI, the emulator).

export const HEADER = [0x7D, 0x46, 0x4F];           // the non-commercial id, "FO"
export const CMD = { INFO: 1, STATE: 2, SETS: 3, PAD: 4, SET: 5, PUT: 6 };
export const NOTES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"];
export const TYPES = ["", "m", "7", "M7", "m7", "dim", "aug", "sus4", "add9"];
export const TYPE_LABEL = ["maj", "m", "7", "M7", "m7", "dim", "aug", "sus4", "add9"];

export function chordName(c) { return NOTES[c.root] + TYPES[c.type]; }

// "Dm", "Bb7", "F#sus4" ... -> { root, type }, or null. Sharps and flats both read.
export function parseChord(s) {
  const m = /^([A-Ga-g])([#b]?)(.*)$/.exec(String(s).trim());
  if (!m) return null;
  const base = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 }[m[1].toUpperCase()];
  const root = (base + (m[2] === "#" ? 1 : m[2] === "b" ? 11 : 0)) % 12;
  const type = TYPES.indexOf(m[3] === "maj" ? "" : m[3]);
  return type < 0 ? null : { root, type };
}

export function frame(cmd, args = []) { return Uint8Array.from([0xF0, ...HEADER, cmd, ...args, 0xF7]); }

// a message from the device (with or without its F0 / F7) -> { cmd, args }, or null if not ours
export function parse(bytes) {
  let b = Array.from(bytes);
  if (b[0] === 0xF0) b = b.slice(1);
  if (b[b.length - 1] === 0xF7) b = b.slice(0, -1);
  if (b.length < 4 || b[0] !== HEADER[0] || b[1] !== HEADER[1] || b[2] !== HEADER[2]) return null;
  return { cmd: b[3], args: b.slice(4) };
}

// One request at a time, each with its one reply. send(Uint8Array) puts a message on the wire;
// give what comes back to receive().
export class Link {
  constructor(send, { timeout = 600 } = {}) {
    this.send = send;
    this.timeout = timeout;
    this.queue = Promise.resolve();
    this.waiting = null;
    this.nsets = 8;
    this.npads = 11;
  }

  receive(bytes) {
    const m = parse(bytes);
    if (!m || !this.waiting || m.cmd !== this.waiting.cmd) return;
    const w = this.waiting;
    this.waiting = null;
    clearTimeout(w.timer);
    w.resolve(m.args);
  }

  request(cmd, args = [], timeout = this.timeout) {
    const run = () => new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.waiting = null;
        reject(new Error("no reply"));
      }, timeout);
      this.waiting = { cmd, resolve, timer };
      try { this.send(frame(cmd, args)); } catch (e) { clearTimeout(timer); this.waiting = null; reject(e); }
    });
    const p = this.queue.then(run, run);
    this.queue = p.catch(() => {});
    return p;
  }

  async info(timeout) {
    const a = await this.request(CMD.INFO, [], timeout);
    const end = a.indexOf(0, 4);
    this.nsets = a[1];
    this.npads = a[2];
    return { protocol: a[0], nsets: a[1], npads: a[2], ntypes: a[3],
             version: String.fromCharCode(...a.slice(4, end < 0 ? a.length : end)) };
  }

  async state() {
    const a = await this.request(CMD.STATE);
    return { set: a[0], sig: (a[1] | a[2] << 7 | a[3] << 14 | a[4] << 21) >>> 0 };
  }

  // { set, sets: [[{ root, type } x 11] x 8] }
  async sets() {
    const a = await this.request(CMD.SETS);
    const sets = [];
    for (let s = 0; s < this.nsets; s++) {
      const row = [];
      for (let i = 0; i < this.npads; i++) {
        const k = 1 + 2 * (s * this.npads + i);
        row.push({ root: a[k], type: a[k + 1] });
      }
      sets.push(row);
    }
    return { set: a[0], sets };
  }

  async pad(set, pad, c) {
    const a = await this.request(CMD.PAD, [set, pad, c.root, c.type]);
    return { root: a[2], type: a[3] };
  }

  async select(set) { return (await this.request(CMD.SET, [set]))[0]; }

  async put(sets) {
    const a = await this.request(CMD.PUT, sets.flatMap((row) => row.flatMap((c) => [c.root, c.type])), 1500);
    if (a[0] !== 0) throw new Error("the FM-1 refused the chord sets");
  }
}

// the chord sets as a file: chord names, so it can be read and written by hand
export function setsToFile(sets) {
  return JSON.stringify({ fomni: "chord-sets", version: 1, sets: sets.map((row) => row.map(chordName)) }, null, 1);
}

export function fileToSets(text, nsets = 8, npads = 11) {
  const j = JSON.parse(text);
  if (!j || j.fomni !== "chord-sets" || !Array.isArray(j.sets) || j.sets.length !== nsets) throw new Error("not a FoMni-1 chord sets file");
  return j.sets.map((row, s) => {
    if (!Array.isArray(row) || row.length !== npads) throw new Error(`set ${s + 1}: ${npads} chords expected`);
    return row.map((name, i) => {
      const c = parseChord(name);
      if (!c) throw new Error(`set ${s + 1}, button ${i + 1}: "${name}" is not a chord FoMni-1 plays`);
      return c;
    });
  });
}
