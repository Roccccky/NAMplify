// Erzeugt von tests/MockDefsTest.cpp aus dem Kern: nicht von Hand ändern.
// Neu schreiben: NAMPLIFY_WRITE_MOCK=1 build/tests/namplify_tests MockDefs

export const LIMITS = { slots: 16, laneSlots: 12, lanes: 2, slotControls: 11 };

/// Die festen Parameter, in der Reihenfolge der Parameterliste.
export const FIXED = [
  { id: "in_gain", name: "Input", kind: "linear", min: -24, max: 24, def: 0, unit: "dB" },
  { id: "out_gain", name: "Output", kind: "linear", min: -24, max: 24, def: 0, unit: "dB" },
  { id: "mute", name: "Mute", kind: "bool", min: 0, max: 1, def: 0, unit: "" },
  { id: "tuner", name: "Tuner", kind: "bool", min: 0, max: 1, def: 0, unit: "" },
  { id: "spread_ms", name: "Spread", kind: "linear", min: 1, max: 25, def: 1, unit: "ms" },
  { id: "drums_on", name: "Drums", kind: "bool", min: 0, max: 1, def: 0, unit: "" },
  { id: "drums_mode", name: "Drums Mode", kind: "choice", min: 0, max: 1, def: 1, unit: "", choices: ["BEAT", "CLICK"] },
  { id: "drums_pattern", name: "Drums Pattern", kind: "choice", min: 0, max: 5, def: 0, unit: "", choices: ["1", "2", "3", "4", "5", "6"] },
  { id: "drums_click", name: "Click Pattern", kind: "choice", min: 0, max: 3, def: 0, unit: "", choices: ["1", "2", "3", "4"] },
  { id: "drums_bpm", name: "Drums Tempo", kind: "linear", min: 60, max: 200, def: 90, unit: "BPM" },
  { id: "drums_volume", name: "Drums Volume", kind: "linear", min: 0, max: 1, def: 0.7, unit: "" },
  { id: "looper_on", name: "Looper", kind: "bool", min: 0, max: 1, def: 0, unit: "" },
  { id: "recorder_on", name: "Recorder", kind: "bool", min: 0, max: 1, def: 0, unit: "" },
  { id: "midi_channel", name: "MIDI Channel", kind: "choice", min: 0, max: 16, def: 0, unit: "", choices: ["OMNI", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"] },
  { id: "midi_program_change", name: "MIDI Program Change", kind: "bool", min: 0, max: 1, def: 0, unit: "" },
  { id: "midi_thru", name: "MIDI Thru", kind: "bool", min: 0, max: 1, def: 0, unit: "" },
  { id: "sync_drums", name: "Sync Drums", kind: "bool", min: 0, max: 1, def: 0, unit: "" },
  { id: "sync_looper", name: "Sync Looper", kind: "bool", min: 0, max: 1, def: 0, unit: "" },
  { id: "dual_on", name: "Dual", kind: "bool", min: 0, max: 1, def: 0, unit: "" },
  { id: "lane_b_source", name: "Lane B Source", kind: "choice", min: 0, max: 2, def: 0, unit: "", choices: ["PARALLEL", "SPLIT", "STEREO"] },
  { id: "split_after", name: "Split After", kind: "choice", min: 0, max: 12, def: 0, unit: "", choices: ["IN", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12"] },
  { id: "merge_after", name: "Merge After", kind: "choice", min: 0, max: 12, def: 12, unit: "", choices: ["IN", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "OUT"] },
  { id: "lane_a_level", name: "Lane A Level", kind: "linear", min: -24, max: 12, def: 0, unit: "dB" },
  { id: "lane_a_pan", name: "Lane A Pan", kind: "linear", min: -1, max: 1, def: 0, unit: "" },
  { id: "lane_a_solo", name: "Lane A Solo", kind: "bool", min: 0, max: 1, def: 0, unit: "" },
  { id: "lane_a_invert", name: "Lane A Phase", kind: "bool", min: 0, max: 1, def: 0, unit: "" },
  { id: "lane_b_level", name: "Lane B Level", kind: "linear", min: -24, max: 12, def: 0, unit: "dB" },
  { id: "lane_b_pan", name: "Lane B Pan", kind: "linear", min: -1, max: 1, def: 0, unit: "" },
  { id: "lane_b_solo", name: "Lane B Solo", kind: "bool", min: 0, max: 1, def: 0, unit: "" },
  { id: "lane_b_invert", name: "Lane B Phase", kind: "bool", min: 0, max: 1, def: 0, unit: "" },
];

export const TYPES = [
  { key: "GATE", name: "GATE", colour: "#8d99a8", limit: 2, takesFiles: false, controls: [
      { id: "gate_threshold", name: "Threshold", kind: "linear", min: -90, max: 0, def: -70, unit: "dB" },
  ] },
  { key: "COMP", name: "COMP", colour: "#46b4e6", limit: 2, takesFiles: false, controls: [
      { id: "comp_sustain", name: "Sustain", kind: "linear", min: 0, max: 1, def: 0.5, unit: "" },
      { id: "comp_level", name: "Level", kind: "linear", min: -12, max: 12, def: 0, unit: "dB" },
  ] },
  { key: "DRIVE", name: "DRIVE", colour: "#5fb03a", limit: 2, takesFiles: false, controls: [
      { id: "drive_model", name: "Model", kind: "choice", min: 0, max: 3, def: 0, unit: "", choices: ["SCREAM", "WARM", "CLEAN", "CRUSH"] },
      { id: "drive_gain", name: "Gain", kind: "linear", min: 0, max: 36, def: 14, unit: "dB" },
      { id: "drive_level", name: "Level", kind: "linear", min: -12, max: 12, def: 4, unit: "dB" },
      { id: "drive_tone", name: "Tone", kind: "linear", min: 0, max: 1, def: 0.55, unit: "" },
      { id: "drive_body", name: "Body", kind: "linear", min: 0, max: 1, def: 0.5, unit: "" },
  ] },
  { key: "NAM", name: "NAM", colour: "#e04a3a", limit: 4, takesFiles: true, controls: [
      { id: "nam_gain", name: "Gain", kind: "linear", min: -24, max: 24, def: 0, unit: "dB", roles: ["amp", "pedal", "outboard", "fullrig", "space"] },
      { id: "nam_level", name: "Level", kind: "linear", min: -12, max: 12, def: 0, unit: "dB", roles: ["amp", "pedal", "outboard", "fullrig", "space"] },
      { id: "nam_mix", name: "Mix", kind: "linear", min: 0, max: 1, def: 1, unit: "", roles: ["pedal", "outboard", "space"] },
      { id: "amp_bass", name: "Bass", kind: "linear", min: -12, max: 12, def: 0, unit: "dB", roles: ["amp", "fullrig"] },
      { id: "amp_mid", name: "Mid", kind: "linear", min: -12, max: 12, def: 0, unit: "dB", roles: ["amp", "fullrig"] },
      { id: "amp_treble", name: "Treble", kind: "linear", min: -12, max: 12, def: 0, unit: "dB", roles: ["amp", "fullrig"] },
      { id: "amp_norm", name: "Output", kind: "choice", min: 0, max: 2, def: 2, unit: "", choices: ["RAW", "CALIBRATED", "NORMALIZED"], roles: ["amp", "outboard", "fullrig", "space"] },
  ] },
  { key: "EQ", name: "EQ", colour: "#c4ccd6", limit: 2, takesFiles: false, controls: [
      { id: "eq_mode", name: "Mode", kind: "choice", min: 0, max: 1, def: 0, unit: "", choices: ["3 BAND", "7 BAND"] },
      { id: "eq3_bass", name: "Bass", kind: "linear", min: -12, max: 12, def: 0, unit: "dB" },
      { id: "eq3_mid", name: "Mid", kind: "linear", min: -12, max: 12, def: 0, unit: "dB" },
      { id: "eq3_treble", name: "Treble", kind: "linear", min: -12, max: 12, def: 0, unit: "dB" },
      { id: "eq7_100", name: "100", kind: "linear", min: -12, max: 12, def: 0, unit: "dB" },
      { id: "eq7_200", name: "200", kind: "linear", min: -12, max: 12, def: 0, unit: "dB" },
      { id: "eq7_400", name: "400", kind: "linear", min: -12, max: 12, def: 0, unit: "dB" },
      { id: "eq7_800", name: "800", kind: "linear", min: -12, max: 12, def: 0, unit: "dB" },
      { id: "eq7_1600", name: "1.6K", kind: "linear", min: -12, max: 12, def: 0, unit: "dB" },
      { id: "eq7_3200", name: "3.2K", kind: "linear", min: -12, max: 12, def: 0, unit: "dB" },
      { id: "eq7_6400", name: "6.4K", kind: "linear", min: -12, max: 12, def: 0, unit: "dB" },
  ] },
  { key: "IR", name: "IR", colour: "#e8c34a", limit: 4, takesFiles: true, controls: [
      { id: "cab_output", name: "Output", kind: "linear", min: -12, max: 12, def: 0, unit: "dB" },
  ] },
  { key: "CHORUS", name: "CHORUS", colour: "#4a86e8", limit: 2, takesFiles: false, controls: [
      { id: "chorus_rate", name: "Rate", kind: "log", min: 0.1, max: 5, def: 0.8, unit: "Hz" },
      { id: "chorus_depth", name: "Depth", kind: "linear", min: 0, max: 1, def: 0.5, unit: "" },
      { id: "chorus_mix", name: "Mix", kind: "linear", min: 0, max: 1, def: 0.35, unit: "" },
  ] },
  { key: "FLANGER", name: "FLANGER", colour: "#8a6ae8", limit: 2, takesFiles: false, controls: [
      { id: "flanger_rate", name: "Rate", kind: "log", min: 0.02, max: 4, def: 0.35, unit: "Hz" },
      { id: "flanger_depth", name: "Depth", kind: "linear", min: 0, max: 1, def: 0.7, unit: "" },
      { id: "flanger_feedback", name: "Feedback", kind: "linear", min: -0.9, max: 0.9, def: 0.6, unit: "" },
      { id: "flanger_manual", name: "Manual", kind: "linear", min: 0, max: 1, def: 0.3, unit: "" },
      { id: "flanger_mix", name: "Mix", kind: "linear", min: 0, max: 1, def: 0.7, unit: "" },
  ] },
  { key: "PHASER", name: "PHASER", colour: "#f0912a", limit: 2, takesFiles: false, controls: [
      { id: "phaser_rate", name: "Rate", kind: "log", min: 0.05, max: 2, def: 0.5, unit: "Hz" },
      { id: "phaser_depth", name: "Depth", kind: "linear", min: 0, max: 1, def: 0.7, unit: "" },
      { id: "phaser_mix", name: "Mix", kind: "linear", min: 0, max: 1, def: 0.7, unit: "" },
  ] },
  { key: "TREMOLO", name: "TREMOLO", colour: "#e56aa8", limit: 2, takesFiles: false, controls: [
      { id: "tremolo_rate", name: "Rate", kind: "linear", min: 1, max: 12, def: 5, unit: "Hz" },
      { id: "tremolo_depth", name: "Depth", kind: "linear", min: 0, max: 1, def: 0.6, unit: "" },
      { id: "sync_tremolo", name: "Sync", kind: "bool", min: 0, max: 1, def: 0, unit: "" },
      { id: "sync_tremolo_div", name: "Division", kind: "choice", min: 0, max: 8, def: 6, unit: "", choices: ["1/1", "1/2", "1/4.", "1/4", "1/4T", "1/8.", "1/8", "1/8T", "1/16."] },
  ] },
  { key: "DELAY", name: "DELAY", colour: "#4e9f72", limit: 2, takesFiles: false, controls: [
      { id: "delay_type", name: "Type", kind: "choice", min: 0, max: 3, def: 0, unit: "", choices: ["TAPE", "ANALOG", "DIGITAL", "PING"] },
      { id: "delay_time", name: "Time", kind: "log", min: 20, max: 2000, def: 375, unit: "ms" },
      { id: "delay_feedback", name: "Feedback", kind: "linear", min: 0, max: 0.95, def: 0.3, unit: "" },
      { id: "delay_mix", name: "Mix", kind: "linear", min: 0, max: 1, def: 0.25, unit: "" },
      { id: "sync_delay", name: "Sync", kind: "bool", min: 0, max: 1, def: 0, unit: "" },
      { id: "sync_delay_div", name: "Division", kind: "choice", min: 0, max: 8, def: 3, unit: "", choices: ["1/1", "1/2", "1/4.", "1/4", "1/4T", "1/8.", "1/8", "1/8T", "1/16."] },
  ] },
  { key: "REVERB", name: "REVERB", colour: "#3fc3e0", limit: 2, takesFiles: false, controls: [
      { id: "reverb_type", name: "Type", kind: "choice", min: 0, max: 3, def: 2, unit: "", choices: ["ROOM", "HALL", "PLATE", "SPRING"] },
      { id: "reverb_time", name: "Time", kind: "log", min: 0.2, max: 12, def: 1.8, unit: "s" },
      { id: "reverb_size", name: "Size", kind: "linear", min: 0, max: 1, def: 0.45, unit: "" },
      { id: "reverb_boing", name: "Boing", kind: "linear", min: 0, max: 1, def: 0.62, unit: "" },
      { id: "reverb_tone", name: "Tone", kind: "linear", min: 0, max: 1, def: 0.72, unit: "" },
      { id: "reverb_mix", name: "Mix", kind: "linear", min: 0, max: 1, def: 0.18, unit: "" },
  ] },
];

export const ROLES = [
  { key: "amp", name: "AMP", colour: "#e04a3a" },
  { key: "pedal", name: "PEDAL", colour: "#2fbf9c" },
  { key: "outboard", name: "OUTBOARD", colour: "#b25ad0" },
  { key: "fullrig", name: "AMP+CAB", colour: "#ea6b2f" },
  { key: "space", name: "SPACE", colour: "#6f7fe0" },
];

export const TOOLS = [
  { key: "LOOPER", name: "LOOPER", colour: "#d9d9d9" },
  { key: "DRUMS", name: "BEATMACHINE", colour: "#949aa3" },
  { key: "RECORDER", name: "RECORDER", colour: "#d24a3c" },
];

export const DRIVE_DEFAULTS = [
  { name: "SCREAM", gain: 14, level: 4, tone: 0.55, body: 0.5 },
  { name: "WARM", gain: 12, level: -0.2, tone: 0.5, body: 0.45 },
  { name: "CLEAN", gain: 9, level: -4.3, tone: 0.62, body: 0.35 },
  { name: "CRUSH", gain: 22, level: -2.5, tone: 0.45, body: 0.6 },
];

export const REVERB_DEFAULTS = [
  { name: "ROOM", time: 0.9, size: 0.35, boing: 0, tone: 0.62, mix: 0.3 },
  { name: "HALL", time: 2.6, size: 0.55, boing: 0, tone: 0.5, mix: 0.22 },
  { name: "PLATE", time: 1.8, size: 0.45, boing: 0, tone: 0.72, mix: 0.18 },
  { name: "SPRING", time: 2.2, size: 0, boing: 0.62, tone: 0.3, mix: 0.32 },
];

/// Der Werksaufbau: welcher Slot welchen Typ hat und wo er steht.
export const DEFAULT_LAYOUT = [
];
