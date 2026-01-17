import wave
import base64
import json
import sys
from pathlib import Path

# usage: python wav_to_json.py test.wav out.json
wav_path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("test.wav")
out_path = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("req.json")

with wave.open(str(wav_path), "rb") as wf:
    channels = wf.getnchannels()
    sample_rate = wf.getframerate()
    sampwidth = wf.getsampwidth()
    frames = wf.readframes(wf.getnframes())

print("WAV info:", {"channels": channels, "sample_rate": sample_rate, "sample_width": sampwidth, "bytes": len(frames)})

# MVP строго: mono + 16-bit
if channels != 1:
    raise SystemExit("ERROR: WAV must be mono (1 channel)")
if sampwidth != 2:
    raise SystemExit("ERROR: WAV must be 16-bit (sample_width=2). If you have 8/24/32-bit — tell me.")

pcm_b64 = base64.b64encode(frames).decode("ascii")

payload = {
    "pcm_b64": pcm_b64,
    "sample_rate": sample_rate,
    "channels": channels,
    "sample_width": sampwidth,
    "format": "pcm_s16le",
    "lang_hint": "ru"  # можешь поменять на "en" или "kk"
}

out_path.write_text(json.dumps(payload), encoding="utf-8")
print("Saved request to:", out_path)
