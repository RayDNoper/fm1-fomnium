// SPDX-License-Identifier: GPL-3.0-only
// The editor's link (web/fomni_ed.js) against the firmware itself: the browser build in Node, with the
// link's messages going in and out as SysEx (app/editor.c).
//   node tests/host/editor_test.mjs build/emu/omni.wasm
import fs from "fs";
import { Link, chordName, parseChord, setsToFile, fileToSets, frame, parse, CMD, blankRhythm, rhythmToFile, fileToRhythm,
         parseBank, bankFile, dxName, dxEmpty, DX_BYTES } from "../../web/fomni_ed.js";

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
ok(info.protocol === 4 && link.ndx === 32 && link.nrhythm === 11 && link.nuser === 8 && info.nsets === 8 && info.npads === 11 && info.ntypes === 9 && info.version.length > 0, `info: ${JSON.stringify(info)}`);

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

// how a chord lies: its bass note in the name
const inv = await link.pad(0, 1, parseChord("C/E"));
d = await link.sets();
ok(inv.inv === 2 && chordName(d.sets[0][1]) === "C/E" && chordName(parseChord("Am/A")) === "Am/A" && parseChord("C/G").inv === 3 &&
   parseChord("C").inv === 0 && parseChord("C/F") === null, "a chord's inversion written, read back and named: C/E");
link.protocol = 1;                                  // as with an FM-1 of before: plain types
d = await link.sets();
ok(d.sets[0][1].inv === 0 && d.sets[0][1].type === 0, "protocol 1's SETS still answers, the types alone");
link.protocol = 2;
d = await link.sets();

let refused = false;
try { await link.request(CMD.PAD, [0, 0, 12, 0]); } catch { refused = true; }
ok(refused, "a root out of range: no reply");

// a file, there and back
const mine = d.sets.map((row) => row.map((c) => ({ ...c })));
mine[7] = "Eb Bb/D F Cm Gm Dm Bb7 G7 F7 Ab Cdim".split(" ").map(parseChord);
const text = setsToFile(mine);
await link.put(fileToSets(text));
d = await link.sets();
ok(d.sets[7].map(chordName).join(" ") === "Eb Bb/D F Cm Gm Dm Bb7 G7 F7 Ab Cdim" && chordName(d.sets[0][1]) === "C/E" && chordName(d.sets[3][5]) === "F#sus4", "a file's sets written, and read back");
let bad = 0;
for (const t of ['{"fomni":"chord-sets","sets":[]}', "{}", text.replace("Cdim", "Cmaj9")]) { try { fileToSets(t); } catch { bad++; } }
ok(bad === 3, "files that are not chord sets are refused");
ok(parse(frame(CMD.INFO)).cmd === 1 && parse([0xF0, 0, 0x59, 0x11, 0xF7]) === null && chordName(parseChord("Db")) === "C#", "frames and names");

// the rhythms: the ones it comes with read; one of the player's written, used, played, and taken away
const rock = await link.rhythm(0);
ok(rock.name === "Rock 1" && rock.len === 32 && rock.beat === 4 && rock.drums[0] === 5 && rock.bass[0] === 1 && rock.chord[4] === 3,
   "Rock 1 read: " + rhythmToFile(rock).split("\n")[7].trim());
ok((await link.rhythm(11)).len === 0, "the player's first rhythm is empty");
const beat3 = fileToRhythm(rhythmToFile({ ...rock, name: "Mine", len: 12, beat: 3 }));
beat3.drums[1] |= 2;
beat3.note[0] = 2;
const r0 = await link.rstate();
await link.putRhythm(0, beat3);
const got = await link.rhythm(11), r1 = await link.rstate();
ok(got.name === "Mine" && got.len === 12 && got.beat === 3 && got.drums[1] === (rock.drums[1] | 2) && got.note[0] === 2 && got.bass[0] === 1 &&
   got.drums[12] === 0 && r1.sig !== r0.sig, "a rhythm written and read back; the rhythms' number changes");
ok(await link.useRhythm(11) === 11 && (await link.rstate()).rhythm === 11, "... and used");
await link.play(true);
for (let i = 0; i < 40; i++) ex.web_render(128);
ok((await link.rstate()).playing, "the rhythm started from the editor");
await link.play(false);
let refusedR = 0;
try { await link.useRhythm(12); } catch { refusedR++; }
try { await link.putRhythm(1, { ...beat3, beat: 0 }); } catch { refusedR++; }
try { fileToRhythm(rhythmToFile(beat3).replace("TR", "TZ")); } catch { refusedR++; }
ok(refusedR === 3, "an empty rhythm is not used; a rhythm or a file out of range is refused");
await link.putRhythm(0, { ...blankRhythm(), len: 0 });
ok((await link.rhythm(11)).len === 0, "the rhythm taken away");

// the DX7 bank: a bank's file made here (two sounds, the rest empty), sent, read back, used, taken away
const patch = (name) => {
  const p = new Uint8Array(DX_BYTES);
  p.fill(99, 85, 92);                                // op 1 (the last in a patch): rates, and levels 1..3
  p[99] = 99; p[100] = 2;                            // its level; the note's own pitch
  p.fill(50, 106, 110);
  p[110] = 31; p[117] = 24;
  p.set(Array.from(name.padEnd(10), (c) => c.charCodeAt(0)), 118);
  return p;
};
const file = bankFile(Array.from({ length: 32 }, (_, i) => (i === 0 ? patch("ONE") : i === 31 ? patch("LAST") : new Uint8Array(DX_BYTES))));
const bank = parseBank(file);
let notBank = 0;
try { parseBank(file.slice(0, 4000)); } catch { notBank++; }
try { parseBank(Uint8Array.from(file, (v, i) => (i === 3 ? 0 : v))); } catch { notBank++; }
ok(file.length === 4104 && (file.slice(6, 4103).reduce((a, v) => a + v, 0) & 127) === 0 && bank.length === 32 && dxName(bank[31]) === "LAST" &&
   !dxEmpty(bank[0]) && dxEmpty(bank[1]) && notBank === 2, "a bank's file written and read; another file is not one");
ok(dxEmpty(await link.dx(0)), "the FM-1's bank: empty as it comes");
let noDx = false;
try { await link.useDx(false, 0); } catch { noDx = true; }
for (let i = 0; i < 32; i++) await link.putDx(i, bank[i]);
const last = await link.dx(31);
ok(noDx && dxName(last) === "LAST" && last.every((v, i) => v === bank[31][i]) && dxEmpty(await link.dx(5)), "the bank sent and read back");
ok(await link.useDx(false, 31) === 31 && await link.useDx(true, 0) === 0, "its sounds used for the strings and the chord");
let noEmpty = false;
try { await link.useDx(true, 5); } catch { noEmpty = true; }
await link.putDx(31, new Uint8Array(DX_BYTES));
ok(noEmpty && dxEmpty(await link.dx(31)), "an empty one is not used; a sound taken away");
await link.putDx(0, new Uint8Array(DX_BYTES));

// a port that sends back what it is sent (ALSA's "Midi Through") is not an FM-1
const echo = new Link((bytes) => queueMicrotask(() => echo.receive(bytes)), { timeout: 50 });
let notOne = false;
try { await echo.info(); } catch { notOne = true; }
ok(notOne, "a port that echoes is not taken for an FM-1");

console.log(failed ? "FAILED" : "editor link ok");
process.exit(failed);
