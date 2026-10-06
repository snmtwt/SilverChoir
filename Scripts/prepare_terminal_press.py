"""Original procedural sci-fi UI confirmation; no third-party samples."""
from pathlib import Path
import wave
import numpy as np

project = Path(__file__).resolve().parents[1]
rate = 48000
t = np.arange(int(rate * .24)) / rate

def tone(start, duration, f0, f1, level, modulation=0):
    local = t - start
    x = np.clip(local / duration, 0, 1)
    phase = 2 * np.pi * (f0 * local + .5 * (f1 - f0) * local * local / duration)
    envelope = (1 - np.exp(-np.maximum(local, 0) / .0015)) * np.exp(-x * 4)
    envelope *= np.minimum(1, (1 - x) / .12)
    envelope *= (local >= 0) & (local < duration)
    return level * envelope * np.sin(phase + modulation * np.sin(phase * 2.01) * np.exp(-x * 5))

# A rounded low transient anchors two short digital chirps, without a long tail.
sound = tone(0, .105, 180, 72, .42)
sound += tone(0, .12, 680, 1040, .28, 1.5)
sound += tone(.033, .15, 1360, 1820, .21, .45)
sound += tone(.052, .115, 2040, 2730, .055)
sound -= np.mean(sound)
sound[:48] *= np.linspace(0, 1, 48)
sound[-960:] *= np.linspace(1, 0, 960)
sound *= .8 / np.max(np.abs(sound))

for path, volume in (
    (project / "SourceAssets/Audio/UI/UI_MenuPress_Terminal.wav", 1),
    (project / "Saved/AudioDownloads/UI_MenuPress_Terminal_Preview.wav", .50),
):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as output:
        output.setparams((1, 2, rate, 0, "NONE", "not compressed"))
        output.writeframes(np.round(sound * volume * 32767).astype("<i2").tobytes())
print("Terminal press: original synthesis, 240 ms, mono 48 kHz PCM16; playback volume 0.50")
