// SPDX-License-Identifier: GPL-3.0-only
// The editor's link (web/fomni_ed.js) against the firmware itself: the browser build in Node, with the
// link's messages going in and out as SysEx (app/editor.c).
//   node tests/host/editor_test.mjs build/emu/omni.wasm
import fs from "fs";
import { Link, chordName, parseChord, setsToFile, fileToSets, frame, parse, CMD } from "../../web/fomni_ed.js";

const wasm = fs.readFileSync(process.argv[2]);
let mem;
const wasi = new Proxy({ clock_time_get: (id, p, out) => { new DataView(mem.buffer).setBigUint64(out, 0n, true); return 0; },
  fd_write: (fd, iov, n, out) => { new DataView(mem.buffer).setUint32(out, 0, true); return 0; } }, { get: (t, k) => k in t ? t[k] : () => 0 });
const { instance } = await WebAssembly.instantiate(wasm, { wasi_snapshot_preview1: wasi, env: new Proxy({}, { get: () => () => 0 }) });
const ex = instance.exports; mem = ex.memory;
if (ex._initialize) ex._initialize();
ex.web_master(2800); ex.web_boot();

let failed = 0;
const ok = (cond, what) => { console.log((cond ? "  ok   " : "  FAIL ") + what); if (!cond) failed = 1; };

// the wire: a message in, the device runs a moment, its reply (if any) comes back
let replies = ex.web_sysex_replies();
const link = new Link((bytes) => {
  const body = bytes.subarray(1, bytes.length - 1);
  new Uint8Array(mem.buffer, ex.web_sysex_in(), body.length).set(body);
  ex.web_sysex_push(body.length);
  for (let i = 0; i < 8 && ex.web_sysex_replies() === replies; i++) ex.web_render(128);   // (a render may need no new block)
  if (ex.web_sysex_replies() !== replies) {
    replies = ex.web_sysex_replies();
    const out = new Uint8Array(mem.buffer, ex.web_sysex_out(), ex.web_sysex_out_len()).slice();
    queueMicrotask(() => link.receive(out));
  }
}, { timeout: 50 });

const info = await link.info();
ok(info.protocol === 1 && info.nsets === 8 && info.npads === 11 && info.ntypes === 9 && info.version.length > 0, `info: ${JSON.stringify(info)}`);

let d = await link.sets();
ok(d.set === 0 && d.sets.length === 8 && d.sets[0].map(chordName).join(" ") === "F C G Dm Am Em G7 E7 D7 Bb A7", "set 1 is the factory one: " + d.sets[0].map(chordName).join(" "));
ok(d.sets[1].map(chordName).join(" ") === "C G D Am Em Bm D7 B7 A7 F E7", "set 2 is the same in G: " + d.sets[1].map(chordName).join(" "));

const s0 = await link.state();
const back = await link.pad(3, 5, parseChord("F#sus4"));
ok(chordName(back) === "F#sus4", "a button written: set 4, button 6 = F#sus4");
const s1 = await link.state();
ok(s1.sig !== s0.sig && s1.set === 0, "the state's number changes with it");
d = await link.sets();
ok(chordName(d.sets[3][5]) === "F#sus4", "... and it reads back");

ok(await link.select(4) === 4 && (await link.state()).set === 4, "set 5 picked");
ex.web_enc(2, -1);                                  // PRESETS, on the FM-1 itself
for (let i = 0; i < 20; i++) ex.web_render(128);
ok((await link.state()).set === 3, "PRESETS turned on the FM-1: the state follows");

let refused = false;
try { await link.request(CMD.PAD, [0, 0, 12, 0]); } catch { refused = true; }
ok(refused, "a root out of range: no reply");

// a file, there and back
const mine = d.sets.map((row) => row.map((c) => ({ ...c })));
mine[7] = "Eb Bb F Cm Gm Dm Bb7 G7 F7 Ab Cdim".split(" ").map(parseChord);
const text = setsToFile(mine);
await link.put(fileToSets(text));
d = await link.sets();
ok(d.sets[7].map(chordName).join(" ") === "Eb Bb F Cm Gm Dm Bb7 G7 F7 Ab Cdim" && chordName(d.sets[3][5]) === "F#sus4", "a file's sets written, and read back");
let bad = 0;
for (const t of ['{"fomni":"chord-sets","sets":[]}', "{}", text.replace("Cdim", "Cmaj9")]) { try { fileToSets(t); } catch { bad++; } }
ok(bad === 3, "files that are not chord sets are refused");
ok(parse(frame(CMD.INFO)).cmd === 1 && parse([0xF0, 0, 0x59, 0x11, 0xF7]) === null && chordName(parseChord("Db")) === "C#", "frames and names");

// a port that sends back what it is sent (ALSA's "Midi Through") is not an FM-1
const echo = new Link((bytes) => queueMicrotask(() => echo.receive(bytes)), { timeout: 50 });
let notOne = false;
try { await echo.info(); } catch { notOne = true; }
ok(notOne, "a port that echoes is not taken for an FM-1");

console.log(failed ? "FAILED" : "editor link ok");
process.exit(failed);
