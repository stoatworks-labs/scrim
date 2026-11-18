/**
 * SW Scrim's CPU half, ported by hand from the C++ to JavaScript.
 *
 * The plugin is one GLSL pass (demo/shaders.js, spliced from Shaders.cpp and
 * held to it by demo/tools/check_shaders.py) driven by numbers the CPU works
 * out once a frame. Those numbers are this file:
 *
 * - Names.h: the id and the display name.
 * - Controls.h and Controls.cpp: every conversion from the host's 0..1 to the
 *   plugin's units, and the defaults' constants.
 * - Model.h and Model.cpp: Asked, Setup, ThreadFraction and Plan -- the lights'
 *   gains, the spill's magnifications and disc, the weave's frequencies and the
 *   split of its phase into an exact 8-bit part and a remainder. Plan computes
 *   in double, and a JavaScript number IS a double, so the port computes in
 *   the same precision with the same operations in the same order; only the
 *   uniform upload rounds to float, as the plugin's static_cast< float > does.
 *   The fault branches are ported too, so port_numbers.py can compare them;
 *   the page never sets one (Fault is 0, as shipped).
 * - Scrim.h and Scrim.cpp: the parameter indices, the constructor's defaults
 *   (as floats: params[] is a Float32Array, so a value is rounded as the host's
 *   float is), asked(), SetFloatParameter, and ProcessOpenGL -- the guards,
 *   inputTextures[0] as Dest (the scene behind) and [1] as Src (the
 *   projection), and every uniform -- with GLState.h's bindUnit and
 *   unbindTextureUnits.
 *
 * Not ported: the log (Diag.cpp), the About block, the harness's negative
 * controls and shader override, InitGL's log lines. demo/tools/crosscheck/
 * compares this port's pictures with smtest's, frame for frame, and
 * port_numbers.py its numbers with the C++'s; past those, only a reader checks
 * it. No DOM here: node imports this file for port_numbers.py.
 */
import { Program } from './vendor/gl.js';
import * as S from './shaders.js';

const f32 = Math.fround;
const clamp = (value, lo, hi) => Math.min(hi, Math.max(lo, value));

// ==== PORT: Names.h ==========================================================

export const kMixerId = 'SM01';
export const kMixerName = 'SW Scrim';

// ==== PORT: Controls.h and Controls.cpp ======================================

export const kPi = 3.14159265358979323846;
export const kLightMax = 16.0;
export const kFrontDefault = 12.2665;
export const kBackDefault = 8.0279;
export const kFillMin = 0.5;
export const kFillDefault = 0.92;
export const kSpillMax = 3.0;
export const kSpillDefault = 0.5;
export const kAudienceMin = 0.25;
export const kAudienceRange = 64.0;
export const kAudienceDefault = 4.0;
export const kOpenDefault = 0.535;
export const kThreadDefault = 0.8822;
export const kMeshMinMm = 0.5;
export const kMeshRange = 20.0;
export const kMeshDefaultMm = 2.5;
export const kWidthMinM = 2.0;
export const kWidthRange = 10.0;
export const kWidthDefaultM = 9.5;

/** Controls.cpp's unit(): the host's float, as a double, clamped to 0..1. */
const unit = (value) => clamp(value, 0.0, 1.0);
/** Controls.cpp's toParam(): back to the host's float. */
const toParam = (p) => f32(clamp(p, 0.0, 1.0));

export function LightFromParam(value) {
  const p = unit(value);
  return kLightMax * p * p;
}
export const ParamForLight = (light) => toParam(Math.sqrt(Math.max(light, 0.0) / kLightMax));

export const FillFromParam = (value) => kFillMin + (1.0 - kFillMin) * unit(value);
export const ParamForFill = (fill) => toParam((fill - kFillMin) / (1.0 - kFillMin));

export const SpillFromParam = (value) => kSpillMax * unit(value);
export const ParamForSpill = (k) => toParam(k / kSpillMax);

export const AudienceFromParam = (value) => kAudienceMin * Math.pow(kAudienceRange, unit(value));
export const ParamForAudience = (v) => toParam(Math.log(Math.max(v, 1e-9) / kAudienceMin) / Math.log(kAudienceRange));

export const MeshFromParam = (value) => kMeshMinMm * Math.pow(kMeshRange, unit(value));
export const ParamForMesh = (mm) => toParam(Math.log(Math.max(mm, 1e-9) / kMeshMinMm) / Math.log(kMeshRange));

export const WidthFromParam = (value) => kWidthMinM * Math.pow(kWidthRange, unit(value));
export const ParamForWidth = (metres) => toParam(Math.log(Math.max(metres, 1e-9) / kWidthMinM) / Math.log(kWidthRange));

export const DecodeSrgb = (c) => (c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4));
export const EncodeSrgb = (x) => (x <= 0.0031308 ? 12.92 * x : 1.055 * Math.pow(x, 1.0 / 2.4) - 0.055);

// ==== PORT: Model.h and Model.cpp ============================================

export const Fault = {
  kFaultNone: 0,
  kFaultOpacity: 1 << 0,
  kFaultSwapArea: 1 << 1,
  kFaultBackAlways: 1 << 2,
  kFaultWashSpills: 1 << 3,
  kFaultGammaSum: 1 << 4,
  kFaultViewerScale: 1 << 5,
  kFaultNoSpread: 1 << 6,
  kFaultBlurOnSet: 1 << 7,
  kFaultBoxAperture: 1 << 8,
  kFaultSquareCell: 1 << 9,
  kFaultStraight: 1 << 10,
  kFaultHardwareBilinear: 1 << 11,
  kFaultAlphaFromInput: 1 << 12,
  kFaultLeaveBound: 1 << 13,
  kFaultPadded: 1 << 14,
  kFaultStaleSize: 1 << 15,
  kFaultClock: 1 << 16,
};

/** The lens's entrance pupil, mm: a PT-RZ120's standard zoom, wide end. */
export const kPupilMm = 25.6 / 1.7;
export const kMaxRadiusPx = 16.0;
export const kPointRadiusPx = 1.0 / 64.0;
export const kMaxSize = 16384;

/** Model.h's Asked, with its member defaults. */
export function Asked() {
  return {
    linear: true,
    opacity: 1.0,
    front: 0.0,
    fill: 0.92,
    back: 0.0,
    wash: 0.0,
    spill: 0.5,
    audience: 4.0,
    open: 0.535,
    thread: [0.8822, 0.8822, 0.8822],
    meshMm: 2.5,
    drawMesh: true,
    widthM: 9.5,
  };
}

export function ThreadFraction(phi) {
  // 2 a^2 - 3 a + ( 1 - phi ) = 0, the root in [0, 1/2].
  return (3.0 - Math.sqrt(1.0 + 8.0 * clamp(phi, 0.0, 1.0))) / 4.0;
}

/** Plan, in double as in the C++ (a JavaScript number is a double). */
export function Plan(asked, width, fault = 0, seconds = 0.0) {
  const s = {
    asked,
    reflect: [0, 0, 0],
    frontGain: 0,
    back: 0,
    wash: 0,
    spillGain: 0,
    magnification: 1,
    apparent: 1,
    invApparent: 1,
    radiusPx: 0,
    radiusCapped: false,
    pitchPx: 0,
    freq: [0, 0],
    freqHi: [0, 0],
    freqLo: [0, 0],
    openAxis: [0, 0],
    aperture: 1,
  };
  const phi = clamp(asked.open, 0.0, 1.0);

  for (let c = 0; c < 3; c += 1) {
    const picked = clamp(asked.thread[c], 0.0, 1.0);
    s.reflect[c] = asked.linear ? DecodeSrgb(picked) : picked;
  }

  s.frontGain = asked.opacity * asked.front;
  s.back = asked.back + ((fault & Fault.kFaultBackAlways) ? 1.0 / 64.0 : 0.0);
  s.wash = asked.wash;

  // The spill: similar triangles from the lens through the gauze to the set.
  const k = Math.max(asked.spill, 0.0);
  const v = Math.max(asked.audience, 1e-6);
  s.magnification = 1.0 + k;
  s.apparent = (1.0 + k) / (1.0 + k / v);
  s.invApparent = 1.0 / ((fault & Fault.kFaultViewerScale) ? s.magnification : s.apparent);
  const spread = (fault & Fault.kFaultNoSpread) ? 1.0 : s.magnification * s.magnification;
  s.spillGain = phi * s.frontGain / spread;

  // The defocus disc, referred to the gauze.
  const pupilM = kPupilMm / 1000.0;
  const widthM = Math.max(asked.widthM, 1e-6);
  const onGauze = pupilM * k / (2.0 * (1.0 + k));
  const onSet = pupilM * k / 2.0;
  const radiusM = (fault & Fault.kFaultBlurOnSet) ? onSet : onGauze;
  const radius = radiusM / widthM * width;
  s.radiusCapped = radius > kMaxRadiusPx;
  s.radiusPx = Math.min(radius, kMaxRadiusPx);

  // The weave: a cell Mesh wide and Mesh / 2 tall, threads t wide both ways.
  let mesh = Math.max(asked.meshMm, 1e-6);
  if (fault & Fault.kFaultClock) mesh *= 1.0 + 1e-4 * seconds;
  s.pitchPx = mesh / 1000.0 / widthM * width;
  s.freq[0] = 1.0 / s.pitchPx;
  s.freq[1] = (fault & Fault.kFaultSquareCell) ? s.freq[0] : 2.0 * s.freq[0];
  const a = ThreadFraction(phi);
  s.openAxis[0] = 1.0 - a;
  s.openAxis[1] = 1.0 - 2.0 * a;
  for (let axis = 0; axis < 2; axis += 1) {
    // frac( f ) split into an 8-bit part, taken exactly in integers by the
    // shader, and the rest (under 1/256).
    const g = s.freq[axis] - Math.floor(s.freq[axis]);
    s.freqHi[axis] = clamp(Math.trunc(Math.floor(g * 256.0)), 0, 255);
    s.freqLo[axis] = g - s.freqHi[axis] / 256.0;
  }
  s.aperture = (fault & Fault.kFaultBoxAperture) ? 1.0 : Math.sqrt(clamp(asked.fill, 0.0, 1.0));
  return s;
}

// ==== PORT: Scrim.h and Scrim.cpp ============================================

/** Scrim::ParamID, up to the About block (which the page does not have). */
export const PT = {
  LINEAR_LIGHT: 0,
  OPACITY: 1,
  FRONT_LIGHT: 2,
  PIXEL_FILL: 3,
  BACK_LIGHT: 4,
  WASH: 5,
  SPILL_DISTANCE: 6,
  AUDIENCE: 7,
  OPEN_AREA: 8,
  THREAD_RED: 9,
  THREAD_GREEN: 10,
  THREAD_BLUE: 11,
  MESH: 12,
  DRAW_MESH: 13,
  GAUZE_WIDTH: 14,
  ABOUT_FIRST: 15,
};

/** The constructor's defaults, as the host is told them: floats. */
export function ScrimDefaults() {
  const p = new Float32Array(PT.ABOUT_FIRST);
  p[PT.LINEAR_LIGHT] = 1.0;
  p[PT.OPACITY] = 1.0;
  p[PT.FRONT_LIGHT] = ParamForLight(kFrontDefault);
  p[PT.PIXEL_FILL] = ParamForFill(kFillDefault);
  p[PT.BACK_LIGHT] = ParamForLight(kBackDefault);
  p[PT.WASH] = ParamForLight(0.0);
  p[PT.SPILL_DISTANCE] = ParamForSpill(kSpillDefault);
  p[PT.AUDIENCE] = ParamForAudience(kAudienceDefault);
  p[PT.OPEN_AREA] = f32(kOpenDefault);
  p[PT.THREAD_RED] = f32(kThreadDefault);
  p[PT.THREAD_GREEN] = f32(kThreadDefault);
  p[PT.THREAD_BLUE] = f32(kThreadDefault);
  p[PT.MESH] = ParamForMesh(kMeshDefaultMm);
  p[PT.DRAW_MESH] = 1.0;
  p[PT.GAUZE_WIDTH] = ParamForWidth(kWidthDefaultM);
  return p;
}

/** The constructor's declaration, in index order: name, FF type, group. */
export const SCRIM_TABLE = [
  [PT.LINEAR_LIGHT, 'Linear Light', 'boolean', 'Light'],
  [PT.OPACITY, 'Opacity', 'standard', 'Projector'],
  [PT.FRONT_LIGHT, 'Front Light', 'standard', 'Projector'],
  [PT.PIXEL_FILL, 'Pixel Fill', 'standard', 'Projector'],
  [PT.BACK_LIGHT, 'Back Light', 'standard', 'Stage'],
  [PT.WASH, 'Wash', 'standard', 'Stage'],
  [PT.SPILL_DISTANCE, 'Spill Distance', 'standard', 'Stage'],
  [PT.AUDIENCE, 'Audience', 'standard', 'Stage'],
  [PT.OPEN_AREA, 'Open Area', 'standard', 'Gauze'],
  [PT.THREAD_RED, 'Thread', 'red', 'Gauze'],
  [PT.THREAD_GREEN, 'Thread_Green', 'green', 'Gauze'],
  [PT.THREAD_BLUE, 'Thread_Blue', 'blue', 'Gauze'],
  [PT.MESH, 'Mesh', 'standard', 'Gauze'],
  [PT.DRAW_MESH, 'Draw Mesh', 'boolean', 'Gauze'],
  [PT.GAUZE_WIDTH, 'Gauze Width', 'standard', 'Gauze'],
].map(([slot, name, kind, group]) => ({ slot, name, kind, group }));

if (SCRIM_TABLE.length !== PT.ABOUT_FIRST || SCRIM_TABLE.some((row, i) => row.slot !== i)) {
  throw new Error('the parameter table is not in index order');
}

/** InitGL: AssembleVertex() = kVersion + kVertex, AssembleFragment( pass ) = kVersion + kCommon + pass. */
export const AssembleVertex = () => S.VERSION + S.VERTEX;
export const AssembleFragment = (pass) => S.VERSION + S.COMMON + pass;

export function compileScreen(gl) {
  return new Program(gl, AssembleVertex(), AssembleFragment(S.SCREEN_FRAGMENT), 'screen');
}

/** glUniform2i by name, counted as missing the way the kit's setters count. */
function setIvec2(program, name, a, b) {
  const location = program.location(name);
  if (location === null) {
    program.missing.add(name);
    return;
  }
  program.gl.uniform2i(location, a | 0, b | 0);
}

/** GLState.h's bindUnit. */
function bindUnit(gl, unit, texture) {
  gl.activeTexture(gl.TEXTURE0 + unit);
  gl.bindTexture(gl.TEXTURE_2D, texture);
}

/** GLState.h's unbindTextureUnits: units count-1 .. 0, leaving 0 active. */
function unbindTextureUnits(gl, count) {
  for (let u = count - 1; u >= 0; u -= 1) {
    gl.activeTexture(gl.TEXTURE0 + u);
    gl.bindTexture(gl.TEXTURE_2D, null);
  }
}

const tooBig = (t) => t.width > kMaxSize || t.height > kMaxSize;

/**
 * One SW Scrim instance. An input is { texture, width, height } and may carry
 * hwWidth/hwHeight; the page's textures are unpadded, so the hardware size is
 * the used size.
 */
export class ScrimInstance {
  constructor(gl, quad, program) {
    this.gl = gl;
    this.quad = quad;
    this.program = program;
    // The plugin's float params[]: a Float32Array rounds as the host's float does.
    this.params = ScrimDefaults();
    this.lastSetup = null;
  }

  /** The About block's indices are not here: a web page has links of its own. */
  SetFloatParameter(index, value) {
    if (index < 0 || index >= PT.ABOUT_FIRST) return false;
    this.params[index] = value;
    return true;
  }

  asked() {
    const p = this.params;
    const a = Asked();
    a.linear = p[PT.LINEAR_LIGHT] > 0.5;
    a.opacity = clamp(p[PT.OPACITY], 0.0, 1.0);
    a.front = LightFromParam(p[PT.FRONT_LIGHT]);
    a.fill = FillFromParam(p[PT.PIXEL_FILL]);
    a.back = LightFromParam(p[PT.BACK_LIGHT]);
    a.wash = LightFromParam(p[PT.WASH]);
    a.spill = SpillFromParam(p[PT.SPILL_DISTANCE]);
    a.audience = AudienceFromParam(p[PT.AUDIENCE]);
    a.open = clamp(p[PT.OPEN_AREA], 0.0, 1.0);
    a.thread = [p[PT.THREAD_RED], p[PT.THREAD_GREEN], p[PT.THREAD_BLUE]];
    a.meshMm = MeshFromParam(p[PT.MESH]);
    a.drawMesh = p[PT.DRAW_MESH] > 0.5;
    a.widthM = WidthFromParam(p[PT.GAUZE_WIDTH]);
    return a;
  }

  /**
   * ProcessOpenGL. `dest` is inputTextures[0] (the layer below: the scene
   * behind), `src` is inputTextures[1] (this layer: the projection). The
   * host's framebuffer is `host` (null: the canvas), already at the output's
   * viewport, outW x outH. Returns false where the plugin returns FF_FAIL.
   */
  ProcessOpenGL(dest, src, outW, outH, host = null) {
    const gl = this.gl;
    const screen = this.program;
    if (!dest || !src) return false;
    const destHw = [dest.hwWidth ?? dest.width, dest.hwHeight ?? dest.height];
    const srcHw = [src.hwWidth ?? src.width, src.hwHeight ?? src.height];
    if (!dest.width || !dest.height || destHw[0] < dest.width || destHw[1] < dest.height
      || !src.width || !src.height || srcHw[0] < src.width || srcHw[1] < src.height) return false;
    if (outW <= 0 || outH <= 0) return false;
    if (outW > kMaxSize || outH > kMaxSize || tooBig(dest) || tooBig(src)) return false;

    // The frame's numbers, in double.
    const s = Plan(this.asked(), outW, 0, 0.0);
    this.lastSetup = s;

    gl.disable(gl.BLEND);

    gl.bindFramebuffer(gl.FRAMEBUFFER, host);
    screen.use();
    bindUnit(gl, 0, dest.texture);
    bindUnit(gl, 1, src.texture);
    screen.setSampler('Stage', 0);
    screen.setSampler('Projection', 1);

    setIvec2(screen, 'OutSize', outW, outH);
    setIvec2(screen, 'StageSize', dest.width, dest.height);
    setIvec2(screen, 'ProjectionSize', src.width, src.height);
    setIvec2(screen, 'StageHardware', destHw[0], destHw[1]);
    setIvec2(screen, 'ProjectionHardware', srcHw[0], srcHw[1]);
    screen.setInt('Linear', s.asked.linear ? 1 : 0);
    screen.set('Reflect', s.reflect[0], s.reflect[1], s.reflect[2]);
    screen.set('Open', s.asked.open);
    screen.set('Front', s.frontGain);
    screen.set('Wash', s.wash);
    screen.set('Back', s.back);
    screen.set('SpillGain', s.spillGain);
    screen.set('InvApparent', s.invApparent);
    screen.set('Radius', s.radiusPx);
    screen.setInt('DrawMesh', s.asked.drawMesh ? 1 : 0);
    setIvec2(screen, 'FreqHi', s.freqHi[0], s.freqHi[1]);
    screen.set('FreqLo', s.freqLo[0], s.freqLo[1]);
    screen.set('Freq', s.freq[0], s.freq[1]);
    screen.set('OpenAxis', s.openAxis[0], s.openAxis[1]);
    screen.set('Aperture', s.aperture);
    screen.setInt('Fault', 0);

    this.quad.draw();

    // Leave nothing bound.
    unbindTextureUnits(gl, 2);
    gl.useProgram(null);
    return true;
  }
}

// ==== END PORT ===============================================================
