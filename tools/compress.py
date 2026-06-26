# Copyright (C) 2025 Ricardo Guzman - CA2RXU
#
# This file is part of LoRa APRS Tracker.
#
# LoRa APRS Tracker is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# LoRa APRS Tracker is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with LoRa APRS Tracker. If not, see <https://www.gnu.org/licenses/>.

import gzip
import io
import os
import datetime
import subprocess
Import("env")

files = [
  'data_embed/index.html',
  'data_embed/script.js',
  'data_embed/style.css',
  'data_embed/bootstrap.js',
  'data_embed/bootstrap.css',
  'data_embed/favicon.png',
]

# Get version string directly from git — no dependency on the generated header file.
# Content-based .gz comparison below handles version changes without mtime tricks.
def _git_version():
  try:
    return subprocess.check_output(
      ["git", "describe", "--tags", "--always", "--dirty"],
      stderr=subprocess.DEVNULL
    ).decode().strip()
  except Exception:
    return "unknown"

versionDate = _git_version() or "unknown"

# Deterministic "build date": newest mtime across tracked web-UI source files
# and this script. Version string changes are captured by the content-based
# .gz comparison below (not mtime), so no version-file dependency needed here.
_input_files = list(files) + ['tools/compress.py']
_latest_input_mtime = max(os.path.getmtime(p) for p in _input_files)
build_date_str = datetime.datetime.utcfromtimestamp(_latest_input_mtime).strftime('%Y-%m-%d %H:%M:%S') + " UTC"

def _gzip_deterministic(data):
  # mtime=0 strips the gzip-header timestamp so identical input bytes produce
  # identical output bytes; otherwise gzip.compress() stamps the current time
  # and every build differs.
  buf = io.BytesIO()
  with gzip.GzipFile(fileobj=buf, mode='wb', compresslevel=9, mtime=0) as gz:
    gz.write(data)
  return buf.getvalue()

for src in files:
  out = src + ".gz"

  with open(src, 'rb') as f:
    content = f.read()

  if src == 'data_embed/index.html':
    # env["BUILD_FLAGS"] may contain tuples (e.g. ("NAME","VALUE")) when CPPDEFINES
    # are appended dynamically by other pre-scripts (gen_version.py). Coerce every
    # element to str so the join never raises TypeError regardless of PlatformIO version.
    flags_str = ','.join(str(f) for f in env.get("BUILD_FLAGS", [])) \
                  .replace('-Werror -Wall,', '') \
                  .replace(',-DELEGANTOTA_USE_ASYNC_WEBSERVER=1', '')
    env_vars = str(env.get("BOARD", "")) + "<br>" + flags_str + "<br>" + "Version date: " + versionDate
    build_info = f'{env_vars}<br>Build date: {build_date_str}'.encode()
    content = content.replace(b'%BUILD_INFO%', build_info)

  new_bytes = _gzip_deterministic(content)

  # Skip the write when the .gz on disk already matches. Rewriting bumps mtime
  # and forces SCons to re-link / recompile every TU that pulls the file in
  # via [env:esp32].board_build.embed_files, even when nothing actually
  # changed.
  existing = None
  if os.path.exists(out):
    with open(out, 'rb') as f:
      existing = f.read()
  if existing == new_bytes:
    continue

  with open(out, 'wb') as f:
    f.write(new_bytes)
