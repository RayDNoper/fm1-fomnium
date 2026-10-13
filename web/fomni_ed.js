// SPDX-License-Identifier: GPL-3.0-only
//
// Jambox FM-1's editor link in the browser: the SysEx protocol of firmware/src/app/editor.c
// (web/EDITOR_PROTOCOL.md), over anything that can carry a message (Web MIDI, the emulator).

export const HEADER = [0x7D, 0x46, 0x4F];           // the non-commercial id, "FO"
export const CMD = { INFO: 1, STATE: 2, SETS: 3, PAD: 4, SET: 5, PUT: 6, SETS2: 7, RHY: 8, RHYPUT: 9, RHYSEL: 10, RSTATE: 11, PLAY: 12,
                     DX: 13, DXPUT: 14, DXUSE: 15 };
export const NOTES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"];
export const TYPES = ["", "m", "7", "M7", "m7", "dim", "aug", "sus4", "add9"];
export const TYPE_LABEL = ["maj", "m", "7", "M7", "m7", "dim", "aug", "sus4", "add9"];

// each type's three tones (the firmware's OM_TYPE_TONES), and how a chord lies: 0 as the Omnichord folds
// it, 1 root position, 2 and 3 the inversions (the second, the third tone at the bottom and in the bass)
export const TONES = [[0, 4, 7], [0, 3, 7], [0, 4, 10], [0, 4, 11], [0, 3, 10], [0, 3, 6], [0, 4, 8], [0, 5, 7], [0, 4, 2]];
export const INV_LABEL = ["OM", "Root", "1st", "2nd"];

// the note at the bottom of a chord that lies some way ("" for the Omnichord's folding)
export function bassName(c) { return c.inv ? NOTES[(c.root + TONES[c.type][c.inv - 1]) % 12] : ""; }
// "Dm", "Bb7"; with how it lies as its bass note: "C/C" root position, "C/E", "C/G"
export function chordName(c) { return NOTES[c.root] + TYPES[c.type] + (c.inv ? "/" + bassName(c) : ""); }

function noteOf(s) {
  const m = /^([A-Ga-g])([#b]?)$/.exec(s);
  if (!m) return -1;
  const base = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 }[m[1].toUpperCase()];
  return (base + (m[2] === "#" ? 1 : m[2] === "b" ? 11 : 0)) % 12;
}

// "Dm", "Bb7", "F#sus4", "C/E" ... -> { root, type, inv }, or null. Sharps and flats both read.
export function parseChord(s) {
  const [name, bass, more] = String(s).trim().split("/");
  const m = /^([A-Ga-g][#b]?)(.*)$/.exec(name);
  if (!m || more !== undefined) return null;
  const root = noteOf(m[1]), type = TYPES.indexOf(m[2] === "maj" ? "" : m[2]);
  if (type < 0) return null;
  if (bass === undefined) return { root, type, inv: 0 };
  const inv = TONES[type].findIndex((t) => (root + t) % 12 === noteOf(bass));     // (the bass: one of its tones)
  return inv < 0 ? null : { root, type, inv: inv + 1 };
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
    this.protocol = 1;                                // 2: the FM-1 knows how a chord lies (inv); 3: the rhythms
    this.nrhythm = 0;                                 // the rhythms it comes with,
    this.nuser = 0;                                   // and the player's own after them
    this.ndx = 0;                                     // the patches of its DX7 bank (protocol 4)
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

  // (a port that only sends back what it is sent, as ALSA's "Midi Through" does, answers INFO with nothing:
  // that is not an FM-1)
  async info(timeout) {
    const a = await this.request(CMD.INFO, [], timeout);
    const end = a.indexOf(0, 4);
    if (a.length < 5 || a[0] < 1 || a[1] < 1 || a[1] > 16 || a[2] < 1 || a[2] > 16 || a[3] !== TYPES.length) throw new Error("not a Jambox FM-1");
    this.protocol = a[0];
    this.nsets = a[1];
    this.npads = a[2];
    if (a[0] >= 3 && end >= 0 && a.length >= end + 3) { this.nrhythm = a[end + 1]; this.nuser = a[end + 2]; }
    if (a[0] >= 4 && end >= 0 && a.length >= end + 4) this.ndx = a[end + 3];
    return { protocol: a[0], nsets: a[1], npads: a[2], ntypes: a[3],
             version: String.fromCharCode(...a.slice(4, end < 0 ? a.length : end)) };
  }

  async state() {
    const a = await this.request(CMD.STATE);
    if (a.length !== 5) throw new Error("a broken reply");
    return { set: a[0], sig: (a[1] | a[2] << 7 | a[3] << 14 | a[4] << 21) >>> 0 };
  }

  // a chord's type byte: with how it lies in bits 4, 5 (protocol 2)
  typeByte(c) { return this.protocol >= 2 ? c.type | (c.inv || 0) << 4 : c.type; }

  // { set, sets: [[{ root, type, inv } x 11] x 8] }
  async sets() {
    const a = await this.request(this.protocol >= 2 ? CMD.SETS2 : CMD.SETS);
    if (a.length !== 1 + 2 * this.nsets * this.npads) throw new Error("a broken reply");
    const sets = [];
    for (let s = 0; s < this.nsets; s++) {
      const row = [];
      for (let i = 0; i < this.npads; i++) {
        const k = 1 + 2 * (s * this.npads + i);
        row.push({ root: a[k], type: a[k + 1] & 15, inv: a[k + 1] >> 4 });
      }
      sets.push(row);
    }
    return { set: a[0], sets };
  }

  async pad(set, pad, c) {
    const a = await this.request(CMD.PAD, [set, pad, c.root, this.typeByte(c)]);
    if (a.length !== 4) throw new Error("a broken reply");
    return { root: a[2], type: a[3] & 15, inv: a[3] >> 4 };
  }

  async select(set) { return (await this.request(CMD.SET, [set]))[0]; }

  async put(sets) {
    const a = await this.request(CMD.PUT, sets.flatMap((row) => row.flatMap((c) => [c.root, this.typeByte(c)])), 1500);
    if (a[0] !== 0) throw new Error("the FM-1 refused the chord sets");
  }
}

// ---- rhythms (protocol 3). One is { name, len, beat, drums, bass, note, chord }: len steps (up to 32; 0: an
// empty one), beat steps to a beat, and for each of 32 steps the drums (bits: DRUMS), the bass and the chord
// (0 nothing, 1 play and hold, 2 let go, 3 play once, 4 cut: STEP_CMD) and the bass's note (0 root, 1 fifth,
// 2 third: BASS_NOTE)
export const DRUMS = ["Bass drum", "Claves", "Hi-hat", "Cymbal", "Snare"];
export const STEP_CMD = "-TRON";
export const BASS_NOTE = "R53";
export const RHY_NAME = 8;

export function blankRhythm(name = "") {
  const z = () => new Array(32).fill(0);
  return { name, len: 32, beat: 4, drums: z(), bass: z(), note: z(), chord: z() };
}

function rhythmBytes(r) {
  const name = Array.from({ length: RHY_NAME }, (_, i) => (i < r.name.length ? r.name.charCodeAt(i) & 0x7F : 0));
  const steps = [];
  for (let i = 0; i < 32; i++) {
    const on = i < r.len;
    steps.push(on ? r.drums[i] & 31 : 0, on ? (r.bass[i] & 7) | (r.note[i] & 3) << 3 : 0, on ? r.chord[i] & 7 : 0);
  }
  return [r.len, r.beat, ...name, ...steps];
}

Link.prototype.rhythm = async function (index) {
  const a = await this.request(CMD.RHY, [index]);
  if (a.length !== 3 + RHY_NAME + 96 || a[0] !== index) throw new Error("a broken reply");
  const r = blankRhythm(String.fromCharCode(...a.slice(3, 3 + RHY_NAME).filter((c) => c)));
  r.len = a[1];
  r.beat = a[2];
  for (let i = 0; i < 32; i++) {
    const k = 3 + RHY_NAME + 3 * i;
    r.drums[i] = a[k];
    r.bass[i] = a[k + 1] & 7;
    r.note[i] = a[k + 1] >> 3;
    r.chord[i] = a[k + 2];
  }
  return r;
};

// one of the player's own (slot 0..): written, or with len 0 taken away
Link.prototype.putRhythm = async function (slot, r) {
  const a = await this.request(CMD.RHYPUT, [slot, ...rhythmBytes(r)], 1500);
  if (a[0] !== slot || a[1] !== 0) throw new Error("the FM-1 refused the rhythm");
};

Link.prototype.useRhythm = async function (index) { return (await this.request(CMD.RHYSEL, [index]))[0]; };
Link.prototype.play = async function (on) { return (await this.request(CMD.PLAY, [on ? 1 : 0]))[0]; };
Link.prototype.rstate = async function () {
  const a = await this.request(CMD.RSTATE);
  if (a.length !== 6) throw new Error("a broken reply");
  return { rhythm: a[0], playing: !!a[1], sig: (a[2] | a[3] << 7 | a[4] << 14 | a[5] << 21) >>> 0 };
};

// ---- the DX7 bank (protocol 4): 32 patches, each the 128 bytes a bank's SysEx holds for it (six operators;
// its name in the last ten). They are voices for the strings and the chord, after the ones Jambox comes with.
export const DX_BYTES = 128;
export const DX_PATCHES = 32;
const DX_CARRIERS = [5, 5, 9, 9, 21, 21, 5, 5, 5, 9, 9, 5, 5, 5, 5, 1, 1, 1, 25, 11, 27, 29, 27, 31, 31, 11, 11, 37, 23, 39, 31, 63];

// a patch holds a sound when an operator that is heard has a level (as the firmware has it)
export function dxEmpty(p) {
  const car = DX_CARRIERS[p[110] & 31];
  for (let i = 0; i < 6; i++) if (car >> i & 1 && p[17 * (5 - i) + 14]) return false;
  return true;
}
export function dxName(p) {
  return String.fromCharCode(...Array.from(p.slice(118, 128), (c) => (c < 32 || c > 126 || c === 92 ? 32 : c))).trim();
}

// a DX7 32-voice bank (.syx) -> its 32 patches; an Error when the file is something else
export function parseBank(bytes) {
  const b = Uint8Array.from(bytes);
  if (b.length !== 4104 || b[0] !== 0xF0 || b[1] !== 0x43 || (b[2] & 0x70) !== 0 || b[3] !== 9 || b[4] !== 0x20 || b[5] !== 0 || b[4103] !== 0xF7)
    throw new Error(b[0] === 0xF0 && b[1] === 0x43 && b[3] === 0 ? "one DX7 voice, not a bank of 32" : "not a DX7 bank of 32 voices");
  return Array.from({ length: DX_PATCHES }, (_, i) => b.slice(6 + DX_BYTES * i, 6 + DX_BYTES * (i + 1)).map((v) => v & 0x7F));
}
// and back: the patches as a bank's file (with its checksum)
export function bankFile(patches) {
  const b = new Uint8Array(4104);
  b.set([0xF0, 0x43, 0, 9, 0x20, 0]);
  let sum = 0;
  patches.forEach((p, i) => p.forEach((v, k) => { b[6 + DX_BYTES * i + k] = v & 0x7F; sum += v & 0x7F; }));
  b[4102] = -sum & 0x7F;
  b[4103] = 0xF7;
  return b;
}

Link.prototype.dx = async function (index) {
  const a = await this.request(CMD.DX, [index]);
  if (a.length !== 1 + DX_BYTES || a[0] !== index) throw new Error("a broken reply");
  return Uint8Array.from(a.slice(1));
};
// a patch written (128 zeros: taken away)
Link.prototype.putDx = async function (index, patch) {
  const a = await this.request(CMD.DXPUT, [index, ...Array.from(patch, (v) => v & 0x7F)], 1500);
  if (a[0] !== index || a[1] !== 0) throw new Error("the FM-1 refused the patch");
};
// a patch as the voice of the strings (chord: false) or of the chord
Link.prototype.useDx = async function (chord, index) { return (await this.request(CMD.DXUSE, [chord ? 1 : 0, index]))[1]; };

// a rhythm as a file: a line of text a row, a character a step ("x" a drum's hit; the bass and chord as
// STEP_CMD has them, the bass's notes as BASS_NOTE), so it can be read and written by hand
export function rhythmToFile(r) {
  const row = (f) => Array.from({ length: r.len }, (_, i) => f(i)).join("");
  const j = { fomni: "rhythm", version: 1, name: r.name, steps: r.len, beat: r.beat, drums: {} };
  DRUMS.forEach((d, k) => { j.drums[d] = row((i) => (r.drums[i] >> k & 1 ? "x" : "-")); });
  j.bass = row((i) => STEP_CMD[r.bass[i]]);
  j.bassNote = row((i) => BASS_NOTE[r.note[i]]);
  j.chord = row((i) => STEP_CMD[r.chord[i]]);
  return JSON.stringify(j, null, 1);
}

export function fileToRhythm(text) {
  const j = JSON.parse(text);
  if (!j || j.fomni !== "rhythm" || !Number.isInteger(j.steps) || j.steps < 1 || j.steps > 32) throw new Error("not a Jambox FM-1 rhythm file");
  if (!Number.isInteger(j.beat) || j.beat < 1 || j.beat > 8) throw new Error("beat: 1 to 8 steps expected");
  const r = blankRhythm(String(j.name || "").replace(/[^ -~]/g, "").slice(0, RHY_NAME));
  r.len = j.steps;
  r.beat = j.beat;
  const row = (what, s, set) => {
    if (typeof s !== "string" || s.length !== j.steps) throw new Error(`${what}: ${j.steps} characters expected`);
    for (let i = 0; i < j.steps; i++) {
      const v = set.indexOf(s[i]);
      if (v < 0) throw new Error(`${what}, step ${i + 1}: "${s[i]}" is not one of "${set}"`);
      r[what === "bass" ? "bass" : what === "bassNote" ? "note" : "chord"][i] = v;
    }
  };
  DRUMS.forEach((d, k) => {
    const s = j.drums && j.drums[d];
    if (typeof s !== "string" || s.length !== j.steps) throw new Error(`${d}: ${j.steps} characters expected`);
    for (let i = 0; i < j.steps; i++) if (s[i] === "x") r.drums[i] |= 1 << k; else if (s[i] !== "-") throw new Error(`${d}, step ${i + 1}: "x" or "-" expected`);
  });
  row("bass", j.bass, STEP_CMD);
  row("bassNote", j.bassNote, BASS_NOTE);
  row("chord", j.chord, STEP_CMD);
  return r;
}

// the chord sets as a file: chord names (how one lies as its bass note, "C/E"), so it can be read and
// written by hand
export function setsToFile(sets) {
  return JSON.stringify({ fomni: "chord-sets", version: 1, sets: sets.map((row) => row.map(chordName)) }, null, 1);
}

export function fileToSets(text, nsets = 8, npads = 11) {
  const j = JSON.parse(text);
  if (!j || j.fomni !== "chord-sets" || !Array.isArray(j.sets) || j.sets.length !== nsets) throw new Error("not a Jambox FM-1 chord sets file");
  return j.sets.map((row, s) => {
    if (!Array.isArray(row) || row.length !== npads) throw new Error(`set ${s + 1}: ${npads} chords expected`);
    return row.map((name, i) => {
      const c = parseChord(name);
      if (!c) throw new Error(`set ${s + 1}, button ${i + 1}: "${name}" is not a chord Jambox FM-1 plays`);
      return c;
    });
  });
}
