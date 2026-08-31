import { useEffect, useRef, useState } from 'react'
import {
  AlertCircle,
  ArrowUpFromLine,
  Check,
  ChevronRight,
  CircleHelp,
  HardDriveDownload,
  Package,
  Play,
  RefreshCw,
  ShieldCheck,
  UploadCloud,
  X,
} from 'lucide-react'
import logoUrl from './assets/logo.png'
import { resolveLanguage, translate, languageMeta } from './i18n.js'
import { sha256Hex } from './sha256.js'

const MAX_FILE_SIZE = 50 * 1024 * 1024 * 1024

function formatBytes(bytes) {
  if (bytes === 0) return '0 B'
  const units = ['B', 'KB', 'MB', 'GB', 'TB']
  const index = Math.floor(Math.log(bytes) / Math.log(1024))
  return `${(bytes / 1024 ** index).toFixed(index > 1 ? 1 : 0)} ${units[index]}`
}

// Remaining-time formatter: seconds, minutes, hours, and days.
function formatEta(seconds) {
  if (!Number.isFinite(seconds) || seconds < 0) return '—'
  const total = Math.ceil(seconds)
  const s = total % 60
  const m = Math.floor(total / 60) % 60
  const h = Math.floor(total / 3600) % 24
  const d = Math.floor(total / 86400)
  if (d > 0) return `${d}d ${String(h).padStart(2, '0')}h`
  if (h > 0) return `${h}h ${String(m).padStart(2, '0')}m`
  if (m > 0) return `${m}m ${String(s).padStart(2, '0')}s`
  return `${s}s`
}

// Display quantization: ceil to 5s under 5min, 30s under 30min, 1min above.
// Keeps the ETA text from flickering every second.
function quantizeEta(seconds) {
  if (!Number.isFinite(seconds)) return 0
  const s = Math.max(0, Math.ceil(seconds))
  if (s < 300) return Math.ceil(s / 5) * 5
  if (s < 1800) return Math.ceil(s / 30) * 30
  return Math.ceil(s / 60) * 60
}

// Rolling-window rate estimation: blocks land in 128MB chunks from 6 parallel
// streams, so per-sample rates spike. Averaging the last 5s (min 2s of data)
// smooths that and keeps speed and ETA consistent with each other.
const RATE_WINDOW_MS = 5000
const RATE_MIN_SPAN_MS = 2000

// Content-based pkg inspection, inlined so the single-file bundle has no
// runtime dependency on a separate module. Reads only small slices of the
// file and derives platform + kind from the on-disk metadata instead of the
// file name:
//   PS4 pkg (bare "\x7FCNT")  -> param.sfo (entry 0x1000) CATEGORY (GD/AC/GP/P A)
//   PS5 pkg ("\x7FFIH")       -> embedded "\x7FCNT" (FIH+0x58) param.json (0x2000)
// The PS5 application filesystem is AES-XTS encrypted for retail, so the
// interior param.sfo is unreachable; keep what the plaintext metadata offers
// and let callers fall back to the name heuristic for the kind.
const MAGIC_PS5 = [0x7f, 0x46, 0x49, 0x48] // "\x7FFIH"
const MAGIC_CNT = [0x7f, 0x43, 0x4e, 0x54] // "\x7FCNT"

const MAX_ENTRY_TABLE = 0x40000
const MAX_PAYLOAD = 0x40000
const MAX_CNT_HEADER = 0x5a0

const CONTENT_ID_RE = /([A-Z0-9]{4}-[A-Z0-9]{5,7}_[0-9A-Z]{2}-[A-Z0-9]{16})/

function bytesEqual(bytes, magic) {
  for (let i = 0; i < magic.length; i++) {
    if (bytes[i] !== magic[i]) return false
  }
  return true
}

function readU16le(bytes, o) {
  return bytes[o] | (bytes[o + 1] << 8)
}

function readU32be(bytes, o) {
  return ((bytes[o] << 24) | (bytes[o + 1] << 16) |
          (bytes[o + 2] << 8) | bytes[o + 3]) >>> 0
}

function readU32le(bytes, o) {
  return (bytes[o] | (bytes[o + 1] << 8) |
          (bytes[o + 2] << 16) | (bytes[o + 3] << 24)) >>> 0
}

function readU64le(bytes, o) {
  const lo = readU32le(bytes, o)
  const hi = readU32le(bytes, o + 4)
  return hi * 0x100000000 + lo
}

// ASCII-only projection; used to scan for the content id and compare SFO keys.
function asciiOf(bytes) {
  let s = ''
  for (let i = 0; i < bytes.length; i++) s += String.fromCharCode(bytes[i])
  return s
}

function textOf(bytes) {
  if (typeof TextDecoder !== 'undefined') {
    try {
      return new TextDecoder('utf-8').decode(bytes)
    } catch {
      /* fall through to ASCII */
    }
  }
  return asciiOf(bytes)
}

async function readSlice(file, start, length) {
  const end = Math.min(file.size, start + length)
  if (end <= start || start < 0) return new Uint8Array(0)
  const buf = await file.slice(start, end).arrayBuffer()
  return new Uint8Array(buf)
}

function findContentId(buffer) {
  const m = CONTENT_ID_RE.exec(asciiOf(buffer))
  return m ? m[1] : null
}

// param.sfo (SFO container): little-endian header, 0x10-byte index records.
// Real Sony param.sfo stores the two 16-bit index fields (key offset, format)
// little-endian despite what some specs claim; data length/max/offset are the
// usual little-endian u32. Returns the CATEGORY value (e.g. "GD") or null.
function parseSfoCategory(bytes) {
  if (bytes.length < 0x14 ||
      bytes[0] !== 0 || bytes[1] !== 0x50 ||
      bytes[2] !== 0x53 || bytes[3] !== 0x46) {
    return null
  }
  const keyTableStart = readU32le(bytes, 0x08)
  const dataTableStart = readU32le(bytes, 0x0c)
  const entryCount = readU32le(bytes, 0x10)
  if (entryCount === 0 || entryCount > 4096) return null

  for (let i = 0; i < entryCount; i++) {
    const idx = 0x14 + i * 0x10
    if (idx + 0x10 > bytes.length) break
    const keyOff = readU16le(bytes, idx)
    const fmt = readU16le(bytes, idx + 2)
    const dataLen = readU32le(bytes, idx + 0x04)
    const dataOff = readU32le(bytes, idx + 0x0c)

    const kStart = keyTableStart + keyOff
    if (kStart >= bytes.length) break
    let kEnd = kStart
    while (kEnd < bytes.length && bytes[kEnd] !== 0) kEnd++
    if (kEnd - kStart !== 8 || asciiOf(bytes.subarray(kStart, kEnd)) !== 'CATEGORY') {
      continue
    }

    const vStart = dataTableStart + dataOff
    if (fmt === 0x0404 || fmt === 0x0204) {
      if (vStart + dataLen > bytes.length || dataLen === 0) return null
      return asciiOf(bytes.subarray(vStart, vStart + dataLen)).replace(/[\0\s]+$/, '')
    }
    if (fmt === 0x0004) {
      if (vStart + 4 > bytes.length) return null
      return readU32le(bytes, vStart).toString(10)
    }
    return null
  }
  return null
}

// Standard PS param.sfo CATEGORY mapping. Scene packages use "gp" (game
// patch) for updates as often as "pa", and "ap" for some add-on variants.
function kindFromCategory(category) {
  if (!category) return 'unknown'
  const c = category.trim().toUpperCase()
  if (c === 'GD') return 'base'
  if (c === 'AC' || c === 'AP') return 'addon'
  if (c === 'GP' || c === 'PA') return 'update'
  return 'unknown'
}

// Best-effort discriminator keys found in a PS5 param.json.
function kindFromParamJson(node) {
  if (!node || typeof node !== 'object') return 'unknown'
  for (const key of ['category', 'contentType', 'content_type', 'kind']) {
    const v = node[key]
    if (typeof v !== 'string' || !v) continue
    const k = v.toLowerCase()
    if (k.includes('patch') || k.includes('update') || k === 'pa') return 'update'
    if (k.includes('dlc') || k.includes('addon') || k === 'ac') return 'addon'
    if (k.includes('game') || k === 'gd' || k === 'base') return 'base'
  }
  return 'unknown'
}

// Find a container entry by id and return its payload bytes. The entry table
// is read at file offset `tableOffset`; entry payload offsets are relative to
// `payloadBase` (0 for a bare PS4 pkg, cOff for a PS5 embedded container).
async function findEntryPayload(file, tableOffset, entryCount, wantId, payloadBase) {
  const tableLength = Math.min(MAX_ENTRY_TABLE, entryCount * 0x20)
  const table = await readSlice(file, tableOffset, tableLength)
  const max = Math.min(entryCount, Math.floor(table.length / 0x20))
  for (let i = 0; i < max; i++) {
    const e = i * 0x20
    const id = readU32be(table, e)
    const flags1 = readU32be(table, e + 0x08)
    const off = readU32be(table, e + 0x10)
    const size = readU32be(table, e + 0x14)
    if (id === wantId && (flags1 & 0x80000000) === 0 && size > 0 && size <= MAX_PAYLOAD) {
      return readSlice(file, payloadBase + off, size)
    }
  }
  return null
}

async function inspectPs4(file, head) {
  if (head.length < 0x20) return null
  const entryCount = readU32be(head, 0x10)
  const entryOffset = readU32be(head, 0x18)
  if (entryCount === 0 || entryOffset === 0) {
    return { platform: 'ps4', kind: 'unknown', contentId: findContentId(head) }
  }
  const sfo = await findEntryPayload(file, entryOffset, entryCount, 0x1000, 0)
  const category = sfo && sfo.length ? parseSfoCategory(sfo) : null
  return {
    platform: 'ps4',
    kind: kindFromCategory(category),
    category: category || null,
    contentId: findContentId(head),
  }
}

async function inspectPs5(file, head) {
  if (head.length < 0xb0) return null
  const cntOff = readU64le(head, 0x58)
  if (cntOff <= 0 || cntOff >= file.size) return null

  const cntHead = await readSlice(file, cntOff, MAX_CNT_HEADER)
  if (cntHead.length < 0x20 || !bytesEqual(cntHead, MAGIC_CNT)) return null

  const entryCount = readU32be(cntHead, 0x10)
  const entryOffset = readU32be(cntHead, 0x18)
  const base = cntOff

  let paramJson = null
  let title = null
  if (entryCount > 0 && entryOffset > 0) {
    const payload = await findEntryPayload(file, base + entryOffset, entryCount, 0x2000, base)
    if (payload && payload.length) {
      try {
        paramJson = JSON.parse(textOf(payload))
      } catch {
        paramJson = null
      }
    }
  }

  if (paramJson && typeof paramJson === 'object') {
    if (typeof paramJson.title === 'string') title = paramJson.title
    if (!title) {
      const loc = paramJson.localizedParams || paramJson.localizedParameters
      if (loc && typeof loc === 'object') {
        for (const k of Object.keys(loc)) {
          if (k === 'defaultLanguage') continue
          const v = loc[k]
          if (v && typeof v === 'object' && typeof v.titleName === 'string') {
            title = v.titleName
            break
          }
        }
      }
    }
  }

  return {
    platform: 'ps5',
    kind: kindFromParamJson(paramJson),
    contentId: findContentId(cntHead) || findContentId(head),
    title,
  }
}

// Inspect a .pkg File (from the browser) content-wise. Never fails throwy:
// summaries with platform:'unknown' fall back to the name heuristic upstream.
async function inspectPkg(file) {
  try {
    const head = await readSlice(file, 0, 0x10000)
    if (head.length < 4) return { platform: 'unknown', kind: 'unknown' }

    if (bytesEqual(head, MAGIC_PS5)) {
      const info = await inspectPs5(file, head)
      return info || { platform: 'ps5', kind: 'unknown' }
    }
    if (bytesEqual(head, MAGIC_CNT)) {
      const info = await inspectPs4(file, head)
      return info || { platform: 'ps4', kind: 'unknown' }
    }
    return { platform: 'unknown', kind: 'unknown' }
  } catch {
    return { platform: 'unknown', kind: 'unknown' }
  }
}

function classifyPackage(name) {
  const lowerName = name.toLowerCase()
  const updateMarkers = ['patch', 'update', 'upd_', 'upd-', 'upd0', 'hotfix', 'fix-', 'fix_']
  for (const m of updateMarkers) {
    if (lowerName.includes(m)) return 'update'
  }
  const addonMarkers = [
    'dlc', 'addon', 'addcont', 'character', 'fighters', 'fighter',
    'kameo', 'skin', 'costume', 'outfit', 'bundle', 'seasonpass',
    'season_', 'season-', 'bonus', 'preorder', 'pre-order', 'expansion',
    'unlock', 'exclusive', 'packs',
  ]
  for (const m of addonMarkers) {
    if (lowerName.includes(m)) return 'addon'
  }
  if (lowerName.includes('pack')) return 'addon'
  return 'base'
}

const CONTENT_KINDS = new Set(['base', 'update', 'addon'])

// Show the real platform derived from the file's magic bytes and use the name
// heuristic only when the content can't discriminate the kind. The kind is
// stored as a stable key ('base' | 'update' | 'addon') and is rendered through
// the i18n dictionary so the queue label follows the active language.
function typeLabel(info, name) {
  const kind = info.kind && CONTENT_KINDS.has(info.kind) ? info.kind : classifyPackage(name)
  const platform = (info.platform === 'ps4' || info.platform === 'ps5')
    ? info.platform.toUpperCase()
    : null
  return { platform, kind }
}

function App() {
  const inputRef = useRef(null)
  const filesRef = useRef([])
  const [files, setFiles] = useState([])
  const [isDragging, setIsDragging] = useState(false)
  const [isInstalling, setIsInstalling] = useState(false)
  const [uploading, setUploading] = useState(false)
  const [uploadProgress, setUploadProgress] = useState(0)
  const [uploadSpeed, setUploadSpeed] = useState(0)
  const [uploadReceived, setUploadReceived] = useState(0)
  const [cancelModal, setCancelModal] = useState(false)
  const rateSamplesRef = useRef([])
  const receivedRef = useRef(0)
  const shownRef = useRef(0)
  const openerDoneRef = useRef(false)
  const statusRef = useRef(null)
  const progressHandlerRef = useRef(null)
  const abortRef = useRef(null)
  const toastTimerRef = useRef(null)
  const [toast, setToast] = useState(null)
  const [activeId, setActiveId] = useState(null)
  const [consoleInfo, setConsoleInfo] = useState({ address: '', online: false })
  const [staged, setStaged] = useState(false)
  const [language, setLanguage] = useState(() => resolveLanguage())
  const t = (key) => translate(language, key)

  const typeText = (type) => {
    const key = type.kind === 'update' ? 'kindUpdate' : type.kind === 'addon' ? 'kindAddon' : 'kindBase'
    const label = t(key)
    return type.platform ? `${type.platform} · ${label}` : label
  }

  const etaText = (entry) => {
    if (uploadSpeed <= 0 || uploadReceived >= entry.file.size) return ''
    const raw = (entry.file.size - uploadReceived) / uploadSpeed
    return ` · ETA ${formatEta(quantizeEta(raw))}`
  }

  useEffect(() => {
    filesRef.current = files
  }, [files])

  const showToast = (type, key) => {
    setToast({ type, key })
    if (toastTimerRef.current) window.clearTimeout(toastTimerRef.current)
    toastTimerRef.current = window.setTimeout(() => setToast(null), 5000)
  }

  useEffect(() => {
    let active = true
    const apply = (status) => {
      if (!active) return
      statusRef.current = { ...status, _at: Date.now() }
      const currentHost = window.location.hostname
      setConsoleInfo({
        address: status.ip || status.address || currentHost,
        online: true,
      })
      setStaged(Boolean(status.staged))
      setLanguage(resolveLanguage(status.language))
      if (openerDoneRef.current && typeof status.bytes === 'number') {
        progressHandlerRef.current?.(status)
      }
    }
    const source = new EventSource('/api/stream')
    source.onmessage = (event) => {
      try {
        apply(JSON.parse(event.data))
      } catch {
        /* ignore malformed frames */
      }
    }
    source.onerror = () => {
      if (active) setConsoleInfo((info) => ({ ...info, online: false }))
    }
    return () => {
      active = false
      source.close()
    }
  }, [])

  useEffect(() => {
    const meta = languageMeta[language] || languageMeta.en
    document.documentElement.lang = meta.html
    document.documentElement.dir = meta.rtl ? 'rtl' : 'ltr'
  }, [language])

  const addFiles = async (incomingFiles) => {
    const entries = incomingFiles ? Array.from(incomingFiles) : []
    const validFiles = entries.filter((file) => {
      if (!file) return false
      // Trim so names with stray whitespace still match; treat a missing or
      // non-finite size (cloud placeholders, odd drag sources) as 0 instead
      // of rejecting a real package.
      const name = String(file.name || '').trim().toLowerCase()
      const size = Number.isFinite(file.size) ? file.size : 0
      return name.endsWith('.pkg') && size <= MAX_FILE_SIZE
    })
    const nextFiles = await Promise.all(
      validFiles.map(async (file) => ({
        id: `${file.name}-${file.size}`,
        file,
        type: typeLabel(await inspectPkg(file), file.name),
        status: 'ready',
      })),
    )
    const existingIds = new Set(filesRef.current.map((entry) => entry.id))
    const fresh = nextFiles.filter((entry) => !existingIds.has(entry.id))
    if (fresh.length) {
      filesRef.current = [...filesRef.current, ...fresh]
      setFiles(filesRef.current)
    }
    if (inputRef.current) inputRef.current.value = ''
    // Only warn when nothing usable arrived; a valid pkg mixed with extra
    // junk entries must not look like a rejection.
    if (validFiles.length === 0 && entries.length > 0) {
      showToast('warning', 'invalidPkg')
    } else if (fresh.length > 0) {
      showToast('success', fresh.length === 1 ? 'filesDetected' : 'filesDetectedPlural')
    }
  }

  const uploadAndInstall = async (entry) => {
    const signal = abortRef.current.signal
    const file = entry.file
    const total = file.size
    const name = encodeURIComponent(file.name)

    // Same protocol as parallel_uploader.py, against the console's own
    // API on TCP 9090 (staged-size preflight → head256 opener → 128MB
    // ranged blocks → empty-body finalize). Progress is pushed by the
    // SSE stream on :12800 (/api/stream), so no per-chunk polling here.
    const API = `http://${window.location.hostname}:9090`
    const BLOCK_BYTES = 128 * 1024 * 1024
    // Browsers cap ~6 connections per origin; keep headroom for the SSE
    // progress stream and the status polls.
    const STREAMS = 6

    setUploading(true)
    setUploadProgress(0)
    setUploadSpeed(0)
    setUploadReceived(0)
    rateSamplesRef.current = []
    receivedRef.current = 0
    shownRef.current = 0
    openerDoneRef.current = false

    const updateProgress = (rawBytes) => {
      const bytes = Math.max(rawBytes, shownRef.current)
      shownRef.current = bytes
      const now = performance.now()
      const samples = rateSamplesRef.current
      if (!samples.length || samples[samples.length - 1].bytes !== bytes) {
        samples.push({ bytes, at: now })
      }
      while (samples.length > 2 && samples[0].at < now - RATE_WINDOW_MS) samples.shift()
      const first = samples[0]
      if (samples.length >= 2 && now - first.at >= RATE_MIN_SPAN_MS) {
        const rate = (bytes - first.bytes) / ((now - first.at) / 1000)
        setUploadSpeed((old) => (old === 0 ? rate : old * 0.7 + rate * 0.3))
      }
      const capped = Math.min(bytes, total)
      setUploadReceived(capped)
      setUploadProgress(Math.min(100, Math.round((capped / total) * 100)))
    }

    const sendBlock = async (offset, length, head256) => {
      let query = `name=${name}&offset=${offset}&total=${total}`
      if (head256) query += `&head256=${head256}`
      const response = await fetch(`${API}/install?${query}`, {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: file.slice(offset, offset + length),
        signal,
      })
      const result = await response.json().catch(() => ({}))
      if (!response.ok || !result.ok) throw new Error(result.error || 'block')
      receivedRef.current += length
      updateProgress(receivedRef.current)
    }

    const hashHead = async (cap) => {
      const head = await file.slice(0, Math.min(cap, total)).arrayBuffer()
      return sha256Hex(head)
    }

    // Preflight (reuse): if a full copy of the same size is already staged,
    // skip the upload and finalize directly.
    let reused = false
    try {
      const res = await fetch(`${API}/staged-size?name=${name}`, { signal })
      const j = await res.json().catch(() => ({}))
      if (res.ok && j.ok && Number(j.size) === total) reused = true
    } catch {
      /* preflight skipped */
    }

    // Progress comes from the unified /api/stream SSE (opened on mount),
    // which pushes the session byte counter 4x/s — no polling requests and
    // no extra connection during the upload.
    // The server reports the decoded/sanitized file name while `name` is
    // URI-encoded for the query, so attributing by name alone would discard
    // every frame for files with spaces. Accept the frame unless BOTH the
    // reported name and total belong to a different session.
    progressHandlerRef.current = (status) => {
      const frameName = status.name || ''
      const frameTotal = Number(status.total) || 0
      if (frameName && frameTotal && frameName !== file.name &&
          frameTotal !== file.size) return
      updateProgress(Math.max(status.bytes, receivedRef.current))
    }

    try {
      if (!reused) {
        // blk0 session opener (first 1MB + head256), awaited first.
        const openLen = Math.min(1048576, total)
        await sendBlock(0, openLen, await hashHead(1048576))
        openerDoneRef.current = true

        // Remaining ranges in 128MB blocks, parallel like the uploader.
        const start = openLen
        const jobs = []
        if (start < total) {
          const first = Math.min(BLOCK_BYTES, total - start)
          jobs.push([start, first])
          for (let off = start + first; off < total; off += BLOCK_BYTES) {
            jobs.push([off, Math.min(BLOCK_BYTES, total - off)])
          }
        }

        let nextJob = 0
        const failed = []
        const worker = async () => {
          for (;;) {
            if (signal.aborted) return
            const idx = nextJob
            nextJob += 1
            if (idx >= jobs.length) return
            try {
              await sendBlock(jobs[idx][0], jobs[idx][1])
            } catch {
              if (signal.aborted) return
              failed.push(jobs[idx])
            }
          }
        }
        await Promise.all(Array.from({ length: STREAMS }, worker))

        if (signal.aborted) {
          const err = new Error('cancelled')
          err.name = 'AbortError'
          throw err
        }

        // One sequential retry pass; the DPI keeps partials.
        for (const [off, len] of failed.splice(0)) {
          if (signal.aborted) break
          await sendBlock(off, len)
        }
      }

      setUploadProgress(100)
      setUploadReceived(total)
      setUploading(false)

      // Finalize: empty body, offset == total (same as the uploader).
      const fin = await fetch(
        `${API}/install?name=${name}&offset=${total}&total=${total}&finalize=1`,
        { method: 'POST', signal },
      )
      const finResult = await fin.json().catch(() => ({}))
      if (!fin.ok || !finResult.ok) throw new Error(finResult.error || t('installRejected'))
      setStaged(true)
    } finally {
      progressHandlerRef.current = null
    }

    showToast('success', 'uploadAccepted')

    // Poll until a terminal phase. Sony reports intermediate phases
    // (transferring, installing, validating, staged, ...) that vary by
    // firmware, so anything non-terminal keeps the wait alive.
    const TERMINAL_OK = ['complete', 'playable']
    const POLL_CAP = 1800
    let status = statusRef.current
    let acceptedPolls = 0
    let pollCount = 0
    while (!status && pollCount < 40) {
      await new Promise((resolve) => window.setTimeout(resolve, 250))
      status = statusRef.current
      pollCount += 1
    }
    if (!status) {
      showToast('warning', 'installError')
      return false
    }
    while (!TERMINAL_OK.includes(status.phase) && status.phase !== 'error' &&
           status.phase !== 'idle' && pollCount < POLL_CAP) {
      if (status.phase === 'accepted') {
        acceptedPolls += 1
        if (acceptedPolls > 10) break // accepted but completion not trackable
      }
      await new Promise((resolve) => window.setTimeout(resolve, 1000))
      status = statusRef.current
      pollCount += 1
    }

    if (status.phase === 'error' || status.phase === 'idle') {
      throw new Error(status.error || t('installError'))
    }
    if (TERMINAL_OK.includes(status.phase)) {
      showToast('success', 'installComplete')
      return true
    }
    showToast('success', 'installStarted')
    return false
  }

  const installFiles = async () => {
    if (!files.length || isInstalling) return
    const queue = files.filter((entry) => entry.status !== 'complete')
    if (!queue.length) return
    setIsInstalling(true)
    setToast(null)
    abortRef.current = new AbortController()

    try {
      for (const entry of queue) {
        if (abortRef.current.signal.aborted) break
        setActiveId(entry.id)
        setFiles((currentFiles) => currentFiles.map((e) =>
          e.id === entry.id ? { ...e, status: 'installing' } : e))
        try {
          const finished = await uploadAndInstall(entry)
          setFiles((currentFiles) => currentFiles.map((e) =>
            e.id === entry.id ? { ...e, status: finished ? 'complete' : 'ready' } : e))
        } catch (error) {
          if (error.name === 'AbortError') {
            showToast('warning', 'uploadCancelled')
            setFiles((currentFiles) => currentFiles.map((e) =>
              e.id === entry.id ? { ...e, status: 'ready' } : e))
            break
          }
          setFiles((currentFiles) => currentFiles.map((e) =>
            e.id === entry.id ? { ...e, status: 'ready' } : e))
          showToast('warning', error.message || 'uploadError')
        }
      }
    } finally {
      setIsInstalling(false)
      setUploading(false)
      setActiveId(null)
      setCancelModal(false)
      abortRef.current = null
    }
  }

  const confirmCancelUpload = () => {
    setCancelModal(false)
    if (abortRef.current) abortRef.current.abort()
  }

  return (
    <main className="app-shell">
      <div className="ambient ambient-left" />
      <div className="ambient ambient-right" />

      <section className="workspace">
        <div className="intro-row">
          <div>
            <div className="hero-heading">
              <img className="hero-logo" src={logoUrl} alt="OnionHEN" />
              <h1>{t('title')}<br /><em>{t('titleAccent')}</em></h1>
            </div>
            <p className="lede">{t('lede')}</p>
          </div>
          <div className="network-chip">
            <span className="chip-label">{t('console')}</span>
            <span className="chip-value">{consoleInfo.address || t('waiting')}</span>
            <span className={`chip-subtitle ${consoleInfo.online ? 'is-online' : 'is-offline'}`}><span className="pulse-dot" /> {consoleInfo.online ? t('receive') : t('offline')}</span>
          </div>
        </div>

        <div className="content-grid">
          <section className="drop-column" aria-labelledby="drop-title">
            <div
              className={`dropzone ${isDragging ? 'is-dragging' : ''} ${files.length ? 'has-files' : ''}`}
              onDragEnter={(event) => { event.preventDefault(); setIsDragging(true) }}
              onDragOver={(event) => event.preventDefault()}
              onDragLeave={(event) => { if (event.currentTarget === event.target) setIsDragging(false) }}
              onDrop={(event) => { event.preventDefault(); setIsDragging(false); addFiles(event.dataTransfer.files) }}
              onClick={() => inputRef.current?.click()}
              role="button"
              tabIndex="0"
              onKeyDown={(event) => { if (event.key === 'Enter' || event.key === ' ') inputRef.current?.click() }}
            >
              {/* The input lives inside the clickable dropzone, so its own
                  click must not bubble back into the dropzone handler —
                  that re-opens the picker a second time in some browsers. */}
              <input
                ref={inputRef}
                type="file"
                accept=".pkg"
                multiple
                hidden
                onClick={(event) => event.stopPropagation()}
                onChange={(event) => addFiles(event.target.files)}
              />
              <div className="drop-orbit orbit-one" />
              <div className="drop-orbit orbit-two" />
              <div className="drop-icon"><UploadCloud size={27} strokeWidth={1.5} /></div>
              <p className="drop-title" id="drop-title">{t('dropTitle')}</p>
              <p className="drop-hint">{t('or')} <span>{t('browse')}</span></p>
              <div className="drop-meta"><span>{t('fileRules')}</span><i /> <span>{t('maxSize')}</span><i /> <span>{t('localNetwork')}</span></div>
            </div>
            <div className="security-note"><ShieldCheck size={15} /><span>{t('security')}</span><CircleHelp size={14} /></div>
          </section>

          <aside className="queue-panel">
            <div className="panel-heading"><div><p className="panel-kicker">{t('queue')}</p><h2>{files.length ? `${files.length} ${files.length === 1 ? t('package') : t('packages')}` : t('noPackages')}</h2></div><Package size={19} /></div>
            {files.length ? (
              <div className="file-list">
                {files.map((entry) => (
                  <div className="file-entry" key={entry.id}>
                    <div className="file-icon"><Package size={18} /></div>
                    <div className="upload-body">
                      <div className="file-details"><strong title={entry.file.name}>{entry.file.name}</strong><span>{typeText(entry.type)} <i /> {formatBytes(entry.file.size)}</span></div>
                      {uploading && entry.id === activeId && (
                        <div className="upload-progress">
                          <div className="upload-progress-track">
                            <div className="upload-progress-fill" style={{ width: `${uploadProgress}%` }} />
                          </div>
                          <span className="upload-progress-meta">{uploadProgress}% · {formatBytes(uploadReceived)} / {formatBytes(entry.file.size)} · {formatBytes(uploadSpeed)}/s{etaText(entry)}</span>
                        </div>
                      )}
                    </div>
                    <div className={`file-state state-${entry.status}`} aria-label={entry.status}>{entry.status === 'complete' ? <Check size={15} /> : entry.status === 'installing' ? <RefreshCw className="spin" size={14} /> : <span />}</div>
                    {!(isInstalling && !uploading && entry.id === activeId) && (
                    <button
                      className="remove-file"
                      title={uploading && entry.id === activeId ? t('cancelUploadTitle') : t('removePackageTitle')}
                      onClick={(event) => {
                        event.stopPropagation()
                        if (uploading && entry.id === activeId) {
                          setCancelModal(true)
                          return
                        }
                        const next = filesRef.current.filter((item) => item.id !== entry.id)
                        filesRef.current = next
                        setFiles(next)
                      }}
                    ><X size={14} /></button>
                  )}
                  </div>
                ))}
              </div>
            ) : (
              <div className="empty-queue"><HardDriveDownload size={27} strokeWidth={1.3} /><p>{t('stagedPackages')}</p></div>
            )}
            <div className="panel-footer"><span>{staged ? t('staged') : files.length ? t('readyUpload') : t('waitingFiles')}</span><span className="footer-line" /></div>
          </aside>
        </div>

        {toast && <div key={`${toast.type}:${toast.key}`} className={`toast toast-${toast.type}`} role="status"><AlertCircle size={16} /><span>{t(toast.key)}</span><button type="button" onClick={() => setToast(null)} aria-label="Dismiss notification"><X size={14} /></button></div>}

        {cancelModal && (
          <div className="modal-overlay" role="dialog" aria-modal="true">
            <div className="modal-card">
              <h3 className="modal-title">{t('cancelTitle')}</h3>
              <p className="modal-body">{t('cancelBody')}</p>
              <div className="modal-actions">
                <button type="button" className="modal-secondary" onClick={() => setCancelModal(false)}>{t('cancelDismiss')}</button>
                <button type="button" className="modal-danger" onClick={confirmCancelUpload}>{t('cancelConfirm')}</button>
              </div>
            </div>
          </div>
        )}

        <div className="action-row">
          <div className="action-context"><ArrowUpFromLine size={17} /><span>{t('stageDirectly')}<br /><strong>{t('storage')}</strong></span></div>
          <button className="install-button" disabled={!files.some((e) => e.status !== 'complete') || isInstalling} onClick={installFiles}>
            {isInstalling ? <RefreshCw className="spin" size={18} /> : <Play size={17} fill="currentColor" />}
            <span>{uploading ? `${t('uploading')} ${uploadProgress}%` : isInstalling ? t('installing') : t('upload')}</span><ChevronRight size={18} />
          </button>
        </div>
      </section>

    </main>
  )
}

export default App
