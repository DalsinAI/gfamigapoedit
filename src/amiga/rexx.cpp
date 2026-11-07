// AmigaOS: Poedit's ARexx port, POEDIT.1, on the GFOpenWX Toolkit's
// wxARexxPort and arexx.class (patch 0007 opens it in OnInit and closes it
// in OnExit):
//   OPEN FILE/A   open a translation (.po, .xliff, .json ...) in a window
//   SAVE          save the front window's translation (and its .mo)
//   STATS         RESULT: the front window's entries: all,
//                 finished (as the status bar's per cent), fuzzy,
//                 untranslated, with errors ("120 100 12 8 0")
// and the toolkit's QUIT, ACTIVATE, DEACTIVATE, VERSION, HELP, FAULT. The
// front window is the active one, else the first one; RC 10 when there is
// no window with a translation.
// Copyright (c) 2026 AmiCradle Team. MIT, like Poedit.
#include "edapp.h"
#include "edframe.h"
#include "catalog.h"
#include "version.h"
#include <wx/filefn.h>
#include <wx/openlayout/arexx.h>

class AmigaRexx
{
public:
    static PoeditFrame *Front(wxARexxCommand& c)
    {
        PoeditFrame *front = nullptr;
        for (auto *f : PoeditFrame::GetInstances())
        {
            if (!f->m_catalog)
                continue;
            if (!front || f->IsActive())
                front = f;
        }
        if (!front)
            c.SetError(10);
        return front;
    }

    static wxARexxPort *Start()
    {
        auto *rexx = new wxARexxPort("POEDIT", wxString("Poedit ") + POEDIT_VERSION);
        rexx->Add("OPEN", "FILE/A", [](wxARexxCommand& c) {
            const wxString file = c.Str(0);
            if (!wxFileExists(file))
                c.SetError(10, 205);    // object not found
            else
            {
                wxArrayString names;
                names.push_back(file);
                if (wxGetApp().OpenFiles(names) == 0)
                    c.SetError(10);
            }
        });
        rexx->Add("SAVE", "", [](wxARexxCommand& c) {
            if (PoeditFrame *f = Front(c))
            {
                wxCommandEvent ev(wxEVT_MENU, wxID_SAVE);
                ev.SetEventObject(f);
                f->GetEventHandler()->ProcessEvent(ev);
            }
        });
        rexx->Add("STATS", "", [](wxARexxCommand& c) {
            if (PoeditFrame *f = Front(c))
            {
                int all = 0, fuzzy = 0, bad = 0, untranslated = 0, unfinished = 0;
                f->m_catalog->GetStatistics(&all, &fuzzy, &bad, &untranslated, &unfinished);
                c.SetResult(wxString::Format("%d %d %d %d %d", all, all - unfinished,
                                             fuzzy, untranslated, bad));
            }
        });
        rexx->Open();
        return rexx;
    }
};

static wxARexxPort *gs_rexx;

void AmigaRexxStart() { if (!gs_rexx) gs_rexx = AmigaRexx::Start(); }
void AmigaRexxStop() { delete gs_rexx; gs_rexx = nullptr; }
