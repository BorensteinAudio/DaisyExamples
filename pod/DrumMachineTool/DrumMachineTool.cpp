#include "daisy_pod.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;

constexpr size_t kNumSteps      = 8;
constexpr float  kDefaultBpm    = 130.0f;
constexpr float  kMinBpm        = 60.0f;
constexpr float  kMaxBpm        = 200.0f;
constexpr float  kPitchSweep    = 4.0f;
constexpr float  kTapThreshold  = 200.0f;  // ms
constexpr float  kHoldThreshold = 700.0f;  // ms

enum class Page
{
    Osc,
    Noise,
    Level,
};

enum class ClockSource
{
    Internal,
    External,
};

struct Step
{
    bool  trig;
    float osc_pitch_hz;
    float osc_decay_s;
    float noise_tone_hz;
    float noise_decay_s;
    float osc_level;
    float noise_level;
};

DaisyPod pod;
Oscillator osc;
WhiteNoise noise;
Svf        noise_filter;
AdEnv      osc_env;
AdEnv      noise_env;
AdEnv      pitch_env;

Page        active_page = Page::Osc;
ClockSource clock_src   = ClockSource::Internal;

Step  steps[kNumSteps];
int   selected_step = 0;
int   play_step     = kNumSteps - 1;
bool  transport_on  = true;
float bpm           = kDefaultBpm;
float external_bpm  = kDefaultBpm;

float active_osc_freq   = 60.0f;
float active_noise_freq = 3000.0f;
float active_osc_level  = 0.8f;
float active_noise_level = 0.6f;

Metro internal_clock;
int   midi_clock_ticks   = 0;
uint32_t last_midi_clock = 0;
float    clock_flash     = 0.0f;

void TriggerStep(size_t idx, bool force);
void AdvanceStep();
void UpdateInternalClock();
void ProcessControls();
void HandleMidi();

Color MakeColor(float r, float g, float b)
{
    Color c;
    c.Init(r, g, b);
    return c;
}

void InitSteps()
{
    for(size_t i = 0; i < kNumSteps; i++)
    {
        steps[i].trig          = true;
        steps[i].osc_pitch_hz  = 55.0f;
        steps[i].osc_decay_s   = 0.35f;
        steps[i].noise_tone_hz = 4000.0f;
        steps[i].noise_decay_s = 0.25f;
        steps[i].osc_level     = 0.8f;
        steps[i].noise_level   = (i % 2) ? 0.6f : 0.4f;
    }
}

void UpdateInternalClock()
{
    float freq = bpm / 30.0f; // 8th-notes at 2x bpm/60
    internal_clock.Init(freq, pod.AudioSampleRate());
}

void ResetTransport()
{
    play_step        = kNumSteps - 1;
    midi_clock_ticks = 0;
    last_midi_clock  = 0;
}

void HandleEncoder()
{
    int inc = pod.encoder.Increment();
    if(pod.button2.Pressed() && clock_src == ClockSource::Internal)
    {
        bpm += inc * 1.0f;
        bpm  = fclamp(bpm, kMinBpm, kMaxBpm);
        UpdateInternalClock();
    }
    else if(inc != 0)
    {
        selected_step += inc;
        while(selected_step < 0)
            selected_step += kNumSteps;
        selected_step %= kNumSteps;
    }

    if(pod.encoder.RisingEdge())
    {
        const uint32_t held = pod.encoder.TimeHeldMs();
        if(pod.button1.Pressed())
        {
            const int prev = (selected_step + kNumSteps - 1) % kNumSteps;
            steps[selected_step] = steps[prev];
        }
        else if(held < kTapThreshold)
        {
            TriggerStep(selected_step, true);
        }
        else if(held < kHoldThreshold)
        {
            steps[selected_step].trig = !steps[selected_step].trig;
        }
    }
}

void HandleButtons()
{
    if(pod.button1.RisingEdge())
    {
        if(pod.button1.TimeHeldMs() > 800)
        {
            clock_src = clock_src == ClockSource::Internal ? ClockSource::External
                                                            : ClockSource::Internal;
            ResetTransport();
            UpdateInternalClock();
        }
        else if(pod.button2.Pressed())
        {
            active_page = Page::Level;
        }
        else
        {
            active_page = Page::Osc;
        }
    }

    if(pod.button2.RisingEdge())
    {
        if(pod.button2.TimeHeldMs() > 800)
        {
            transport_on = !transport_on;
            if(transport_on)
            {
                ResetTransport();
            }
        }
        else if(pod.button1.Pressed())
        {
            active_page = Page::Level;
        }
        else
        {
            active_page = Page::Noise;
        }
    }
}

void HandleKnobs()
{
    const float k1 = pod.knob1.Process();
    const float k2 = pod.knob2.Process();

    Step &step = steps[selected_step];

    switch(active_page)
    {
        case Page::Osc:
            step.osc_pitch_hz = fmap(k1, 35.0f, 160.0f, Mapping::LOG);
            step.osc_decay_s  = fmap(k2, 0.05f, 1.5f, Mapping::LINEAR);
            break;
        case Page::Noise:
            step.noise_tone_hz = fmap(k1, 800.0f, 12000.0f, Mapping::LOG);
            step.noise_decay_s = fmap(k2, 0.04f, 1.2f, Mapping::LINEAR);
            break;
        case Page::Level:
            step.osc_level  = fmap(k1, 0.0f, 1.0f, Mapping::LINEAR);
            step.noise_level = fmap(k2, 0.0f, 1.0f, Mapping::LINEAR);
            break;
    }
}

void UpdateLeds()
{
    Color page_color;
    switch(active_page)
    {
        case Page::Osc: page_color = MakeColor(0.1f, 0.2f, 1.0f); break;
        case Page::Noise: page_color = MakeColor(1.0f, 0.2f, 0.1f); break;
        case Page::Level: page_color = MakeColor(0.1f, 1.0f, 0.1f); break;
    }
    pod.led1.SetColor(page_color);

    const float tempo      = clock_src == ClockSource::External ? external_bpm : bpm;
    const float tempo_norm = fclamp((tempo - kMinBpm) / (kMaxBpm - kMinBpm), 0.0f, 1.0f);
    const float base_pulse = transport_on ? 0.05f : 0.02f;
    const float pulse      = (base_pulse + 0.9f * clock_flash) * (0.5f + 0.5f * tempo_norm);
    if(clock_src == ClockSource::External)
    {
        pod.led2.SetColor(pulse * 0.3f, pulse * 0.8f, pulse);
    }
    else
    {
        pod.led2.SetColor(pulse * 0.6f, pulse, pulse * 0.2f);
    }

    clock_flash *= 0.9f;
    pod.UpdateLeds();
}

void ProcessControls()
{
    pod.ProcessAnalogControls();
    pod.ProcessDigitalControls();

    HandleMidi();
    HandleButtons();
    HandleEncoder();
    HandleKnobs();
    UpdateLeds();
}

void AdvanceStep()
{
    play_step = (play_step + 1) % kNumSteps;
    TriggerStep(play_step, false);
}

void TriggerStep(size_t idx, bool force)
{
    const Step &step = steps[idx];
    if(!step.trig && !force)
    {
        return;
    }

    active_osc_freq    = step.osc_pitch_hz;
    active_noise_freq  = step.noise_tone_hz;
    active_osc_level   = step.osc_level;
    active_noise_level = step.noise_level;

    osc_env.SetTime(ADENV_SEG_ATTACK, 0.001f);
    osc_env.SetTime(ADENV_SEG_DECAY, step.osc_decay_s);
    osc_env.SetMax(1.0f);
    osc_env.SetMin(0.0f);

    pitch_env.SetTime(ADENV_SEG_ATTACK, 0.0f);
    pitch_env.SetTime(ADENV_SEG_DECAY, step.osc_decay_s * 0.6f);
    pitch_env.SetMax(1.0f);
    pitch_env.SetMin(0.0f);

    noise_env.SetTime(ADENV_SEG_ATTACK, 0.001f);
    noise_env.SetTime(ADENV_SEG_DECAY, step.noise_decay_s);
    noise_env.SetMax(1.0f);
    noise_env.SetMin(0.0f);

    noise_filter.SetFreq(active_noise_freq);

    osc_env.Trigger();
    noise_env.Trigger();
    pitch_env.Trigger();
    clock_flash = 1.0f;
}

void HandleMidiClockTick()
{
    const uint32_t now = System::GetNow();
    if(last_midi_clock > 0)
    {
        const float delta_s = (now - last_midi_clock) / 1000.0f;
        if(delta_s > 0.0f)
        {
            const float clock_hz = 1.0f / delta_s;
            external_bpm        = clock_hz * 60.0f / 24.0f;
        }
    }
    last_midi_clock = now;

    if(clock_src != ClockSource::External || !transport_on)
    {
        return;
    }

    midi_clock_ticks++;
    if(midi_clock_ticks >= 12)
    {
        midi_clock_ticks = 0;
        AdvanceStep();
    }
}

void HandleMidi()
{
    pod.midi.Listen();
    while(pod.midi.HasEvents())
    {
        MidiEvent evt = pod.midi.PopEvent();
        if(evt.type == MidiMessageType::SystemRealTime)
        {
            auto rt = evt.AsSystemRealTime();
            switch(rt.type)
            {
                case SystemRealTimeType::CLOCK: HandleMidiClockTick(); break;
                case SystemRealTimeType::START:
                    transport_on = true;
                    ResetTransport();
                    break;
                case SystemRealTimeType::CONTINUE:
                    transport_on = true;
                    break;
                case SystemRealTimeType::STOP:
                    transport_on = false;
                    break;
                default: break;
            }
        }
    }
}

static void AudioCallback(AudioHandle::InterleavingInputBuffer  in,
                          AudioHandle::InterleavingOutputBuffer out,
                          size_t                                size)
{
    ProcessControls();

    if(clock_src == ClockSource::Internal && transport_on && internal_clock.Process())
    {
        AdvanceStep();
    }

    for(size_t i = 0; i < size; i += 2)
    {
        const float pitch_mod   = pitch_env.Process();
        const float env_osc     = osc_env.Process();
        const float env_noise   = noise_env.Process();
        const float osc_freq    = active_osc_freq * powf(2.0f, pitch_mod * kPitchSweep);
        float       osc_sample  = 0.0f;
        float       noise_sample = 0.0f;

        osc.SetFreq(osc_freq);
        osc_sample = osc.Process() * env_osc * active_osc_level;

        const float raw_noise = noise.Process();
        noise_filter.Process(raw_noise);
        noise_sample = noise_filter.Low() * env_noise * active_noise_level;

        const float sig = osc_sample + noise_sample;
        out[i]          = sig;
        out[i + 1]      = sig;
    }
}

int main(void)
{
    pod.Init();
    pod.SetAudioBlockSize(48);

    float samplerate = pod.AudioSampleRate();

    osc.Init(samplerate);
    osc.SetWaveform(Oscillator::WAVE_SIN);

    noise.Init();
    noise_filter.Init(samplerate);
    noise_filter.SetRes(0.7f);

    osc_env.Init(samplerate);
    noise_env.Init(samplerate);
    pitch_env.Init(samplerate);

    InitSteps();
    UpdateInternalClock();

    pod.StartAdc();
    pod.StartAudio(AudioCallback);
    pod.midi.StartReceive();

    while(1) {};
}
