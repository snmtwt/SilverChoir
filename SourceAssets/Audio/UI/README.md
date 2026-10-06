# Menu UI sounds

## Current electronic pair

Source: EZduzziteh's [UI Click Sounds (Sci Fi)](https://opengameart.org/content/ui-click-sounds-sci-fi), CC0.
Original files and license provenance are retained in `SciFiOriginals`.

| UE asset (under `/Game/System/Scene/MainMenu/Audio`) | Source | Duration | Asset volume |
| --- | --- | --- | --- |
| UI_MenuHover_Electronic | sfx_ui_click_8.mp3 | 120 ms | 0.16 |
| UI_MenuPress_Terminal | Original procedural synthesis | 240 ms | 0.50 |

`Scripts/prepare_scifi_menu_audio.py` removes leading silence, crops and fades the source, normalizes peaks, and writes mono 48 kHz / 16-bit WAV files plus volume-matched previews.
`Scripts/import_menu_audio.py` assigns both sounds to the button defaults and four existing Designer instances, preserving the layout.
Hover plays once per entry into an enabled, visible button. Press submits playback immediately; pressed visuals still start two engine frames later.
Both sounds use the UI sound group, non-looping playback and Force Inline loading.
The current press sound is synthesized by `Scripts/prepare_terminal_press.py`: a low-frequency transient, two rising FM chirps and a quiet upper harmonic, with short decay and no third-party samples. The CC0 provenance above applies to the hover sound and retained older source files.
The previous `UI_MenuPress_Electronic` (sfx_ui_click_3, 300 ms, volume 0.55) and `UI_MenuPress_Pulse` (sfx_ui_click_1, 180 ms, volume 0.50) are retained as alternatives. `-MenuPressOnly` changes only the press reference and preserves hover selections.

## Previous click sound (retained, not assigned)

- Author: Kenney, Interface Sounds 1.0, `Audio/click_001.ogg`.
- Source: https://kenney.nl/assets/interface-sounds
- License: CC0; original license is retained in `Kenney-License.txt`.
- Converted with FFmpeg to mono 48 kHz, 16-bit PCM WAV, 97.125 ms.
- UE asset: `/Game/System/Scene/MainMenu/Audio/UI_MenuPress`.
- Asset volume: 0.4; UI sound group; non-looping; Force Inline loading for this tiny, frequently used sound.
- `Scripts/import_menu_audio.py` imports and assigns the sound without replacing Widget Designer trees.
- Button input already submits `PlaySound2D` on press frame N; pressed visuals start at N+2. Hardware audio output latency still depends on the audio device.
