#!/usr/bin/env python3
# compiled_xrc.cpp for Poedit: its .xrc files built into the program, with
# InitXmlResource() loading them from wx's memory file system, as Poedit's
# own build makes it with wxrc -c (this way needs no host wxrc).
#   gen-xrc.py OUT.cpp FILE.xrc...
# Copyright (c) 2026 AmiCradle Team
import os
import sys

out, files = sys.argv[1], sys.argv[2:]
w = []
w.append('// made by gen-xrc.py from ' + ' '.join(os.path.basename(f) for f in files))
w.append('#include <wx/wxprec.h>')
w.append('#include <wx/filesys.h>')
w.append('#include <wx/fs_mem.h>')
w.append('#include <wx/xrc/xmlres.h>')
w.append('#include <wx/xrc/xh_all.h>')
w.append('')
for i, f in enumerate(files):
    data = open(f, 'rb').read()
    w.append(f'static const size_t xml_res_size_{i} = {len(data)};')
    w.append(f'static const unsigned char xml_res_file_{i}[] = {{')
    for k in range(0, len(data), 16):
        w.append('  ' + ','.join(str(b) for b in data[k:k + 16]) + ',')
    w.append('  0};')
    w.append('')
w.append('void InitXmlResource()')
w.append('{')
w.append('    // the memory file system, if the program has not added it yet')
w.append('    {')
w.append('        wxMemoryFSHandler::AddFile(wxT("XRC_resource/dummy_file"), wxT("dummy one"));')
w.append('        wxFileSystem fsys;')
w.append('        wxFSFile *f = fsys.OpenFile(wxT("memory:XRC_resource/dummy_file"));')
w.append('        wxMemoryFSHandler::RemoveFile(wxT("XRC_resource/dummy_file"));')
w.append('        if (f) delete f;')
w.append('        else wxFileSystem::AddHandler(new wxMemoryFSHandlerBase);')
w.append('    }')
for i, f in enumerate(files):
    n = os.path.basename(f)
    w.append(f'    wxMemoryFSHandler::AddFileWithMimeType(wxT("XRC_resource/{n}"), xml_res_file_{i}, xml_res_size_{i}, wxT("text/xml"));')
for f in files:
    n = os.path.basename(f)
    w.append(f'    wxXmlResource::Get()->Load(wxT("memory:XRC_resource/{n}"));')
w.append('}')
open(out, 'w').write('\n'.join(w) + '\n')
