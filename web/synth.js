// Midinja preview synth: Web Audio only, no samples, no libraries. Ported from bfunc.cc's
// deep-house-midi-generator (src/audio/synthEngine.ts). An ES module: the page imports it.
//
//   const s = new Synth();          // the AudioContext is made on the first call (a user gesture)
//   s.note('bass', 40, 100, s.time, 0.25);
//   s.drum(36, 110, s.time);         // GM drum notes: 36 kick, 38/39 snare/clap, 42/46 hats, 45/47/50 toms, 49 crash, 37 rim
//   s.stopAll();

// The riff voices. Each is two oscillators (type, detune in cents) into a low-pass with an envelope, then an ADSR.
// f0 → f1 → f2: the filter's start, peak (after fa s) and settle (after fd s), as multiples of cutoff.
export const VOICES = {
  bass:   { label: 'Bass',   osc: [['sine', 0], ['sawtooth', 4]],   cutoff: 950,  q: 6, f: [1.8, 1.8, 0.3], fa: 0.001, fd: 0.18, a: 0.005, d: 0.15, peak: 0.9, sus: 0.65, r: 0.15, pan: 0, delay: 0, reverb: 0.05, vol: 0.95 },
  lead:   { label: 'Lead',   osc: [['sawtooth', -6], ['sawtooth', 7]], cutoff: 3000, q: 1, f: [0.3, 2.5, 0.4], fa: 0.02, fd: 0.3, a: 0.01, d: 0.28, peak: 0.7, sus: 0.3, r: 0.35, pan: -0.25, delay: 0.35, reverb: 0.25, vol: 0.85 },
  square: { label: 'Square', osc: [['square', -5], ['square', 6]],     cutoff: 3000, q: 1, f: [0.4, 2.0, 0.5], fa: 0.03, fd: 0.35, a: 0.02, d: 0.32, peak: 0.5, sus: 0.18, r: 0.45, pan: 0.35, delay: 0.4, reverb: 0.35, vol: 0.82 },
  keys:   { label: 'Keys',   osc: [['triangle', -5], ['sine', 6]],     cutoff: 2200, q: 2, f: [1, 1, 1], fa: 0.01, fd: 0.01, a: 0.03, d: 0.6, peak: 0.5, sus: 0.25, r: 0.8, pan: 0, delay: 0.3, reverb: 0.45, vol: 0.75 },
};

export class Synth {
  ctx = null;
  #master; #delay; #reverb; #live = new Set(); #noise = new Map();
  volume = 0.8; delayAmt = 1; reverbAmt = 1;

  init() {
    if (this.ctx) return this.ctx;
    const AC = window.AudioContext || window.webkitAudioContext;
    const ctx = this.ctx = new AC();
    this.#master = ctx.createGain(); this.#master.gain.value = this.volume;
    // a limiter, so stacked tracks never clip
    const comp = ctx.createDynamicsCompressor();
    comp.threshold.value = -3; comp.knee.value = 8; comp.ratio.value = 12; comp.attack.value = 0.003; comp.release.value = 0.15;
    this.#master.connect(comp); comp.connect(ctx.destination);
    // a dotted-8th delay with feedback, and a short reverb from a decaying noise impulse
    const dl = ctx.createDelay(2), fb = ctx.createGain(), dIn = ctx.createGain();
    dl.delayTime.value = 0.369; fb.gain.value = 0.35;
    dIn.connect(dl); dl.connect(fb); fb.connect(dl); fb.connect(this.#master);
    this.#delay = { in: dIn, node: dl };
    const cv = ctx.createConvolver(), rIn = ctx.createGain(), len = Math.floor(ctx.sampleRate * 1.6);
    const ir = ctx.createBuffer(2, len, ctx.sampleRate);
    for (let c = 0; c < 2; c++) { const d = ir.getChannelData(c); for (let i = 0; i < len; i++) d[i] = (Math.random() * 2 - 1) * Math.pow(1 - i / len, 3); }
    cv.buffer = ir; rIn.connect(cv); cv.connect(this.#master);
    this.#reverb = { in: rIn };
    return ctx;
  }
  // call from a click: browsers start audio only after a gesture
  resume() { this.init(); if (this.ctx.state === 'suspended') this.ctx.resume(); }
  get time() { return this.ctx ? this.ctx.currentTime : 0; }

  setVolume(v) { this.volume = v; if (this.ctx) this.#master.gain.setTargetAtTime(Math.max(0, Math.min(1.5, v)), this.time, 0.05); }
  setTempo(bpm) { if (this.ctx) this.#delay.node.delayTime.setTargetAtTime(60 / bpm * 0.75, this.time, 0.05); }

  stopAll() {
    const t = this.time;
    for (const v of this.#live) { try { v.forEach(o => o.stop(t)); } catch { /* already stopped */ } }
    this.#live.clear();
  }

  // a channel strip for one note: gain (velocity × volume), pan, sends; returns its input
  #strip(vol, pan, delay, reverb, time) {
    const ctx = this.ctx, g = ctx.createGain(); g.gain.value = vol;
    let out = g;
    if (ctx.createStereoPanner) { const p = ctx.createStereoPanner(); p.pan.setValueAtTime(pan, time); g.connect(p); out = p; }
    out.connect(this.#master);
    const send = (amt, bus) => { if (amt <= 0) return; const s = ctx.createGain(); s.gain.value = amt; g.connect(s); s.connect(bus.in); };
    send(delay * 0.5 * this.delayAmt, this.#delay); send(reverb * 0.4 * this.reverbAmt, this.#reverb);
    return g;
  }
  #track(sources, end) {
    sources.forEach(o => o.stop(end));
    this.#live.add(sources);
    sources[0].onended = () => this.#live.delete(sources);
  }
  #noiseBuf(sec) {
    const k = Math.round(sec * 1000);
    if (!this.#noise.has(k)) {
      const n = Math.floor(this.ctx.sampleRate * sec), b = this.ctx.createBuffer(1, n, this.ctx.sampleRate), d = b.getChannelData(0);
      for (let i = 0; i < n; i++) d[i] = Math.random() * 2 - 1;
      this.#noise.set(k, b);
    }
    return this.#noise.get(k);
  }

  // a riff note: voice name, MIDI note, velocity 1–127, start (context time), seconds it sounds
  note(voice, midi, vel, time, dur) {
    this.init();
    const ctx = this.ctx, V = VOICES[voice] || VOICES.lead, freq = 440 * Math.pow(2, (midi - 69) / 12);
    const out = this.#strip(V.vol * (vel || 96) / 127, V.pan, V.delay, V.reverb, time);
    const filt = ctx.createBiquadFilter(), env = ctx.createGain();
    filt.type = 'lowpass'; filt.Q.setValueAtTime(V.q, time);
    filt.frequency.setValueAtTime(V.cutoff * V.f[0], time);
    filt.frequency.exponentialRampToValueAtTime(Math.min(12000, V.cutoff * V.f[1]), time + V.fa);
    filt.frequency.exponentialRampToValueAtTime(Math.max(60, V.cutoff * V.f[2]), time + Math.max(V.fa + 0.001, Math.min(dur, V.fd)));
    // a short note still gets its attack and decay, then the release from wherever it is
    const end = Math.max(time + V.a + 0.01, time + dur), decEnd = time + V.a + V.d;
    env.gain.setValueAtTime(0.0001, time);
    env.gain.exponentialRampToValueAtTime(V.peak, time + V.a);
    if (end >= decEnd) { env.gain.exponentialRampToValueAtTime(V.sus, decEnd); env.gain.setValueAtTime(V.sus, end); }
    else env.gain.exponentialRampToValueAtTime(V.peak + (V.sus - V.peak) * (end - time - V.a) / V.d, end);
    env.gain.exponentialRampToValueAtTime(0.0001, end + V.r);
    const oscs = V.osc.map(([type, det]) => {
      const o = ctx.createOscillator(); o.type = type; o.frequency.setValueAtTime(freq, time); o.detune.setValueAtTime(det, time);
      o.connect(filt); o.start(time); return o;
    });
    filt.connect(env); env.connect(out);
    this.#track(oscs, end + V.r + 0.05);
  }

  // a drum hit on a GM note: a synthesised 909-ish kit
  drum(gm, vel, time) {
    this.init();
    const ctx = this.ctx, v = (vel || 100) / 127, out = this.#strip(0.9, 0, 0, gm === 49 ? 0.3 : 0.08, time);
    const gain = (peak, at, len) => { const g = ctx.createGain(); g.gain.setValueAtTime(peak * v, at); g.gain.exponentialRampToValueAtTime(0.001, at + len); g.connect(out); return g; };
    const tone = (type, f0, f1, sweep, peak, len) => {
      const o = ctx.createOscillator(); o.type = type;
      o.frequency.setValueAtTime(f0, time); o.frequency.exponentialRampToValueAtTime(f1, time + sweep);
      o.connect(gain(peak, time, len)); o.start(time); this.#track([o], time + len + 0.03);
    };
    const noise = (fType, freq, q, peak, len, flam) => {
      const n = ctx.createBufferSource(), f = ctx.createBiquadFilter();
      n.buffer = this.#noiseBuf(len + 0.03); f.type = fType; f.frequency.setValueAtTime(freq, time); if (q) f.Q.setValueAtTime(q, time);
      const g = gain(peak, time, len);
      if (flam) { g.gain.setValueAtTime(0.4 * peak * v, time); g.gain.setValueAtTime(0.7 * peak * v, time + 0.012); g.gain.setValueAtTime(peak * v, time + 0.024); g.gain.exponentialRampToValueAtTime(0.001, time + len); }
      n.connect(f); f.connect(g); n.start(time); this.#track([n], time + len + 0.03);
    };
    switch (gm) {
      case 35: case 36: tone('sine', 160, 45, 0.12, 1, 0.32); break;               // kick: a fast pitch drop
      case 38: noise('bandpass', 1800, 1.2, 0.8, 0.18); tone('triangle', 220, 160, 0.05, 0.45, 0.1); break; // snare: noise and a body
      case 39: noise('bandpass', 1100, 2.5, 1, 0.22, true); break;                  // clap: three quick bursts
      case 42: noise('highpass', 7500, 0, 0.5, 0.06); break;                        // closed hat
      case 46: noise('highpass', 6000, 0, 0.65, 0.28); break;                       // open hat
      case 49: noise('highpass', 4500, 0, 0.6, 1.2); break;                         // crash
      case 45: tone('sine', 140, 80, 0.2, 0.9, 0.35); break;                        // toms: low, mid, high
      case 47: tone('sine', 190, 115, 0.18, 0.85, 0.3); break;
      case 50: tone('sine', 260, 160, 0.16, 0.8, 0.26); break;
      case 37: tone('square', 950, 420, 0.03, 0.5, 0.045); break;                   // rim
      default: noise('bandpass', 4200, 3, 0.35, 0.05);                              // shaker / other perc
    }
  }
}
