# DrumMachineTool

Two-voice drum synthesizer and 8-step sequencer for the Daisy Pod. The firmware targets the Daisy Seed and Pod control layout.

## Controls

- **Encoder turn:** Select step (0–7). Hold **Button 2** while turning to change internal BPM.
- **Encoder press:**
  - Tap (<200 ms): Preview the selected step.
  - Short hold (200–700 ms): Toggle the step trigger on/off.
  - Press while holding **Button 1:** Copy the previous step into the selected step.
- **Buttons:**
  - Button 1: OSC page (knobs edit osc pitch/decay). Long hold toggles internal/external MIDI clock.
  - Button 2: NOISE page (knobs edit noise tone/decay). Long hold toggles play/pause. Hold while turning encoder to set BPM when using the internal clock.
  - Buttons 1 + 2: LEVEL page (knobs edit osc/noise levels).
- **LEDs:** LED1 shows the active page (blue = OSC, red = NOISE, green = LEVEL). LED2 flashes with the clock/transport (orange for internal, cyan for external).

## Clocking

- Internal clock defaults to 130 BPM with 8th-note resolution.
- External MIDI clock is received over the Pod's UART MIDI input. START/STOP/CONTINUE messages control transport. Twelve MIDI clocks advance one sequencer step.

## Synthesis

- Voice 1: Sine oscillator with amplitude and pitch envelopes for kick/tom tones.
- Voice 2: White noise through an SVF filter with its own envelope for snare/noise hits.
- Per-step parameter locks store trig, pitch/decay, tone/decay, and level settings for both voices.
