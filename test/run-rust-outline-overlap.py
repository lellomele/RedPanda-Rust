#!/usr/bin/env python3
"""Run the outline overlap regression with a UI-check build and its fake LSP server.

Example: python test/run-rust-outline-overlap.py /path/to/RedPandaRust /path/to/rust-lsp-checks /path/to/new-output
The Qt runtime and plugins required by both executables must be available.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('ide', type=Path)
parser.add_argument('server', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
ide, server, output = (path.resolve() for path in (args.ide, args.server, args.output))
if not ide.is_file() or not server.is_file():
    parser.error('IDE and fake server executables must exist')
output.mkdir(parents=True, exist_ok=False)
env = os.environ.copy()
env.update(RUST_OUTLINE_EDIT_INPUTS=str(output), RUST_OUTLINE_OVERLAP_CHECK='1',
           REDPANDA_RUST_ANALYZER=str(server), REDPANDA_FAKE_LSP='1',
           REDPANDA_FAKE_LSP_MODE='outline-overlap', REDPANDA_FAKE_LSP_LOG=str(output / 'traffic.jsonl'))
for name in ('RUST_OUTLINE_CHECK_SOURCE', 'RUST_OUTLINE_CHECK_PHASE', 'RUST_OUTLINE_EDIT_ORIGINALS', 'RUST_OUTLINE_BOUNDED_RETRY'):
    env.pop(name, None)
with (output / 'process.log').open('wb') as log:
    result = subprocess.run([str(ide), '--rust-ui-test-output', str(output)], env=env, stdout=log, stderr=subprocess.STDOUT, timeout=150)
report_path = output / 'checks.json'
if not report_path.is_file():
    raise SystemExit('UI-check executable did not produce a report; see process.log')
report = json.loads(report_path.read_text(encoding='utf-8'))
print(json.dumps(report, ensure_ascii=False, indent=2))
raise SystemExit(1 if result.returncode or report.get('failed', 1) else 0)
