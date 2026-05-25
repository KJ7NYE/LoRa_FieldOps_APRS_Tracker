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
Import("env")

files = [
  'data_embed/index.html',
  'data_embed/script.js',
  'data_embed/style.css',
  'data_embed/bootstrap.js',
  'data_embed/bootstrap.css',
  'data_embed/favicon.png',
]

string_to_find_str = "String"
string_to_find_ver = "versionDate"

CPP_SRC = 'src/LoRa_APRS_Tracker.cpp'

with open(CPP_SRC, encoding='utf-8') as cpp_file:
  for line in cpp_file:
    if string_to_find_str in line and string_to_find_ver in line:
      start = line.find('"') + 1
      end = line.find('"', start)
      if start > 0 and end > start:
        versionDate = line[start:end]
        break

# Deterministic "build date": newest mtime across all inputs that can affect
# the embedded web UI. Since we only regenerate a .gz when its inputs actually
# changed (see below), this stamp also only changes when something real
# changed — turning identical inputs into byte-identical firmware and
# letting SCons cache configuration.cpp + friends across no-op builds.
_input_files = list(files) + [CPP_SRC, 'tools/compress.py']
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
    env_vars = env["BOARD"] + "<br>" + ','.join(env["BUILD_FLAGS"]).replace('-Werror -Wall,', '').replace(',-DELEGANTOTA_USE_ASYNC_WEBSERVER=1', '') + "<br>" + "Version date: " + versionDate
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
