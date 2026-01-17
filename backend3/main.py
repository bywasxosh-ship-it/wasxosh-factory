import os
import re
import uuid
import base64
import tempfile
import wave
from pathlib import Path
from typing import Dict, List, Literal, Optional

import httpx
from dotenv import load_dotenv
from fastapi import FastAPI, File, HTTPException, UploadFile
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import Response
from pydantic import BaseModel, Field

# ─────────────────────────────────────────────────────────────
# .env load (гарантированно рядом с main.py)
# ─────────────────────────────────────────────────────────────
ENV_PATH = Path(__file__).with_name(".env")
load_dotenv(dotenv_path=ENV_PATH, override=True)

OPENAI_API_KEY = (os.getenv("OPENAI_API_KEY") or "").strip()
OPENAI_TEXT_MODEL = (os.getenv("OPENAI_TEXT_MODEL") or "gpt-5-mini").strip()
OPENAI_TTS_MODEL = (os.getenv("OPENAI_TTS_MODEL") or "gpt-4o-mini-tts").strip()
OPENAI_STT_MODEL = (os.getenv("OPENAI_STT_MODEL") or "gpt-4o-mini-transcribe").strip()

OPENAI_BASE_URL = "https://api.openai.com/v1"

Lang = Literal["en", "ru", "kk"]

app = FastAPI(title="SteppeTalk Backend", version="0.1.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # для MVP нормально
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# ─────────────────────────────────────────────────────────────
# In-memory "сессии"
# ─────────────────────────────────────────────────────────────
SESSIONS: Dict[str, List[Dict[str, str]]] = {}
MAX_TURNS = 12

# ─────────────────────────────────────────────────────────────
# Text cleanup
# ─────────────────────────────────────────────────────────────
_ansi_re = re.compile(r"\x1b\[[0-9;]*m")


def clean_text(s: str) -> str:
    if not s:
        return s
    s = _ansi_re.sub("", s)
    s = "".join(ch for ch in s if ch in ("\n", "\t") or (ord(ch) >= 32 and ord(ch) != 127))
    s = re.sub(r"[ \u00A0]{2,}", " ", s)
    return s.strip()


def has_openai() -> bool:
    return bool(OPENAI_API_KEY)


def _auth_headers() -> Dict[str, str]:
    if not has_openai():
        return {}
    return {"Authorization": f"Bearer {OPENAI_API_KEY}"}


def _lang_name(code: str) -> str:
    return {"en": "English", "ru": "Russian", "kk": "Kazakh"}.get(code, code)


def _extract_output_text(data: dict) -> str:
    out = (data.get("output_text") or "").strip()
    if out:
        return out

    chunks: List[str] = []
    for item in data.get("output", []):
        if item.get("type") == "message":
            for c in item.get("content", []):
                if c.get("type") == "output_text":
                    chunks.append(c.get("text", ""))
    return "".join(chunks).strip()


class TranslateReq(BaseModel):
    text: str = Field(..., min_length=1, max_length=4000)
    source_lang: Lang
    target_lang: Lang


class TranslateResp(BaseModel):
    original: str
    source_lang: Lang
    target_lang: Lang
    translated: str


class ChatReq(BaseModel):
    session_id: Optional[str] = None
    text: str = Field(..., min_length=1, max_length=4000)
    user_lang: Lang = "en"
    assistant_lang: Lang = "ru"
    mode: Literal["assistant", "tourist", "learning"] = "assistant"


class ChatResp(BaseModel):
    session_id: str
    user_text: str
    assistant_text: str


class TTSReq(BaseModel):
    text: str = Field(..., min_length=1, max_length=2000)
    voice: str = "marin"
    format: Literal["mp3", "wav"] = "mp3"


class STTRawRequest(BaseModel):
    pcm_b64: str
    sample_rate: int
    channels: int
    sample_width: int
    format: str
    lang_hint: Optional[str] = None


@app.get("/health")
def health():
    return {"ok": True, "openai_enabled": has_openai()}


# ─────────────────────────────────────────────────────────────
# OpenAI helpers
# ─────────────────────────────────────────────────────────────
async def openai_translate(text: str, source_lang: Lang, target_lang: Lang) -> str:
    if not has_openai():
        return f"[{source_lang}->{target_lang}] {text}"

    system = (
        "You are a translation engine. "
        "Translate faithfully and naturally. "
        "Return ONLY the translated text, no quotes, no explanations."
    )
    user = (
        f"Source language: {_lang_name(source_lang)}\n"
        f"Target language: {_lang_name(target_lang)}\n"
        f"Text:\n{text}"
    )

    payload = {
        "model": OPENAI_TEXT_MODEL,
        "input": [
            {"role": "system", "content": system},
            {"role": "user", "content": user},
        ],
    }

    async with httpx.AsyncClient(timeout=30.0) as client:
        r = await client.post(
            f"{OPENAI_BASE_URL}/responses",
            headers={**_auth_headers(), "Content-Type": "application/json"},
            json=payload,
        )
    if r.status_code >= 400:
        raise HTTPException(status_code=502, detail=f"OpenAI error: {r.text}")

    data = r.json()
    translated = _extract_output_text(data)
    return clean_text(translated)


async def openai_chat(
    session_messages: List[Dict[str, str]],
    user_text: str,
    user_lang: Lang,
    assistant_lang: Lang,
    mode: str,
) -> str:
    if not has_openai():
        return f"(demo) Я понял: {user_text}"

    mode_hint = {
        "assistant": "Be a helpful bilingual assistant.",
        "tourist": "Be a travel helper. Keep answers short and practical.",
        "learning": "Be a language tutor. Correct mistakes gently and give a better phrasing.",
    }.get(mode, "Be a helpful assistant.")

    system = (
        f"{mode_hint}\n"
        f"Your reply language MUST be {_lang_name(assistant_lang)}.\n"
        "Keep it concise for a hackathon demo.\n"
    )

    ctx = session_messages[-MAX_TURNS * 2 :] if session_messages else []

    messages = [{"role": "system", "content": system}]
    for m in ctx:
        role = m.get("role", "user")
        text = m.get("text", "")
        if text:
            messages.append({"role": role, "content": text})

    payload = {"model": OPENAI_TEXT_MODEL, "input": messages}

    async with httpx.AsyncClient(timeout=30.0) as client:
        r = await client.post(
            f"{OPENAI_BASE_URL}/responses",
            headers={**_auth_headers(), "Content-Type": "application/json"},
            json=payload,
        )
    if r.status_code >= 400:
        raise HTTPException(status_code=502, detail=f"OpenAI error: {r.text}")

    data = r.json()
    out = _extract_output_text(data)
    return clean_text(out)


async def openai_tts_bytes(text: str, voice: str, fmt: str) -> bytes:
    if not has_openai():
        return b""

    payload = {"model": OPENAI_TTS_MODEL, "input": text, "voice": voice, "format": fmt}

    async with httpx.AsyncClient(timeout=60.0) as client:
        r = await client.post(
            f"{OPENAI_BASE_URL}/audio/speech",
            headers={**_auth_headers(), "Content-Type": "application/json"},
            json=payload,
        )
    if r.status_code >= 400:
        raise HTTPException(status_code=502, detail=f"OpenAI TTS error: {r.text}")
    return r.content


async def openai_stt_text(file_bytes: bytes, filename: str) -> str:
    if not has_openai():
        return "(demo) STT отключен без ключа"

    files = {"file": (filename, file_bytes)}
    data = {"model": OPENAI_STT_MODEL}

    async with httpx.AsyncClient(timeout=60.0) as client:
        r = await client.post(
            f"{OPENAI_BASE_URL}/audio/transcriptions",
            headers=_auth_headers(),
            data=data,
            files=files,
        )
    if r.status_code >= 400:
        raise HTTPException(status_code=502, detail=f"OpenAI STT error: {r.text}")

    j = r.json()
    text = j.get("text") or ""
    return clean_text(text)


# ─────────────────────────────────────────────────────────────
# Endpoints
# ─────────────────────────────────────────────────────────────
@app.post("/translate", response_model=TranslateResp)
async def translate(req: TranslateReq):
    translated = await openai_translate(req.text, req.source_lang, req.target_lang)
    return TranslateResp(
        original=req.text,
        source_lang=req.source_lang,
        target_lang=req.target_lang,
        translated=translated,
    )


@app.post("/chat", response_model=ChatResp)
async def chat(req: ChatReq):
    session_id = req.session_id or str(uuid.uuid4())
    history = SESSIONS.get(session_id, [])

    history.append({"role": "user", "text": req.text})
    assistant_text = await openai_chat(history, req.text, req.user_lang, req.assistant_lang, req.mode)
    history.append({"role": "assistant", "text": assistant_text})

    if len(history) > MAX_TURNS * 2:
        history = history[-MAX_TURNS * 2 :]

    SESSIONS[session_id] = history
    return ChatResp(session_id=session_id, user_text=req.text, assistant_text=assistant_text)


@app.get("/session/{session_id}")
def get_session(session_id: str):
    return {"session_id": session_id, "messages": SESSIONS.get(session_id, [])}


@app.delete("/session/{session_id}")
def clear_session(session_id: str):
    SESSIONS.pop(session_id, None)
    return {"ok": True}


@app.post("/tts")
async def tts(req: TTSReq):
    audio = await openai_tts_bytes(req.text, req.voice, req.format)
    media = "audio/mpeg" if req.format == "mp3" else "audio/wav"
    return Response(content=audio, media_type=media)


@app.post("/stt")
async def stt(file: UploadFile = File(...)):
    content = await file.read()
    if len(content) > 25 * 1024 * 1024:
        raise HTTPException(status_code=413, detail="File too large (max 25MB)")
    text = await openai_stt_text(content, file.filename or "audio.wav")
    return {"text": text}


@app.post("/stt_raw")
async def stt_raw(req: STTRawRequest):
    """
    ESP32 sends JSON with base64 raw PCM.
    We wrap it into a WAV in memory and call the same OpenAI STT helper.
    """
    if req.format != "pcm_s16le":
        raise HTTPException(status_code=400, detail="Only pcm_s16le supported in MVP")

    try:
        pcm_bytes = base64.b64decode(req.pcm_b64)
    except Exception:
        raise HTTPException(status_code=400, detail="Invalid base64 pcm_b64")

    # создаём временный WAV файл
    with tempfile.NamedTemporaryFile(delete=False, suffix=".wav") as f:
        wav_path = f.name

    try:
        with wave.open(wav_path, "wb") as wf:
            wf.setnchannels(req.channels)
            wf.setsampwidth(req.sample_width)  # 2 bytes for int16
            wf.setframerate(req.sample_rate)
            wf.writeframes(pcm_bytes)

        with open(wav_path, "rb") as rf:
            wav_bytes = rf.read()

        text = await openai_stt_text(wav_bytes, "mic.wav")
        return {"text": text, "lang": req.lang_hint or "auto"}
    finally:
        try:
            os.remove(wav_path)
        except Exception:
            pass
