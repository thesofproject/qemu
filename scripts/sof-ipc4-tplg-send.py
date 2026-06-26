#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""sof-ipc4-tplg-send.py

Parse an IPC4 SOF binary topology (.tplg) for one or more pipeline IDs,
resolve module IDs from the firmware .ri manifest, and send the IPC4
pipeline-setup sequence to a running QEMU ACE DSP via its HMP monitor socket.

Sequence sent per pipeline
--------------------------
  1. GLB_CREATE_PIPELINE
  2. MOD_INIT_INSTANCE  (each module widget, SCHEDULER excluded)
  3. MOD_BIND           (intra-pipeline DAPM graph connections)

After all pipelines are created:
  4. MOD_BIND (cross-pipeline connections between the requested pipelines)
  5. GLB_SET_PIPELINE_STATE -> PAUSED
  6. GLB_SET_PIPELINE_STATE -> RUNNING  (only when --start is given)
  7. Wait --run-duration seconds        (default 5.0)
  8. GLB_SET_PIPELINE_STATE -> PAUSED   (stop)

Auto-discovery
--------------
The script lives at  <workspace>/qemu/scripts/sof-ipc4-tplg-send.py
and derives the workspace root as its grandparent directory.  From there:

  tplgtool2   <workspace>/sof-test/tools/
  firmware    <workspace>/build-<machine>/zephyr/zephyr.ri
  topology    <workspace>/sof/tools/build_tools/topology/topology2/target/

Use --machine to set the machine name (e.g. mtl, ptl, lnl).  All paths that
are not explicitly given will be discovered and printed before use.

QEMU must expose its HMP monitor on a socket:
  qemu-system-xtensa ... -monitor unix:/tmp/qemu-mon.sock,server,nowait
  qemu-system-xtensa ... -monitor tcp:localhost:4444,server,nowait

Usage
-----
  sof-ipc4-tplg-send.py --machine mtl --pipeline 1 2 --start
  sof-ipc4-tplg-send.py --machine ptl --tplg sof-ptl-es83x6-ssp1.tplg \\
                         --pipeline 1 --start --run-duration 10
"""

import argparse
import re
import socket
import struct
import sys
import time
import os
from pathlib import Path

# ---------------------------------------------------------------------------
# IPC4 error handling
# ---------------------------------------------------------------------------

class IPCError(Exception):
    """Raised when firmware returns a non-zero IPC4 status or a TX/poll fails."""

# IPC4 firmware error codes (from SOF ipc4/error_status.h and Zephyr errno mapping)
_IPC4_STATUS = {
    0x00: 'IPC4_SUCCESS',
    0x01: 'IPC4_OUT_OF_MEMORY',
    0x02: 'IPC4_BUSY',
    0x03: 'IPC4_PENDING',
    0x04: 'IPC4_FAILURE',
    0x05: 'IPC4_INVALID_RESOURCE_ID',
    0x06: 'IPC4_OUT_OF_MIPS',
    0x07: 'IPC4_INVALID_REQUEST',
    0x08: 'IPC4_UNAVAILABLE',
    0x09: 'IPC4_RESOURCE_IN_USE',  # EBUSY / comp not found
    0x0A: 'IPC4_INVALID_RESOURCE_STATE',  # invalid state transition
    0x0B: 'IPC4_POWER_TRANSITION_FAILED',
    0x0C: 'IPC4_NOT_INITIALIZED',
    0x0D: 'IPC4_INIT_FAILED',     # module init data failed (-EINVAL)
    0x0E: 'IPC4_INVALID_QUEUE_ID',
    0x0F: 'IPC4_ALREADY_EXISTS',
    0x68: 'IPC4_MOD_INIT_FAILED', # 104 decimal: MODULE_MSG failed
}

def _decode_ipc4_status(status: int) -> str:
    """Return a human-readable string for an IPC4 status code."""
    name = _IPC4_STATUS.get(status, f'UNKNOWN_0x{status:02x}')
    return f'0x{status:06x} ({name})'


# ---------------------------------------------------------------------------
# Workspace / path auto-discovery
# ---------------------------------------------------------------------------
SCRIPT_DIR = Path(__file__).resolve().parent


def _find_workspace() -> Path:
    """Walk upward from the script to find the workspace root.

    The workspace root is identified by containing at least two of:
      sof-test/, qemu/, sof/, build-*/
    Falls back to the grandparent of the script directory.
    """
    here = SCRIPT_DIR
    for candidate in [here, here.parent, here.parent.parent, here.parent.parent.parent]:
        markers = sum(1 for name in ('sof-test', 'qemu', 'sof')
                      if (candidate / name).is_dir())
        if markers >= 2:
            return candidate
    # fallback
    return SCRIPT_DIR.parent.parent


WORKSPACE = _find_workspace()

_TPLG_SEARCH_DIRS = [
    # pre-built topology shipped with the sof source tree
    WORKSPACE / 'sof' / 'tools' / 'build_tools' / 'topology' / 'topology2' / 'target' / 'sof-ipc4-tplg',
    WORKSPACE / 'sof' / 'tools' / 'build_tools' / 'topology' / 'topology2' / 'target' / 'development',
    WORKSPACE / 'sof' / 'tools' / 'build_tools' / 'topology' / 'topology2' / 'target',
]

def _find_tplg_tools() -> Path | None:
    candidate = WORKSPACE / 'sof-test' / 'tools'
    return candidate if (candidate / 'tplgtool2.py').exists() else None

def _find_firmware(machine: str) -> Path | None:
    candidate = WORKSPACE / f'build-{machine}' / 'zephyr' / 'zephyr.ri'
    return candidate if candidate.exists() else None

def _find_tplg(machine: str, tplg_name: str | None) -> Path | None:
    """Search for a .tplg file by name (with or without .tplg suffix) or
    by machine pattern if only --machine is given."""
    if tplg_name:
        # If it's an absolute or relative path that exists, use it directly
        p = Path(tplg_name)
        if p.exists():
            return p.resolve()
        # Try appending .tplg
        if not tplg_name.endswith('.tplg'):
            p2 = Path(tplg_name + '.tplg')
            if p2.exists():
                return p2.resolve()
        # Search known directories by filename
        name = tplg_name if tplg_name.endswith('.tplg') else tplg_name + '.tplg'
        for d in _TPLG_SEARCH_DIRS:
            candidate = d / name
            if candidate.exists():
                return candidate
        return None

    # No explicit name: search for any tplg matching *nocodec* + machine
    if machine:
        patterns = [f'*{machine}*nocodec*.tplg', f'*nocodec*{machine}*.tplg']
        for d in _TPLG_SEARCH_DIRS:
            if not d.exists():
                continue
            for pat in patterns:
                hits = sorted(d.glob(pat))
                if hits:
                    return hits[0]
    return None

def _list_tplg_candidates(machine: str) -> list[Path]:
    """Return all .tplg files matching *machine* across search dirs."""
    results = []
    for d in _TPLG_SEARCH_DIRS:
        if d.exists():
            results.extend(sorted(d.glob(f'*{machine}*.tplg')))
    return results


# ---------------------------------------------------------------------------
# tplgtool2 import
# ---------------------------------------------------------------------------
def _import_tplgtool2(extra_path: str | None):
    candidates = []
    if extra_path:
        candidates.append(extra_path)
    tools = _find_tplg_tools()
    if tools:
        candidates.append(str(tools))
    for p in candidates:
        if p not in sys.path:
            sys.path.insert(0, p)
    try:
        import tplgtool2 as tt
        return tt
    except ImportError:
        pass
    sys.exit(
        "error: cannot import tplgtool2.  Install 'construct' and ensure\n"
        f"  {WORKSPACE}/sof-test/tools/  exists, or pass --tplg-tools-path."
    )


# ---------------------------------------------------------------------------
# IPC4 constants
# ---------------------------------------------------------------------------
IPC4_GLB_CREATE_PIPELINE    = 17
IPC4_GLB_DELETE_PIPELINE    = 18
IPC4_GLB_SET_PIPELINE_STATE = 19

IPC4_MOD_INIT_INSTANCE   = 0
IPC4_MOD_LARGE_CONFIG_SET = 4
IPC4_MOD_BIND            = 5

IPC4_PPL_STATE_RESET   = 0
IPC4_PPL_STATE_PAUSED  = 3
IPC4_PPL_STATE_RUNNING = 4

IPC4_MSG_TGT_FW_GEN  = 0
IPC4_MSG_TGT_MODULE  = 1

# Widget token IDs used for building module init data
TKN_COMP_CPC          = 406
TKN_COMP_IS_PAGES     = 416
# Input audio format tokens (ipc4_audio_format in ipc4_base_module_cfg)
TKN_FMT_IN_RATE       = 1900
TKN_FMT_IN_BIT_DEPTH  = 1901
TKN_FMT_IN_VALID_BITS = 1902
TKN_FMT_IN_CH_MAP     = 1904
TKN_FMT_IN_CH_CFG     = 1905
TKN_FMT_IN_INTERLEAVE = 1906
# fmt_cfg packs (channels_count | valid_bit_depth<<8 | s_type<<16) per ipc4_audio_format
TKN_FMT_IN_FMT_CFG    = 1907
# Output audio format tokens (ipc4_audio_format out_fmt in ipc4_copier_module_cfg)
TKN_FMT_OUT_RATE       = 1930
TKN_FMT_OUT_BIT_DEPTH  = 1931
TKN_FMT_OUT_CH_MAP     = 1934
TKN_FMT_OUT_CH_CFG     = 1935
TKN_FMT_OUT_INTERLEAVE = 1936
TKN_FMT_OUT_FMT_CFG    = 1937
TKN_IBS               = 1970
TKN_OBS               = 1971

# ---------------------------------------------------------------------------
# Inline topology: default format and module-name mapping
# ---------------------------------------------------------------------------

# Standard 48 kHz, stereo, 16-bit PCM assumed for all inline topologies.
# IBS/OBS = 48000 Hz × 2 ch × 2 B × 1 ms = 192 B
_INLINE_FMT = {
    TKN_COMP_CPC:          0,
    TKN_COMP_IS_PAGES:     1,
    TKN_IBS:               192,
    TKN_OBS:               192,
    TKN_FMT_IN_RATE:       48000,
    TKN_FMT_IN_BIT_DEPTH:  16,
    TKN_FMT_IN_CH_MAP:     0xFFFFFF10,  # L=ch0, R=ch1
    TKN_FMT_IN_CH_CFG:     1,           # IPC4_CHANNEL_CONFIG_STEREO
    TKN_FMT_IN_INTERLEAVE: 0,
    TKN_FMT_IN_FMT_CFG:    0x001002,    # ch=2 | (valid=16)<<8 | type=0
    # out_fmt — same as in_fmt for passthrough nodes
    TKN_FMT_OUT_RATE:       48000,
    TKN_FMT_OUT_BIT_DEPTH:  16,
    TKN_FMT_OUT_CH_MAP:     0xFFFFFF10,
    TKN_FMT_OUT_CH_CFG:     1,
    TKN_FMT_OUT_INTERLEAVE: 0,
    TKN_FMT_OUT_FMT_CFG:    0x001002,
}

# Ordered list of firmware manifest module name candidates per node type.
# The first name found in the manifest is used.
_NODE_TO_FW_NAMES: dict[str, list[str]] = {
    'host':    ['COPIER'],
    'link':    ['COPIER'],
    'copier':  ['COPIER'],
    'volume':  ['GAIN', 'VOLUME'],
    'gain':    ['GAIN', 'VOLUME'],
    'eqfir':   ['EQFIRI', 'EQFIRP', 'EQFIR', 'EQ_FIR'],
    'eq_fir':  ['EQFIRI', 'EQFIRP', 'EQFIR', 'EQ_FIR'],
    'eqiir':   ['EQIIRII', 'EQIIRP', 'EQIIR', 'EQ_IIR'],
    'eq_iir':  ['EQIIRII', 'EQIIRP', 'EQIIR', 'EQ_IIR'],
    'src':     ['SRC'],
    'asrc':    ['ASRC'],
    'mixin':   ['MIXIN'],
    'mixout':  ['MIXOUT'],
}

# Node types that require the copier init layout:
# base_cfg(40) + out_fmt(24) + copier_feature_mask(4) = 68 B
_COPIER_NODE_TYPES = {'host', 'link', 'copier'}

# Node types that need extended init beyond base_cfg
_SRC_NODE_TYPES = {'src', 'asrc'}

# IPC4 DMA gateway types for node_id encoding: (dma_type << 8) | v_index
_DMA_TYPE_HDA_HOST_OUT = 0   # host → DSP (playback)
_DMA_TYPE_HDA_HOST_IN  = 1   # DSP → host (capture)
_DMA_TYPE_HDA_LINK_OUT = 2   # DSP → codec (playback)
_DMA_TYPE_HDA_LINK_IN  = 3   # codec → DSP (capture)
_DMA_TYPE_DMIC_LINK_IN = 4   # DMIC → DSP (capture)
_DMA_BUF_SIZE_DEFAULT  = 4096  # reasonable default for QEMU

def _build_copier_node_id(widget_name: str, stream_counters: dict) -> int:
    """Derive a valid IPC4 gateway node_id from the copier widget name.

    Returns 0xFFFFFFFF for module-copiers (no gateway).
    """
    low = widget_name.lower()
    if 'module-copier' in low or ('copier' in low and 'host' not in low
                                   and 'dai' not in low):
        return 0xFFFFFFFF  # internal copier, no DMA gateway

    if 'host' in low:
        if 'playback' in low:
            dma_type = _DMA_TYPE_HDA_HOST_OUT
        else:
            dma_type = _DMA_TYPE_HDA_HOST_IN
    elif 'dai' in low or 'ssp' in low.replace('-', '').replace('.', ''):
        if 'dmic' in low:
            dma_type = _DMA_TYPE_DMIC_LINK_IN
        elif 'playback' in low:
            dma_type = _DMA_TYPE_HDA_LINK_OUT
        else:
            dma_type = _DMA_TYPE_HDA_LINK_IN
    else:
        return 0xFFFFFFFF

    # Assign unique stream index per DMA type.
    idx = stream_counters.get(dma_type, 0)
    stream_counters[dma_type] = idx + 1
    return (dma_type << 8) | idx


# ---------------------------------------------------------------------------
# Manifest parser
# ---------------------------------------------------------------------------
_FW_HDR_FMT  = '<4sI8sIII2H2H4I'
_FW_HDR_SIZE = struct.calcsize(_FW_HDR_FMT)
_FW_HDR_MAGIC = b'$AM1'

_MOD_FMT  = '<4s8s16sI32sIHHIHH36s'
_MOD_SIZE = struct.calcsize(_MOD_FMT)


def parse_manifest(fw_path: Path) -> tuple[dict, dict]:
    """Return (uuid_to_id, name_to_id) from the SOF firmware manifest.

    uuid_to_id  maps 16-byte UUID bytes → module index.
    name_to_id  maps upper-cased 8-char module name string → module index.
    """
    data = fw_path.read_bytes()
    offset = data.find(_FW_HDR_MAGIC)
    if offset < 0:
        sys.exit(f"error: '{fw_path}': firmware manifest magic '$AM1' not found")
    hdr = struct.unpack_from(_FW_HDR_FMT, data, offset)
    num_modules = hdr[10]
    uuid_to_id = {}
    name_to_id = {}
    mod_base = offset + _FW_HDR_SIZE
    for idx in range(num_modules):
        moff = mod_base + idx * _MOD_SIZE
        if moff + _MOD_SIZE > len(data):
            break
        fields = struct.unpack_from(_MOD_FMT, data, moff)
        uuid_to_id[fields[2]] = idx   # uuid (16 bytes) -> index
        name = fields[1].rstrip(b'\x00').decode('ascii', errors='replace').upper()
        name_to_id[name] = idx        # '8-char name' -> index
    return uuid_to_id, name_to_id


# ---------------------------------------------------------------------------
# Topology parser
# ---------------------------------------------------------------------------

def _get_tokens(widget_priv) -> dict:
    """Return a flat token dict from widget private data.

    First-occurrence wins: copier widgets have many format blocks (one per
    supported rate/channel/depth combination).  Using the first occurrence
    gives the primary format rather than a random high-rate variant.
    """
    tokens = {}
    for arr in widget_priv:
        for elem in arr['elems']:
            tok = elem['token']
            key = tok.value if hasattr(tok, 'value') else tok
            if key not in tokens:
                tokens[key] = elem.get('value', elem.get('uuid', None))
    return tokens


def _get_init_bytes_from_kcontrols(kcontrols) -> bytes:
    for kc in kcontrols:
        if kc['hdr']['type'] == 'BYTES':
            raw = kc['body'].get('priv', b'')
            return bytes(raw) if raw else b''
    return b''


def _build_base_module_cfg(tokens: dict) -> bytes:
    """Build ipc4_base_module_cfg (40 bytes) from widget vendor tokens."""
    return struct.pack('<10I',
        tokens.get(TKN_COMP_CPC,          0),
        tokens.get(TKN_IBS,               384),
        tokens.get(TKN_OBS,               384),
        tokens.get(TKN_COMP_IS_PAGES,     1),
        tokens.get(TKN_FMT_IN_RATE,       48000),
        tokens.get(TKN_FMT_IN_BIT_DEPTH,  32),
        tokens.get(TKN_FMT_IN_CH_MAP,     0xFFFFFF10),
        tokens.get(TKN_FMT_IN_CH_CFG,     1),
        tokens.get(TKN_FMT_IN_INTERLEAVE, 0),
        tokens.get(TKN_FMT_IN_FMT_CFG,    0x012002),
    )


def _build_copier_out_fmt(tokens: dict) -> bytes:
    """Build the ipc4_audio_format (24 bytes) for ipc4_copier_module_cfg.out_fmt.

    Uses OUT tokens when present, falling back to the matching IN token so that
    passthrough copiers (same format in/out) work without explicit OUT tokens.
    """
    return struct.pack('<6I',
        tokens.get(TKN_FMT_OUT_RATE,       tokens.get(TKN_FMT_IN_RATE,       48000)),
        tokens.get(TKN_FMT_OUT_BIT_DEPTH,  tokens.get(TKN_FMT_IN_BIT_DEPTH,  32)),
        tokens.get(TKN_FMT_OUT_CH_MAP,     tokens.get(TKN_FMT_IN_CH_MAP,     0xFFFFFF10)),
        tokens.get(TKN_FMT_OUT_CH_CFG,     tokens.get(TKN_FMT_IN_CH_CFG,     1)),
        tokens.get(TKN_FMT_OUT_INTERLEAVE, tokens.get(TKN_FMT_IN_INTERLEAVE, 0)),
        tokens.get(TKN_FMT_OUT_FMT_CFG,    tokens.get(TKN_FMT_IN_FMT_CFG,   0x012002)),
    )


class WidgetInfo:
    __slots__ = ('name', 'uuid', 'module_id', 'instance_id',
                 'core_id', 'init_data', 'is_scheduler')

    def __init__(self, name, uuid, core_id, init_data):
        self.name        = name
        self.uuid        = uuid
        self.module_id   = None
        self.instance_id = 0
        self.core_id     = core_id
        self.init_data   = init_data
        self.is_scheduler = False


class PipelineInfo:
    __slots__ = ('pipeline_id', 'mem_size', 'priority', 'lp',
                 'core_id', 'widgets', 'connections', 'cross_connections')

    def __init__(self, pipeline_id):
        self.pipeline_id     = pipeline_id
        self.mem_size        = 2
        self.priority        = 0
        self.lp              = 0
        self.core_id         = 0
        self.widgets         = []
        self.connections     = []
        self.cross_connections = []


def parse_topology(tplg_path: Path, tt) -> dict:
    """Parse tplg binary; return {pipeline_id: PipelineInfo}."""
    fmt = tt.TplgBinaryFormat()
    raw = fmt.parse_file(str(tplg_path))

    name_to_info = {}
    name_to_ppl  = {}
    pipelines    = {}
    stream_counters = {}  # per-DMA-type stream index allocation

    for item in raw:
        hdr = item['header']
        if hdr['type'] != 'DAPM_WIDGET':
            continue
        ppl_id = int(hdr['index'])
        if ppl_id not in pipelines:
            pipelines[ppl_id] = PipelineInfo(ppl_id)
        ppl = pipelines[ppl_id]

        for blk in item['blocks']:
            widget    = blk['widget']
            name      = widget['name']
            is_sched  = (str(widget.get('id', '')) == 'SCHEDULER')
            tokens    = _get_tokens(widget['priv'])

            uuid = None
            for arr in widget['priv']:
                for elem in arr['elems']:
                    tok = elem.get('token')
                    tok_name = tok.name if hasattr(tok, 'name') else str(tok)
                    if tok_name == 'SOF_TKN_COMP_UUID':
                        uuid = bytes(elem['uuid'])
                        break
                if uuid is not None:
                    break

            core_id = int(tokens.get(404, 0))

            if is_sched:
                ppl.core_id  = core_id
                ppl.priority = int(tokens.get(201, 0))
                ppl.lp       = 1 if int(tokens.get(205, 0)) else 0
            else:
                extra = _get_init_bytes_from_kcontrols(blk.get('kcontrols', []))
                if 'copier' in name.lower():
                    # Use a deterministic passthrough copier config for QEMU.
                    # Host/DAI copiers get a valid node_id; module-copiers get 0xFFFFFFFF.
                    base_cfg = _build_base_module_cfg(_INLINE_FMT)
                    node_id = _build_copier_node_id(name, stream_counters)
                    dma_buf = _DMA_BUF_SIZE_DEFAULT if node_id != 0xFFFFFFFF else 0
                    extra = (_build_copier_out_fmt(_INLINE_FMT) +
                             struct.pack('<I', 0) +      # copier_feature_mask
                             struct.pack('<IIII', node_id, dma_buf, 0, 0))  # gtw_cfg (incl config_data[0])
                else:
                    base_cfg = _build_base_module_cfg(tokens)
                wi = WidgetInfo(name, uuid, core_id, base_cfg + extra)
                ppl.widgets.append(wi)
                name_to_info[name] = wi
                name_to_ppl[name]  = ppl_id

    cross = []
    for item in raw:
        hdr = item['header']
        if hdr['type'] != 'DAPM_GRAPH':
            continue
        for g in item['blocks']:
            src, dst = g['source'], g['sink']
            sp = name_to_ppl.get(src)
            dp = name_to_ppl.get(dst)
            if sp is None or dp is None:
                continue
            if sp == dp:
                pipelines[sp].connections.append((src, dst))
            else:
                cross.append((src, dst, sp, dp))

    for src, dst, sp, dp in cross:
        if sp in pipelines:
            pipelines[sp].cross_connections.append((src, dst, sp, dp))

    return pipelines


# ---------------------------------------------------------------------------
# Inline topology builder
# ---------------------------------------------------------------------------

def parse_inline_topology(spec: str, pipeline_id: int,
                           name_to_id: dict,
                           iid_counters: dict = None) -> PipelineInfo:
    """Build a PipelineInfo from a 'host->copier->volume->copier->link' string.

    Module IDs are resolved immediately from *name_to_id* (firmware manifest).
    All nodes use the standard 48 kHz / stereo / 16-bit PCM format defined in
    _INLINE_FMT.  Copier nodes (host, link, copier) get the full
    ipc4_copier_module_cfg layout; other nodes get ipc4_base_module_cfg only.

    The pipeline is linear: connections are added left → right in the order
    the nodes appear in the spec string.
    """
    nodes = [n.strip() for n in spec.split('->')]
    nodes = [n for n in nodes if n]          # drop empty segments
    if not nodes:
        raise ValueError(f"Empty inline topology spec: {spec!r}")

    ppl = PipelineInfo(pipeline_id)
    if iid_counters is None:
        iid_counters = {}

    for i, node_type in enumerate(nodes):
        nt = node_type.lower()
        fw_name_candidates = _NODE_TO_FW_NAMES.get(nt, [nt.upper()])

        # Resolve module ID — first matching name in the manifest wins
        mid = None
        resolved_fw_name = None
        for fw_name in fw_name_candidates:
            mid = name_to_id.get(fw_name.upper())
            if mid is not None:
                resolved_fw_name = fw_name
                break

        # Build init data
        if nt in _COPIER_NODE_TYPES:
            # ipc4_copier_module_cfg: base_cfg(40) + out_fmt(24) + feature_mask(4) + gtw_cfg(16)
            # gtw_cfg is struct ipc4_copier_gateway_cfg: node_id(4) + dma_buffer_size(4)
            # + config_length(4) + config_data[1](4) = 16 bytes. The trailing config_data[0]
            # word is part of sizeof(*copier) (84 bytes), so it must be present or copier_init()
            # rejects the payload ("cfg size 84 exceeds init payload 80").
            # For inline chains: first copier is host-playback, last is host-capture,
            # middle copiers are module-copiers (no gateway).
            if nt == 'host':
                if i == 0:
                    node_id = (_DMA_TYPE_HDA_HOST_OUT << 8) | 0
                else:
                    node_id = (_DMA_TYPE_HDA_HOST_IN << 8) | 0
                dma_buf = _DMA_BUF_SIZE_DEFAULT
            elif nt == 'link':
                if i == 0:
                    node_id = (_DMA_TYPE_HDA_LINK_IN << 8) | 0
                else:
                    node_id = (_DMA_TYPE_HDA_LINK_OUT << 8) | 0
                dma_buf = _DMA_BUF_SIZE_DEFAULT
            else:
                node_id = 0xFFFFFFFF
                dma_buf = 0
            init_data = (_build_base_module_cfg(_INLINE_FMT) +
                         _build_copier_out_fmt(_INLINE_FMT) +
                         struct.pack('<I', 0) +      # copier_feature_mask = 0
                         struct.pack('<IIII', node_id, dma_buf, 0, 0))  # gtw_cfg (incl config_data[0])
        else:
            # Other modules: base_cfg only; firmware uses compiled-in defaults
            init_data = _build_base_module_cfg(_INLINE_FMT)
            if nt in _SRC_NODE_TYPES:
                # SRC needs ipc4_config_src: base_cfg(40) + sink_rate(4) = 44 bytes
                sink_rate = _INLINE_FMT.get(TKN_FMT_OUT_RATE,
                                            _INLINE_FMT.get(TKN_FMT_IN_RATE, 48000))
                init_data += struct.pack('<I', sink_rate)

        widget_name = f'{node_type}.{pipeline_id}.{i}'
        wi = WidgetInfo(widget_name, None, 0, init_data)
        wi.module_id = mid

        if mid is not None:
            iid_counters.setdefault(mid, 0)
            wi.instance_id = iid_counters[mid]
            iid_counters[mid] += 1

        if mid is None:
            print(f"  warn: '{node_type}' (pipeline {pipeline_id}): "
                  f"none of {fw_name_candidates} found in firmware manifest — "
                  f"MOD_INIT will be skipped")
        else:
            pass  # success message printed by caller

        ppl.widgets.append(wi)

    # Linear connection chain: widget[0] -> widget[1] -> ... -> widget[N-1]
    for i in range(len(ppl.widgets) - 1):
        ppl.connections.append((ppl.widgets[i].name, ppl.widgets[i + 1].name))

    return ppl


# ---------------------------------------------------------------------------
# IPC4 message builders
# ---------------------------------------------------------------------------

def ipc4_create_pipeline(pipeline_id, mem_size, priority, lp, core_id):
    primary = (
        (mem_size    & 0x7FF) |
        ((priority   & 0x1F) << 11) |
        ((pipeline_id & 0xFF) << 16) |
        (IPC4_GLB_CREATE_PIPELINE << 24)
    )
    extension = (lp & 0x1) | ((core_id & 0xF) << 20)
    return primary, extension


def ipc4_mod_init_instance(module_id, instance_id, pipeline_id, core_id, init_data):
    pad = (-len(init_data)) % 4
    payload = init_data + b'\x00' * pad
    param_block_size = len(payload) // 4
    primary = (
        (module_id    & 0xFFFF) |
        ((instance_id & 0xFF) << 16) |
        (IPC4_MOD_INIT_INSTANCE << 24) |
        (IPC4_MSG_TGT_MODULE    << 30)
    )
    extension = (
        (param_block_size  & 0xFFFF) |
        ((pipeline_id  & 0xFF) << 16) |
        ((core_id      & 0xF)  << 24)
    )
    return primary, extension, payload


def ipc4_mod_bind(src_mod_id, src_iid, src_pin, dst_mod_id, dst_iid, dst_pin):
    primary = (
        (src_mod_id & 0xFFFF) |
        ((src_iid   & 0xFF) << 16) |
        (IPC4_MOD_BIND       << 24) |
        (IPC4_MSG_TGT_MODULE << 30)
    )
    extension = (
        (dst_mod_id & 0xFFFF) |
        ((dst_iid   & 0xFF) << 16) |
        ((dst_pin   & 0x7)  << 24) |
        ((src_pin   & 0x7)  << 27)
    )
    return primary, extension


def ipc4_set_pipeline_state(pipeline_id, state):
    primary = (
        (state        & 0xFFFF) |
        ((pipeline_id & 0xFF) << 16) |
        (IPC4_GLB_SET_PIPELINE_STATE << 24)
    )
    return primary, 0


def ipc4_mod_large_config_set(module_id, instance_id, large_param_id, payload):
    """Build IPC4 SET_LARGE_CONFIG primary/extension for a single-block message."""
    primary = (
        (module_id    & 0xFFFF) |
        ((instance_id & 0xFF) << 16) |
        (IPC4_MOD_LARGE_CONFIG_SET << 24) |
        (IPC4_MSG_TGT_MODULE       << 30)
    )
    extension = (
        (len(payload)       & 0xFFFFF) |       # data_off_size
        ((large_param_id & 0xFF) << 20) |      # large_param_id
        (1 << 28) |                            # final_block
        (1 << 29)                              # init_block
    )
    return primary, extension, payload


def build_gain_volume_payload(volume_q131=0x7FFFFFFF, channel_id=0xFFFFFFFF,
                              curve_type=0, curve_duration_100ns=0):
    """Build ipc4_peak_volume_config blob (24 bytes).

    volume_q131: Q1.31 volume (0x7FFFFFFF = 0 dB unity, 0 = mute)
    channel_id:  0xFFFFFFFF = all channels
    curve_type:  0=NONE(instant), 1=WINDOWS_FADE, 2=LINEAR, 3=LOG
    """
    return struct.pack('<IIIIq',
                       channel_id,
                       volume_q131,
                       curve_type,
                       0,                    # reserved
                       curve_duration_100ns)


# ---------------------------------------------------------------------------
# QEMU HMP monitor client
# ---------------------------------------------------------------------------

class QemuHMPClient:
    _PROMPT  = b'(qemu) '
    _TIMEOUT = 10.0

    def __init__(self, socket_addr: str, verbose: bool = False):
        self._verbose = verbose
        self._sock    = None
        self._buf     = b''
        self._connect(socket_addr)

    def _connect(self, addr: str):
        if addr.startswith('unix:'):
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(addr[5:])
        elif addr.startswith('/') or ':' not in addr:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(addr)
        else:
            host, port_str = addr.rsplit(':', 1)
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((host, int(port_str)))
        s.settimeout(self._TIMEOUT)
        self._sock = s
        self._read_until_prompt()

    def _read_until_prompt(self) -> str:
        deadline = time.monotonic() + self._TIMEOUT
        while True:
            if self._PROMPT in self._buf:
                idx  = self._buf.index(self._PROMPT) + len(self._PROMPT)
                text = self._buf[:idx].decode('utf-8', errors='replace')
                self._buf = self._buf[idx:]
                return text
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("QEMU monitor prompt timeout")
            self._sock.settimeout(remaining)
            try:
                chunk = self._sock.recv(4096)
                if not chunk:
                    raise ConnectionResetError("QEMU monitor connection closed")
                self._buf += chunk
            except socket.timeout:
                raise TimeoutError("QEMU monitor prompt timeout")

    def _send_cmd(self, cmd: str) -> str:
        self._sock.sendall((cmd.strip() + '\n').encode())
        if self._verbose:
            print(f"  >> {cmd.strip()}")
        resp = self._read_until_prompt()
        if resp.startswith(cmd.strip()):
            resp = resp[len(cmd.strip()):].lstrip('\r\n')
        resp = resp.rstrip().removesuffix('(qemu) ').strip()
        if self._verbose and resp:
            print(f"  << {resp}")
        return resp

    def send_ipc(self, primary: int, extension: int = 0,
                 payload: bytes = b'', delay_ms: float = 5.0) -> str:
        cmd = f"ace-ipc-tx 0x{primary:08x} 0x{extension:08x}"
        if payload:
            cmd += ' ' + ':'.join(f'{b:02x}' for b in payload)
        resp = self._send_cmd(cmd)
        if delay_ms > 0:
            time.sleep(delay_ms / 1000.0)
        return resp

    _RX_RE = re.compile(
        r'IPC_RX: busy=(\d) reply_pending=(\d) reply=0x([0-9a-f]+)'
        r' tdr=0x([0-9a-f]+) idr=0x([0-9a-f]+) ida=0x([0-9a-f]+)', re.IGNORECASE)

    def poll_reply(self, timeout_s: float = 2.0, poll_ms: float = 20.0) -> dict | None:
        """Poll ace-ipc-rx until reply_pending=1 (firmware posted a reply) or timeout.

        last_ipc4_reply in QEMU is set when firmware writes idr|BUSY and cleared
        only when the next TX is sent, so it persists even after the firmware ISR
        clears ida.DONE.  Returns a dict or None on timeout.
        """
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            resp = self._send_cmd('ace-ipc-rx')
            m = self._RX_RE.search(resp)
            if m:
                result = {
                    'busy':          int(m.group(1)),
                    'reply_pending': int(m.group(2)),
                    'reply': int(m.group(3), 16),
                    'tdr':   int(m.group(4), 16),
                    'idr':   int(m.group(5), 16),
                    'ida':   int(m.group(6), 16),
                }
                if result['reply_pending'] == 1:
                    return result
                # Busy cleared but no explicit reply yet.  Do NOT return
                # immediately: firmware clears TDR BUSY early in the ISR
                # (before the ipc_cmd work-queue item actually runs), so the
                # caller would race ahead and overwrite the single-slot IPC
                # buffer before firmware processes this message.  Hold here
                # for a grace period so the Zephyr work queue has time to
                # drain before the next IPC is injected.
                if result['busy'] == 0:
                    time.sleep(2.0)
                    # One final check in case a reply arrived during grace period
                    resp2 = self._send_cmd('ace-ipc-rx')
                    m2 = self._RX_RE.search(resp2)
                    if m2 and int(m2.group(2)):  # reply_pending
                        return {
                            'busy':          int(m2.group(1)),
                            'reply_pending': int(m2.group(2)),
                            'reply': int(m2.group(3), 16),
                            'tdr':   int(m2.group(4), 16),
                            'idr':   int(m2.group(5), 16),
                            'ida':   int(m2.group(6), 16),
                        }
                    return result
            time.sleep(poll_ms / 1000.0)
        return None

    def send_ipc_checked(self, primary: int, extension: int = 0,
                         payload: bytes = b'',
                         reply_timeout_s: float = 2.0) -> dict:
        """Send IPC and wait for firmware to post a reply.

        Prints a one-line reply summary.  Raises IPCError on any failure
        (TX rejected, timeout, or non-zero firmware status).
        Returns the poll_reply result dict on success.
        """
        tx_resp = self.send_ipc(primary, extension, payload)
        if tx_resp and 'Error' in tx_resp:
            raise IPCError(f"TX rejected by QEMU: {tx_resp.strip()}")
        result = self.poll_reply(timeout_s=reply_timeout_s)
        if result is None:
            raise IPCError(f"reply TIMEOUT after {reply_timeout_s:.1f}s")
        reply = result['reply']
        status = reply & 0xFFFFFF
        msg_type = (reply >> 24) & 0x1F
        rsp_bit  = (reply >> 29) & 0x1
        if status == 0:
            print(f"     reply: OK  (0x{reply:08x})")
        else:
            decoded = _decode_ipc4_status(status)
            print(f"     reply: ERROR  status={decoded}"
                  f"  type=0x{msg_type:02x}  rsp={rsp_bit}  raw=0x{reply:08x}")
            raise IPCError(
                f"firmware error status={decoded} raw=0x{reply:08x}")
        return result

    def close(self):
        if self._sock:
            self._sock.close()
            self._sock = None


# ---------------------------------------------------------------------------
# Orchestration helpers
# ---------------------------------------------------------------------------

def resolve_module_ids(pipelines: dict, uuid_to_id: dict, verbose: bool):
    warned = set()
    for ppl in pipelines.values():
        iid_counters = {}
        for wi in ppl.widgets:
            if wi.uuid is None:
                if verbose:
                    print(f"  warn: {wi.name!r}: no UUID, skipping")
                continue
            mid = uuid_to_id.get(wi.uuid)
            if mid is None:
                uhex = wi.uuid.hex()
                if uhex not in warned:
                    warned.add(uhex)
                    print(f"  warn: UUID {uhex} ({wi.name!r}) "
                          f"not found in firmware manifest — skipping")
                continue
            wi.module_id = mid
            iid_counters.setdefault(mid, 0)
            wi.instance_id = iid_counters[mid]
            iid_counters[mid] += 1


def send_pipeline(client, ppl: PipelineInfo, name_to_widget: dict,
                  dry_run: bool, verbose: bool):
    print(f"\n--- Pipeline {ppl.pipeline_id} ---")

    p, e = ipc4_create_pipeline(ppl.pipeline_id, ppl.mem_size,
                                 ppl.priority, ppl.lp, ppl.core_id)
    print(f"  CREATE_PIPELINE id={ppl.pipeline_id} mem={ppl.mem_size} "
          f"prio={ppl.priority} lp={ppl.lp} core={ppl.core_id} "
          f"=> 0x{p:08x} 0x{e:08x}")
    if not dry_run:
        try:
            client.send_ipc_checked(p, e)
        except IPCError as exc:
            raise IPCError(
                f"CREATE_PIPELINE id={ppl.pipeline_id}: {exc}") from exc

    for wi in ppl.widgets:
        if wi.module_id is None:
            print(f"  SKIP MOD_INIT {wi.name!r} (no module_id)")
            continue
        p, e, payload = ipc4_mod_init_instance(
            wi.module_id, wi.instance_id, ppl.pipeline_id,
            wi.core_id, wi.init_data)
        print(f"  MOD_INIT {wi.name!r} mod={wi.module_id} "
              f"iid={wi.instance_id} init={len(wi.init_data)}B "
              f"=> 0x{p:08x} 0x{e:08x}")
        if not dry_run:
            try:
                client.send_ipc_checked(p, e, wi.init_data)
            except IPCError as exc:
                # Some firmware/QEMU combinations are picky about copier init payloads.
                # Retry with smaller, safer payloads before giving up.
                if 'copier' not in wi.name.lower():
                    raise IPCError(
                        f"MOD_INIT {wi.name!r} mod={wi.module_id} iid={wi.instance_id}: {exc}"
                    ) from exc

                fallback_payloads = []
                # Try base module cfg only (40 bytes)
                if len(wi.init_data) >= 40:
                    fallback_payloads.append(wi.init_data[:40])
                # Finally, try no payload at all
                fallback_payloads.append(b'')

                recovered = False
                for idx, alt_payload in enumerate(fallback_payloads, start=1):
                    if alt_payload == wi.init_data:
                        continue
                    p2, e2, _ = ipc4_mod_init_instance(
                        wi.module_id, wi.instance_id, ppl.pipeline_id,
                        wi.core_id, alt_payload)
                    print(f"  RETRY MOD_INIT {wi.name!r} attempt={idx} "
                          f"init={len(alt_payload)}B => 0x{p2:08x} 0x{e2:08x}")
                    try:
                        client.send_ipc_checked(p2, e2, alt_payload)
                        wi.init_data = alt_payload
                        recovered = True
                        break
                    except IPCError:
                        continue

                if not recovered:
                    raise IPCError(
                        f"MOD_INIT {wi.name!r} mod={wi.module_id} iid={wi.instance_id}: {exc}"
                    ) from exc

    for src_name, dst_name in ppl.connections:
        sw = name_to_widget.get(src_name)
        dw = name_to_widget.get(dst_name)
        if not sw or not dw or sw.module_id is None or dw.module_id is None:
            print(f"  SKIP BIND {src_name!r} -> {dst_name!r}")
            continue
        p, e = ipc4_mod_bind(sw.module_id, sw.instance_id, 0,
                              dw.module_id, dw.instance_id, 0)
        print(f"  BIND {src_name!r}[0] -> {dst_name!r}[0] "
              f"=> 0x{p:08x} 0x{e:08x}")
        if not dry_run:
            try:
                client.send_ipc_checked(p, e)
            except IPCError as exc:
                raise IPCError(
                    f"BIND {src_name!r} -> {dst_name!r}: {exc}") from exc


def send_cross_binds(client, pipelines: dict, requested_ids: set,
                     name_to_widget: dict, dry_run: bool):
    seen = set()
    for ppl_id in requested_ids:
        ppl = pipelines.get(ppl_id)
        if not ppl:
            continue
        for src, dst, sp, dp in ppl.cross_connections:
            if (src, dst) in seen or sp not in requested_ids or dp not in requested_ids:
                continue
            seen.add((src, dst))
            sw = name_to_widget.get(src)
            dw = name_to_widget.get(dst)
            if not sw or not dw or sw.module_id is None or dw.module_id is None:
                continue
            p, e = ipc4_mod_bind(sw.module_id, sw.instance_id, 0,
                                  dw.module_id, dw.instance_id, 0)
            print(f"  CROSS-BIND {src!r}[0] -> {dst!r}[0] => 0x{p:08x} 0x{e:08x}")
            if not dry_run:
                try:
                    client.send_ipc_checked(p, e)
                except IPCError as exc:
                    raise IPCError(
                        f"CROSS-BIND {src!r} -> {dst!r}: {exc}") from exc


def set_state(client, requested_ids: set, state: int, label: str, dry_run: bool):
    print(f"\n--- Set pipeline state: {label} ---")
    for pid in sorted(requested_ids):
        p, e = ipc4_set_pipeline_state(pid, state)
        print(f"  SET_STATE pipeline={pid} state={label} => 0x{p:08x} 0x{e:08x}")
        if not dry_run:
            try:
                client.send_ipc_checked(p, e)
            except IPCError as exc:
                raise IPCError(
                    f"SET_STATE pipeline={pid} {label}: {exc}") from exc


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)

    ap.add_argument('--machine', '-m', default=None,
                    help='Machine name (e.g. mtl, ptl, lnl) used to auto-discover '
                         'firmware and topology files')
    ap.add_argument('--tplg', action='append', default=None, metavar='SPEC',
                    help='Topology source — may be given multiple times.  '
                         'Two forms are accepted:\n'
                         '  FILE   an existing .tplg binary file (absolute path, '
                         'filename, or basename searched in known directories).\n'
                         '  CHAIN  an inline topology string such as '
                         '"host->copier->volume->copier->link".  Each CHAIN '
                         'becomes one pipeline; 48 kHz stereo 16-bit PCM is '
                         'assumed throughout.  All --tplg values must be the '
                         'same form (all inline or all file).')
    ap.add_argument('--fw', default=None,
                    help='SOF firmware .ri image (default: '
                         '<workspace>/build-<machine>/zephyr/zephyr.ri)')
    ap.add_argument('--socket', default='/tmp/qemu-mon.sock',
                    help='QEMU HMP monitor socket — path or host:port '
                         '(default: /tmp/qemu-mon.sock)')
    ap.add_argument('--pipeline', type=int, nargs='+', default=None,
                    metavar='ID',
                    help='Pipeline IDs to set up (e.g. --pipeline 1 2).  '
                         'Required for file-based topologies.  Optional for '
                         'inline --tplg strings: IDs are auto-assigned 1, 2, … '
                         'unless overridden here.')
    ap.add_argument('--start', action='store_true',
                    help='Transition pipelines to RUNNING, then stop after '
                         '--run-duration seconds')
    ap.add_argument('--run-duration', type=float, default=5.0,
                    metavar='SECS',
                    help='Seconds to run before auto-stop (default: 5.0)')
    ap.add_argument('--dry-run', action='store_true',
                    help='Print IPC messages without connecting or sending')
    ap.add_argument('--verbose', '-v', action='store_true',
                    help='Print raw HMP command I/O')
    ap.add_argument('--tplg-tools-path', default=None,
                    help='Override path to directory containing tplgtool2.py')
    ap.add_argument('--list-tplg', action='store_true',
                    help='List available .tplg files for --machine and exit')
    args = ap.parse_args()

    # --- list mode ---
    if args.list_tplg:
        if not args.machine:
            ap.error('--list-tplg requires --machine')
        hits = _list_tplg_candidates(args.machine)
        if not hits:
            print(f"No .tplg files found for machine '{args.machine}'")
        else:
            print(f"Available topology files for '{args.machine}':")
            for h in hits:
                print(f"  {h}")
        return

    # -----------------------------------------------------------------------
    # Determine mode: inline topology strings vs .tplg file
    # -----------------------------------------------------------------------
    inline_specs = None   # list[str] when using inline A->B->C syntax
    tplg_file_spec = None # str when using an existing .tplg file

    if args.tplg:
        has_inline = any('->' in s for s in args.tplg)
        has_file   = any('->' not in s for s in args.tplg)
        if has_inline and has_file:
            ap.error('Cannot mix inline topology strings (A->B->C) with file '
                     'names in --tplg; use one form or the other.')
        if has_inline:
            inline_specs = args.tplg
        else:
            if len(args.tplg) > 1:
                ap.error('Only one topology file can be specified via --tplg; '
                         'select multiple pipelines with --pipeline instead.')
            tplg_file_spec = args.tplg[0]

    if inline_specs is None and not args.pipeline:
        ap.error('--pipeline is required when using a .tplg file '
                 '(or omit --tplg and use --tplg CHAIN->CHAIN... syntax)')

    # -----------------------------------------------------------------------
    # Resolve file paths (with defaults + announcements)
    # -----------------------------------------------------------------------
    print(f"Workspace root: {WORKSPACE}")

    # tplgtool2 — only needed for file-based topology
    tt = None
    if inline_specs is None:
        tt = _import_tplgtool2(args.tplg_tools_path)
        tools_path = _find_tplg_tools()
        if tools_path:
            print(f"  tplgtool2    : {tools_path}  [auto-discovered]"
                  if not args.tplg_tools_path else
                  f"  tplgtool2    : {args.tplg_tools_path}  [from --tplg-tools-path]")

    # firmware
    fw_path = None
    if args.fw:
        fw_path = Path(args.fw)
        if not fw_path.exists():
            sys.exit(f"error: firmware not found: {fw_path}")
        print(f"  firmware     : {fw_path}  [from --fw]")
    elif args.machine:
        fw_path = _find_firmware(args.machine)
        if fw_path:
            print(f"  firmware     : {fw_path}  [auto-discovered from --machine={args.machine}]")
        else:
            sys.exit(f"error: no firmware found at "
                     f"{WORKSPACE}/build-{args.machine}/zephyr/zephyr.ri\n"
                     f"  build the firmware first, or pass --fw explicitly")
    else:
        sys.exit("error: provide --fw or --machine so firmware can be located")

    # topology — only needed for file-based mode
    tplg_path = None
    if inline_specs is None:
        if tplg_file_spec:
            tplg_path = _find_tplg(args.machine or '', tplg_file_spec)
            if tplg_path:
                print(f"  topology     : {tplg_path}  [resolved from --tplg]")
            else:
                sys.exit(f"error: topology not found: {tplg_file_spec}\n"
                         f"  searched in: {[str(d) for d in _TPLG_SEARCH_DIRS]}")
        elif args.machine:
            tplg_path = _find_tplg(args.machine, None)
            if tplg_path:
                print(f"  topology     : {tplg_path}  [auto-discovered from --machine={args.machine}]")
            else:
                candidates = _list_tplg_candidates(args.machine)
                hint = '\n'.join(f'    {c.name}' for c in candidates[:10])
                sys.exit(
                    f"error: no 'nocodec' topology found for machine '{args.machine}'.\n"
                    f"  Pass --tplg <filename>.  Available files:\n{hint or '    (none found)'}"
                )
        else:
            sys.exit("error: provide --tplg or --machine so topology can be located")

    print(f"  monitor      : {args.socket}")
    print()

    # -----------------------------------------------------------------------
    # Parse manifest + topology
    # -----------------------------------------------------------------------
    print(f"Parsing firmware manifest ...")
    uuid_to_id, name_to_id = parse_manifest(fw_path)
    print(f"  {len(uuid_to_id)} module entries found")
    if args.verbose:
        print(f"  Module names : {sorted(name_to_id.keys())}")

    if inline_specs is not None:
        # -----------------------------------------------------------------
        # Inline topology mode
        # -----------------------------------------------------------------
        if args.pipeline:
            if len(args.pipeline) != len(inline_specs):
                ap.error(f'--pipeline count ({len(args.pipeline)}) must match '
                         f'the number of --tplg specs ({len(inline_specs)})')
            pipeline_ids = args.pipeline
        else:
            pipeline_ids = list(range(1, len(inline_specs) + 1))

        print(f"Building inline pipelines ...")
        all_pipelines = {}
        iid_counters = {}  # shared across all inline pipelines
        for spec, pid in zip(inline_specs, pipeline_ids):
            print(f"  pipeline {pid}: {spec}")
            ppl = parse_inline_topology(spec, pid, name_to_id, iid_counters)
            for wi in ppl.widgets:
                mid_str = (f"mod={wi.module_id} iid={wi.instance_id}"
                           if wi.module_id is not None else "NOT_FOUND")
                print(f"    {wi.name:<30s}  {mid_str}  init={len(wi.init_data)}B")
            all_pipelines[pid] = ppl

        requested_ids = set(pipeline_ids)
        # Auto-detect cross-pipeline mixin→mixout binds:
        # Any pipeline ending in 'mixin' binds to any pipeline starting with 'mixout'.
        mixin_widgets = []   # (widget_name, pipeline_id)
        mixout_widgets = []  # (widget_name, pipeline_id)
        for pid, ppl in all_pipelines.items():
            if ppl.widgets:
                last_w = ppl.widgets[-1]
                if last_w.name.startswith('mixin.'):
                    mixin_widgets.append((last_w.name, pid))
                first_w = ppl.widgets[0]
                if first_w.name.startswith('mixout.'):
                    mixout_widgets.append((first_w.name, pid))
        # Bind each mixin to each mixout (fan-out mixing)
        for mixin_name, src_pid in mixin_widgets:
            for mixout_name, dst_pid in mixout_widgets:
                src_ppl = all_pipelines[src_pid]
                src_ppl.cross_connections.append(
                    (mixin_name, mixout_name, src_pid, dst_pid))

    else:
        # -----------------------------------------------------------------
        # File-based topology mode
        # -----------------------------------------------------------------
        print(f"Parsing topology ...")
        all_pipelines = parse_topology(tplg_path, tt)
        print(f"  {len(all_pipelines)} pipeline(s) found: {sorted(all_pipelines.keys())}")
        for pid in sorted(all_pipelines.keys()):
            ppl = all_pipelines[pid]
            print(f"    pipeline {pid}  (core={ppl.core_id} prio={ppl.priority}"
                  f" lp={ppl.lp} mem={ppl.mem_size})")
            # Build adjacency for pretty chain rendering
            edges = {src: dst for src, dst in ppl.connections}
            # Find chain heads (nodes that are never a dst)
            dsts = set(edges.values())
            heads = [w.name for w in ppl.widgets if w.name not in dsts]
            printed = set()
            for head in heads:
                chain = []
                node = head
                while node:
                    chain.append(node)
                    printed.add(node)
                    node = edges.get(node)
                print("      " + " -> ".join(chain))
            # Any widgets not reachable from a head (isolated or cycles)
            for wi in ppl.widgets:
                if wi.name not in printed:
                    print(f"      {wi.name}  (disconnected)")
            # Cross-pipeline connections originating here
            for src, dst, sp, dp in ppl.cross_connections:
                print(f"      {src} -> [{dst} @ pipeline {dp}]")

        # validate
        requested_ids = set(args.pipeline)
        for pid in requested_ids:
            if pid not in all_pipelines:
                avail = sorted(all_pipelines.keys())
                sys.exit(f"error: pipeline {pid} not in topology (available: {avail})")

        # Warn if none of the requested pipelines connect to each other
        if len(requested_ids) > 1:
            connected_pairs = set()
            for ppl_id in requested_ids:
                for _, _, sp, dp in all_pipelines[ppl_id].cross_connections:
                    if sp in requested_ids and dp in requested_ids:
                        connected_pairs.add((min(sp, dp), max(sp, dp)))
            if not connected_pairs:
                suggestions = set()
                for pid in requested_ids:
                    for _, _, sp, dp in all_pipelines[pid].cross_connections:
                        other = dp if sp == pid else sp
                        suggestions.add(other)
                hint = f"  Try: {sorted(suggestions)}" if suggestions else ""
                print(f"  warning: pipelines {sorted(requested_ids)} have no"
                      f" cross-connections with each other — IPC will be sent"
                      f" but topology will be incomplete.\n{hint}")

    selected = {pid: all_pipelines[pid] for pid in requested_ids}

    if inline_specs is None:
        resolve_module_ids(selected, uuid_to_id, args.verbose)

    name_to_widget = {}
    for ppl in selected.values():
        for wi in ppl.widgets:
            name_to_widget[wi.name] = wi

    # -----------------------------------------------------------------------
    # Connect to QEMU (unless dry-run)
    # -----------------------------------------------------------------------
    client = None
    if not args.dry_run:
        print(f"\nConnecting to QEMU HMP monitor: {args.socket}")
        try:
            client = QemuHMPClient(args.socket, verbose=args.verbose)
            print("  Connected.")
        except (FileNotFoundError, ConnectionRefusedError, OSError) as e:
            sys.exit(f"error: cannot connect to QEMU monitor at {args.socket}: {e}")

    # -----------------------------------------------------------------------
    # CREATE + INIT + BIND
    # -----------------------------------------------------------------------
    try:
        for pid in sorted(requested_ids):
            send_pipeline(client, selected[pid], name_to_widget,
                          args.dry_run, args.verbose)

        if len(requested_ids) > 1:
            print("\n--- Cross-pipeline BIND ---")
            send_cross_binds(client, all_pipelines, requested_ids,
                             name_to_widget, args.dry_run)
        # -------------------------------------------------------------------
        # PAUSED (arm) -> RUNNING -> wait -> PAUSED (stop)
        # -------------------------------------------------------------------
        set_state(client, requested_ids, IPC4_PPL_STATE_PAUSED, 'PAUSED', args.dry_run)

        # Configure gain/volume modules to 0 dB (unity) so data passes through
        gain_widgets = [w for w in name_to_widget.values()
                        if w.name.startswith(('gain.', 'volume.')) and w.module_id is not None]
        if gain_widgets:
            print("\n--- Configure GAIN modules (0 dB) ---")
            vol_payload = build_gain_volume_payload()
            for gw in gain_widgets:
                p, e, pl = ipc4_mod_large_config_set(
                    gw.module_id, gw.instance_id, 0, vol_payload)
                print(f"  SET_LARGE_CONFIG {gw.name!r} mod={gw.module_id} "
                      f"iid={gw.instance_id} param=VOLUME size={len(pl)}B "
                      f"=> 0x{p:08x} 0x{e:08x}")
                if not args.dry_run:
                    client.send_ipc_checked(p, e, pl)

        if args.start:
            set_state(client, requested_ids, IPC4_PPL_STATE_RUNNING, 'RUNNING', args.dry_run)
            print(f"\nPipelines running — stopping after {args.run_duration:.1f}s ...")
            if not args.dry_run:
                time.sleep(args.run_duration)
            set_state(client, requested_ids, IPC4_PPL_STATE_PAUSED, 'PAUSED (stop)', args.dry_run)

    except IPCError as exc:
        if client:
            client.close()
        sys.exit(f"\nFATAL IPC error: {exc}")

    if client:
        client.close()
    print("\nDone.")


if __name__ == '__main__':
    main()
