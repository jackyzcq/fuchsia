#!/usr/bin/env python

import os
import re
import subprocess
import sys

source_dir = os.path.dirname(__file__)
zircon_include_dir = os.path.join(source_dir,
                                  '../../../../zircon/system/public/zircon')

file_header = """// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of zircon;

// This is autogenerated from Zircon headers. Do not edit directly.
// Generated by //garnet/public/dart-pkg/zircon/extract-zircon-constants.py

// ignore_for_file: constant_identifier_names
"""

prefix = 'ZX_'
prefix_len = len(prefix)


def extract_defines(header):
  """Extract the C macro defines from a header file."""
  defines = []
  for line in open(header):
    line = line.rstrip()
    if not line.startswith('#define'):
      continue
    match = re.match(r'#define\s+(?P<symbol>[A-Z0-9_]+)\s+(?P<value>\S.*)',
                     line)
    if match:
      defines.append((match.groupdict()['symbol'], match.groupdict()['value']))
    else:
      # ignore function-like macros
      if not re.match(r'#define\s+[A-Za-z0-9_]+\(', line):
        print 'Unrecognized line: %s in %s' % (line, header)
        sys.exit(1)

  # quadratic time is best time
  return [(k[prefix_len:], c_to_dart_value(v, defines)) for k, v in defines
          if k.startswith(prefix)]


def c_to_dart_value(value, defines):
  """Convert a C macro value to something that can be a Dart constant."""
  # expand macro references
  for k, v in defines:
    value = value.replace(k, v)

  # strip type casts
  value = re.sub(r'\([a-z0-9_]+_t\)', '', value)

  # strip u suffix from decimal integers
  value = re.sub(r'([0-9]+)u', r'\1', value)

  # strip u suffix from hex integers
  value = re.sub(r'(0x[a-fA-F0-9]+)u', r'\1', value)

  # replace a C constant that's used in the headers
  value = re.sub(r'UINT64_MAX', r'0xFFFFFFFFFFFFFFFF', value)

  # strip outer parens
  value = re.sub(r'^\((.*)\)$', r'\1', value)
  return value


def dart_writer(path):
  """Returns a file object that writes Dart code to the supplied path.
  The code is passed through dartfmt to ensure correct formatting."""
  dest = open(path, 'w')
  return subprocess.Popen(['dartfmt'], stdin=subprocess.PIPE, stdout=dest).stdin


def write_constants():
  path = os.path.join(source_dir, 'lib/src/constants.dart')
  error_defines = extract_defines(os.path.join(zircon_include_dir, 'errors.h'))
  type_defines = extract_defines(os.path.join(zircon_include_dir, 'types.h'))
  with dart_writer(path) as f:
    f.write(file_header)
    f.write('abstract class ZX {\n')
    f.write('  ZX._();')
    for symbol, value in error_defines + type_defines:
      f.write('  static const int %s = %s;\n' % (symbol, value))
    f.write('}\n')

    f.write('String getStringForStatus(int status) {\n')
    f.write('  switch(status) {\n')
    for symbol, value in error_defines:
      f.write('    case ZX.%s: return "%s";\n' % (symbol, symbol))
    f.write('    default: return "(unknown: $status)";\n')
    f.write('  }\n')
    f.write('}\n')


write_constants()
