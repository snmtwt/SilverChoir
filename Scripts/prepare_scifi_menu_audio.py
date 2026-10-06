"""Prepare short UI variations from the retained CC0 originals; requires numpy/imageio_ffmpeg."""
from pathlib import Path
import subprocess
import wave
import numpy as np
import imageio_ffmpeg

project = Path(__file__).resolve().parents[1]
source = project / "SourceAssets/Audio/UI"
preview = project / "Saved/AudioDownloads"
preview.mkdir(parents=True, exist_ok=True)

def write_wave(path, data):
    with wave.open(str(path), "wb") as output:
        output.setparams((1, 2, 48000, 0, "NONE", "not compressed"))
        output.writeframes(np.round(np.clip(data, -1, 1) * 32767).astype("<i2").tobytes())

for number, name, duration, peak, volume, fade_ms in (
    (8, "UI_MenuHover_Electronic", .12, .7, .16, 15),
    (3, "UI_MenuPress_Electronic", .30, .8, .55, 45),
    (1, "UI_MenuPress_Pulse", .18, .75, .5, 30),
):
    decoded = subprocess.run([
        imageio_ffmpeg.get_ffmpeg_exe(), "-v", "error", "-i",
        str(source / "SciFiOriginals" / f"sfx_ui_click_{number}.mp3"),
        "-f", "f32le", "-ar", "48000", "-ac", "1", "pipe:1"
    ], check=True, capture_output=True).stdout
    samples = np.frombuffer(decoded, dtype="<f4").copy()
    # Remove encoder lead-in silence, then soften cut boundaries to avoid digital pops.
    onset = max(0, int(np.flatnonzero(abs(samples) > .001)[0]) - 24)
    samples = samples[onset:onset + round(duration * 48000)]
    samples[:48] *= np.linspace(0, 1, 48)
    fade = round(fade_ms * 48)
    samples[-fade:] *= np.linspace(1, 0, fade)
    samples *= peak / max(float(abs(samples).max()), 1e-6)
    write_wave(source / (name + ".wav"), samples)
    write_wave(preview / (name + "_Preview.wav"), samples * volume)
    print(name, "duration_ms=", len(samples) / 48, "asset_volume=", volume)
