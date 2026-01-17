const express = require("express");
const dotenv = require("dotenv");

// 1) загрузка .env
dotenv.config();

// 2) OpenAI SDK
const OpenAI = require("openai");
const { toFile } = require("openai/uploads");

const app = express();
const PORT = 8000;

// base64 может быть большой, 10mb на MVP ок (можно поднять до 20mb)
app.use(express.json({ limit: "10mb" }));

// ---------- helpers: PCM -> WAV (в памяти) ----------
function pcmS16leToWavBuffer(pcmBuffer, sampleRate, channels) {
  // WAV header (44 bytes) + data
  const bitsPerSample = 16;
  const byteRate = sampleRate * channels * (bitsPerSample / 8);
  const blockAlign = channels * (bitsPerSample / 8);
  const dataSize = pcmBuffer.length;

  const header = Buffer.alloc(44);

  header.write("RIFF", 0); // ChunkID
  header.writeUInt32LE(36 + dataSize, 4); // ChunkSize
  header.write("WAVE", 8); // Format

  header.write("fmt ", 12); // Subchunk1ID
  header.writeUInt32LE(16, 16); // Subchunk1Size (PCM)
  header.writeUInt16LE(1, 20); // AudioFormat = 1 (PCM)
  header.writeUInt16LE(channels, 22);
  header.writeUInt32LE(sampleRate, 24);
  header.writeUInt32LE(byteRate, 28);
  header.writeUInt16LE(blockAlign, 32);
  header.writeUInt16LE(bitsPerSample, 34);

  header.write("data", 36); // Subchunk2ID
  header.writeUInt32LE(dataSize, 40); // Subchunk2Size

  return Buffer.concat([header, pcmBuffer]);
}

// ---------- OpenAI client ----------
const openai = new OpenAI({
  apiKey: process.env.OPENAI_API_KEY,
});

// ---------- health ----------
app.get("/health", (req, res) => {
  res.json({ ok: true });
});

// ---------- ping ----------
app.post("/ping", (req, res) => {
  res.json({ reply: "got: " + req.body.message });
});

// ---------- stt_raw (РЕАЛЬНЫЙ STT) ----------
app.post("/stt_raw", async (req, res) => {
  try {
    const {
      pcm_b64,
      sample_rate = 16000,
      channels = 1,
      sample_width = 2,
      format = "pcm_s16le",
      lang_hint = undefined, // "ru" / "en" / "kk" (если знаешь)
    } = req.body || {};

    if (!process.env.OPENAI_API_KEY) {
      return res.status(500).json({ error: "OPENAI_API_KEY is missing in .env" });
    }

    if (!pcm_b64) {
      return res.status(400).json({ error: "pcm_b64 missing" });
    }

    // MVP: строго поддерживаем только int16 mono
    if (format !== "pcm_s16le" || channels !== 1 || sample_width !== 2) {
      return res.status(400).json({
        error: "MVP supports only pcm_s16le, mono (channels=1), 16-bit (sample_width=2)",
      });
    }

    // base64 -> raw PCM bytes
    let pcmBytes;
    try {
      pcmBytes = Buffer.from(pcm_b64, "base64");
    } catch {
      return res.status(400).json({ error: "pcm_b64 is not valid base64" });
    }

    // raw PCM -> WAV (только для отправки в STT)
    const wavBuf = pcmS16leToWavBuffer(pcmBytes, sample_rate, channels);

    // превращаем Buffer в "файл" для multipart
    const wavFile = await toFile(wavBuf, "audio.wav", { type: "audio/wav" });

    // Transcriptions API: file + model (+ optional language)
    const transcription = await openai.audio.transcriptions.create({
      file: wavFile,
      model: "gpt-4o-mini-transcribe",
      ...(lang_hint ? { language: lang_hint } : {}),
    });

    // разные модели могут возвращать по-разному; обычно есть transcription.text
    const text = transcription.text || String(transcription);

    return res.json({
      text,
      lang: lang_hint || null,
    });
  } catch (err) {
    console.log("stt_raw error:", err);
    return res.status(500).json({ error: "stt_raw failed" });
  }
});

// ---------- start ----------
app.post("/tts_raw", async (req, res) => {
  try {
    const {
      text,
      lang = "en",
      format = "pcm_s16le",
      channels = 1,
    } = req.body || {};

    if (!text) {
      return res.status(400).json({ error: "text missing" });
    }

    if (format !== "pcm_s16le" || channels !== 1) {
      return res.status(400).json({
        error: "MVP supports only pcm_s16le, mono (channels=1)",
      });
    }

    if (!process.env.OPENAI_API_KEY) {
      return res.status(500).json({ error: "OPENAI_API_KEY missing in .env" });
    }

    const response = await openai.audio.speech.create({
      model: "gpt-4o-mini-tts",
      voice: "alloy",
      input: text,
      format: "wav",
    });

    const wavBuf = Buffer.from(await response.arrayBuffer());

    // стандартный PCM WAV = 44 байта заголовка
    if (wavBuf.length <= 44) {
      return res.status(500).json({ error: "invalid wav returned from TTS" });
    }

    const pcmBuf = wavBuf.subarray(44);

    return res.json({
      pcm_b64: pcmBuf.toString("base64"),
      sample_rate: 16000,
      channels: 1,
      sample_width: 2,
      format: "pcm_s16le",
      lang,
    });
  } catch (err) {
    console.log("tts_raw error:", err);
    return res.status(500).json({ error: "tts_raw failed" });
  }
});


app.listen(PORT, "0.0.0.0", () => {
  console.log("Server running on port", PORT);
});
