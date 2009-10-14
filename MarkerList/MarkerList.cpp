/******************************************************************************
/ MarkerList.cpp
/
/ Copyright (c) 2009 Tim Payne (SWS)
/ http://www.standingwaterstudios.com/reaper
/
/ Permission is hereby granted, free of charge, to any person obtaining a copy
/ of this software and associated documentation files (the "Software"), to deal
/ in the Software without restriction, including without limitation the rights to
/ use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
/ of the Software, and to permit persons to whom the Software is furnished to
/ do so, subject to the following conditions:
/ 
/ The above copyright notice and this permission notice shall be included in all
/ copies or substantial portions of the Software.
/ 
/ THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
/ EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
/ OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
/ NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
/ HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
/ WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
/ FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
/ OTHER DEALINGS IN THE SOFTWARE.
/
******************************************************************************/

#include "stdafx.h"
#include "../Utility/SectionLock.h"
#include "MarkerListClass.h"
#include "MarkerList.h"
#include "MarkerListActions.h"

#define SAVEWINDOW_POS_KEY "Markerlist Save Window Position"

#define DELETE_MSG		0x100F0
#define SELNEXT_MSG		0x100F1
#define SELPREV_MSG		0x100F2
#define FIRST_LOAD_MSG	0x10100

// Globals
static SWSProjConfig<WDL_PtrList<MarkerList> > g_savedLists;
MarkerList* g_curList = NULL;
SWS_MarkerListWnd* pMarkerList = NULL;

static SWS_LVColumn g_cols[] = { { 75, 0, "Time" }, { 45, 0, "Type" }, { 30, 0, "ID" }, { 170, 1, "Description" } };

SWS_MarkerListView::SWS_MarkerListView(HWND hwndList, HWND hwndEdit)
:SWS_ListView(hwndList, hwndEdit, 4, g_cols, "MarkerList View State", ListComparo, false)
{
}

int CALLBACK SWS_MarkerListView::ListComparo(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	int iCol = (int)lParamSort;
	MarkerItem* item1 = (MarkerItem*)lParam1;
	MarkerItem* item2 = (MarkerItem*)lParam2;
	int iRet = 0;

	switch (abs(iCol))
	{
	case 1: // Time
		if (item1->m_dPos > item2->m_dPos)
			iRet = 1;
		else if (item1->m_dPos < item2->m_dPos)
			iRet = -1;
		break;
	case 2: // Type
		if (item1->m_bReg && !item2->m_bReg)
			iRet = 1;
		else if (!item1->m_bReg && item2->m_bReg)
			iRet = -1;
		break;
	case 3: // ID
		if (item1->m_id > item2->m_id)
			iRet = 1;
		else if (item1->m_id < item2->m_id)
			iRet = -1;
		break;
	case 4: // Desc
		if (!item1->m_cName)
			iRet = 1;
		else if (!item2->m_cName)
			iRet = -1;
		else
			iRet = strcmp(item1->m_cName, item2->m_cName);
		break;
	}
	if (iCol < 0)
		return -iRet;
	else
		return iRet;
}

void SWS_MarkerListView::GetItemText(LPARAM item, int iCol, char* str, int iStrMax)
{
	MarkerItem* mi = (MarkerItem*)item;
	if (mi)
	{
		switch (iCol)
		{
		case 0:
			format_timestr_pos(mi->m_dPos, str, iStrMax, -1);
			break;
		case 1:
			_snprintf(str, iStrMax, "%s", mi->m_bReg ? "Region" : "Marker");
			break;
		case 2:
			_snprintf(str, iStrMax, "%d", mi->m_id);
			break;
		case 3:
			if (mi->m_cName)
				lstrcpyn(str, mi->m_cName, iStrMax);
			else
				str[0] = 0;
			break;
		}
	}
}

bool SWS_MarkerListView::OnItemSelChange(LPARAM item, bool bSel)
{
	if (bSel)
	{
		MarkerItem* mi = (MarkerItem*)item;
		if (mi->m_dPos != GetCursorPosition())
			SetEditCurPos(mi->m_dPos, false, false);
	}
	return false;
}

void SWS_MarkerListView::OnItemDblClk(LPARAM item, int iCol)
{
	MarkerItem* mi = (MarkerItem*)item;
	if (mi->m_bReg)
		GetSet_LoopTimeRange(true, true, &mi->m_dPos, &mi->m_dRegEnd, false);
}

void SWS_MarkerListView::SetItemText(LPARAM item, int iCol, const char* str)
{
	if (iCol == 3)
	{
		MarkerItem* mi = (MarkerItem*)item;
		mi->SetName(str);
		SetProjectMarker(mi->m_id, mi->m_bReg, mi->m_dPos, mi->m_dRegEnd, str);
		Update();
	}
}

int SWS_MarkerListView::GetItemCount()
{
	return g_curList->m_items.GetSize();
}

LPARAM SWS_MarkerListView::GetItemPointer(int iItem)
{
	return (LPARAM)g_curList->m_items.Get(iItem);
}

bool SWS_MarkerListView::GetItemState(LPARAM item)
{
	MarkerItem* mi = (MarkerItem*)item;
	double dPos = GetCursorPosition();
	return GetCursorPosition() == mi->m_dPos;
}

SWS_MarkerListWnd::SWS_MarkerListWnd()
:SWS_DockWnd(IDD_MARKERLIST, "Marker List", 30001)
{
	if (m_bShowAfterInit)
		Show(false, false);
}

void SWS_MarkerListWnd::Update()
{
	// Change the time string if the project time mode changes
	static int prevTimeMode = -1;
	bool bChanged = false;
	if (*(int*)GetConfigVar("projtimemode") != prevTimeMode)
	{
		prevTimeMode = *(int*)GetConfigVar("projtimemode");
		bChanged = true;
	}

	static double dCurPos = DBL_MAX;
	if (GetCursorPosition() != dCurPos)
	{
		dCurPos = GetCursorPosition();
		bChanged = true;
	}

	if (!g_curList)
	{
		g_curList = new MarkerList("CurrentList", true);
		bChanged = true;
	}
	else if (g_curList->BuildFromReaper())
		bChanged = true;

	if (m_pList && bChanged)
	{
		SectionLock lock(g_curList->m_hLock);
		m_pList->Update();
	}
}

void SWS_MarkerListWnd::OnInitDlg()
{
	m_resize.init_item(IDC_LIST, 0.0, 0.0, 1.0, 1.0);
	m_pList = new SWS_MarkerListView(GetDlgItem(m_hwnd, IDC_LIST), GetDlgItem(m_hwnd, IDC_EDIT));
	delete g_curList;
	g_curList = NULL;
	Update();

	SetTimer(m_hwnd, 0, 500, NULL);
}

void SWS_MarkerListWnd::OnCommand(WPARAM wParam, LPARAM lParam)
{
	switch (wParam)
	{
		case DELETE_MSG:
			if (ListView_GetSelectedCount(m_pList->GetHWND()))
			{
				Undo_BeginBlock();
				LVITEM li;
				li.mask = LVIF_STATE | LVIF_PARAM;
				li.stateMask = LVIS_SELECTED;
				li.iSubItem = 0;
				for (int i = 0; i < ListView_GetItemCount(m_pList->GetHWND()); i++)
				{
					li.iItem = i;
					ListView_GetItem(m_pList->GetHWND(), &li);
					if (li.state == LVIS_SELECTED)
					{
						MarkerItem* item = (MarkerItem*)li.lParam;
						DeleteProjectMarker(NULL, item->m_id, item->m_bReg);
					}
				}
				Undo_EndBlock("Delete marker(s)", UNDO_STATE_MISCCFG);
				Update();
				break;
			}
		case SELPREV_MSG:
		{
			HWND hwndList = GetDlgItem(m_hwnd, IDC_LIST);
			int i;
			for (i = 0; i < ListView_GetItemCount(hwndList); i++)
				if (ListView_GetItemState(hwndList, i, LVIS_SELECTED))
					break;
			if (i >= ListView_GetItemCount(hwndList))
				i = 1;

			if (i > 0)
			{
				i--;
				SetEditCurPos(((MarkerItem*)m_pList->GetListItem(i))->m_dPos, true, true);
				ListView_SetItemState(hwndList, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			}
			break;
		}
		case SELNEXT_MSG:
		{
			HWND hwndList = GetDlgItem(m_hwnd, IDC_LIST);
			int i;
			for (i = ListView_GetItemCount(hwndList)-1; i >= 0; i--)
				if (ListView_GetItemState(hwndList, i, LVIS_SELECTED))
					break;
			if (i < 0)
				i = ListView_GetItemCount(hwndList) - 2;

			if (i < ListView_GetItemCount(hwndList) - 1)
			{
				i++;
				SetEditCurPos(((MarkerItem*)m_pList->GetListItem(i))->m_dPos, true, true);
				ListView_SetItemState(hwndList, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			}
			break;
		}
		default:
			if (wParam >= FIRST_LOAD_MSG && wParam - FIRST_LOAD_MSG < (UINT)g_savedLists.Get()->GetSize())
			{	// Load marker list
				g_savedLists.Get()->Get(wParam - FIRST_LOAD_MSG)->UpdateReaper();
				Update();
			}
			else
				Main_OnCommand((int)wParam, (int)lParam);
	}
}

HMENU SWS_MarkerListWnd::OnContextMenu(int x, int y)
{
	HMENU hMenu = CreatePopupMenu();
	AddToMenu(hMenu, "Save marker set...", SWSGetCommandID(SaveMarkerList));
	AddToMenu(hMenu, "Delete market set...", SWSGetCommandID(DeleteMarkerList));

	if (g_savedLists.Get()->GetSize())
		AddToMenu(hMenu, SWS_SEPARATOR, 0);

	char str[256];
	for (int i = 0; i < g_savedLists.Get()->GetSize(); i++)
	{
		sprintf(str, "Load %s", g_savedLists.Get()->Get(i)->m_name);
		AddToMenu(hMenu, str, FIRST_LOAD_MSG+i);
	}

	AddToMenu(hMenu, SWS_SEPARATOR, 0);
	AddToMenu(hMenu, "Copy marker set to clipboard", SWSGetCommandID(ListToClipboard));
	AddToMenu(hMenu, "Paste marker set from clipboard", SWSGetCommandID(ClipboardToList));
	AddToMenu(hMenu, SWS_SEPARATOR, 0);
	AddToMenu(hMenu, "Reorder marker IDs", SWSGetCommandID(RenumberIds));
	AddToMenu(hMenu, "Reorder region IDs", SWSGetCommandID(RenumberRegions));
	AddToMenu(hMenu, SWS_SEPARATOR, 0);
	AddToMenu(hMenu, "Delete selected marker(s)", DELETE_MSG);
	AddToMenu(hMenu, "Delete all markers", SWSGetCommandID(DeleteAllMarkers));
	AddToMenu(hMenu, "Delete all regions", SWSGetCommandID(DeleteAllRegions));
	AddToMenu(hMenu, SWS_SEPARATOR, 0);
	AddToMenu(hMenu, "Export tracklist to clipboard", SWSGetCommandID(ExportToClipboard));
	AddToMenu(hMenu, "Format tracklist...", SWSGetCommandID(ExportFormat));
	
	return hMenu;
}

void SWS_MarkerListWnd::OnDestroy()
{
	KillTimer(m_hwnd, 0);

	m_pList->OnDestroy();
}

void SWS_MarkerListWnd::OnTimer()
{
	if (ListView_GetItemCount(m_pList->GetHWND()) <= 1 || !IsActive())
		Update();
}

INT_PTR WINAPI doSaveDialog(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
		case WM_INITDIALOG:
		{
			HWND list = GetDlgItem(hwndDlg, IDC_COMBO);
			for (int i = 0; i < g_savedLists.Get()->GetSize(); i++)
				SendMessage(list, CB_ADDSTRING, 0, (LPARAM)g_savedLists.Get()->Get(i)->m_name);
			RestoreWindowPos(hwndDlg, SAVEWINDOW_POS_KEY);
			return 0;
		}
		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
				case IDOK:
				{
					char str[256];
					GetDlgItemText(hwndDlg, IDC_COMBO, str, 256);
					if (strlen(str) && EnumProjectMarkers(0, NULL, NULL, NULL, NULL, NULL))
					{	// Don't save a blank string, and don't save an empty list
						// Check to see if there's already a saved spot with that name (and overwrite if necessary)
						int i;
						for (i = 0; i < g_savedLists.Get()->GetSize(); i++)
							if (_strcmpi(g_savedLists.Get()->Get(i)->m_name, str) == 0)
							{
								delete g_savedLists.Get()->Get(i);
								g_savedLists.Get()->Set(i, new MarkerList(str, true));
								return 0;
							}
						g_savedLists.Get()->Add(new MarkerList(str, true));
					}
				}
				// Fall through to cancel to save/end
				case IDCANCEL:
					SaveWindowPos(hwndDlg, SAVEWINDOW_POS_KEY);
					EndDialog(hwndDlg, 0);
					break;
			}
			break;
	}
	return 0;
}

INT_PTR WINAPI doLoadDialog(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
		case WM_INITDIALOG:
		{
			HWND list = GetDlgItem(hwndDlg, IDC_COMBO);
			for (int i = 0; i < g_savedLists.Get()->GetSize(); i++)
				SendMessage(list, CB_ADDSTRING, 0, (LPARAM)g_savedLists.Get()->Get(i)->m_name);
            SendMessage(list, CB_SETCURSEL, 0, 0);
			SetWindowText(hwndDlg, "Load Marker Set");
			RestoreWindowPos(hwndDlg, SAVEWINDOW_POS_KEY);
			return 0;
		}
		case WM_COMMAND:
			switch(LOWORD(wParam))
			{
				case IDOK:
				{
					HWND list = GetDlgItem(hwndDlg, IDC_COMBO);
					int newList = (int)SendMessage(list, CB_GETCURSEL, 0, 0);
					g_savedLists.Get()->Get(newList)->UpdateReaper();
				}
				// Fall through to cancel to save/end
				case IDCANCEL:
					SaveWindowPos(hwndDlg, SAVEWINDOW_POS_KEY);
					EndDialog(hwndDlg, 0);
					break;
			}
			break;
	}
	return 0;
}

INT_PTR WINAPI doDeleteDialog(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
		case WM_INITDIALOG:
		{
			HWND list = GetDlgItem(hwndDlg, IDC_COMBO);
			for (int i = 0; i < g_savedLists.Get()->GetSize(); i++)
				SendMessage(list, CB_ADDSTRING, 0, (LPARAM)g_savedLists.Get()->Get(i)->m_name);
            SendMessage(list, CB_SETCURSEL, 0, 0);
			SetWindowText(hwndDlg, "Delete Marker Set");
			RestoreWindowPos(hwndDlg, SAVEWINDOW_POS_KEY);
			return 0;
		}
		case WM_COMMAND:
			switch(LOWORD(wParam))
			{
				case IDOK:
				{
					HWND list = GetDlgItem(hwndDlg, IDC_COMBO);
					int removeList = (int)SendMessage(list, CB_GETCURSEL, 0, 0);
					g_savedLists.Get()->Delete(removeList, true);
				}
				// Fall through to cancel to save/end
				case IDCANCEL:
					SaveWindowPos(hwndDlg, SAVEWINDOW_POS_KEY);
					EndDialog(hwndDlg, 0);
					break;
			}
			break;
	}
	return 0;
}

INT_PTR WINAPI doFormatDialog(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
		case WM_INITDIALOG:
		{
			HWND format = GetDlgItem(hwndDlg, IDC_EDIT);
			HWND desc = GetDlgItem(hwndDlg, IDC_DESC);
			char str[256];
			GetPrivateProfileString(SWS_INI, TRACKLIST_FORMAT_KEY, TRACKLIST_FORMAT_DEFAULT, str, 256, get_ini_file());
			SetWindowText(format, str);
			SetWindowText(desc,
				"Export format string:\r\n"
				"First char one of a/r/m\r\n"
				"  (all / only regions / only markers)\r\n"
				"Then, in any order, n, i, l, d, t\r\n"
				"n = number (count), starts at 1\r\n"
				"i = ID\r\n"
				"l = Length in H:M:S\r\n"
				"d = Description\r\n"
				"t = Absolute time in H:M:S\r\n"
				"s = Absolute time in project samples");
			return 0;
		}
		case WM_COMMAND:
			if (LOWORD(wParam) == IDCANCEL)
				EndDialog(hwndDlg, 0);
			else if (LOWORD(wParam) == IDOK)
			{
				HWND format = GetDlgItem(hwndDlg, IDC_EDIT);
				char str[256];
				GetWindowText(format, str, 256);
				if (str[0] == 'a' || str[0] == 'r' || str[0] == 'm')
					WritePrivateProfileString(SWS_INI, TRACKLIST_FORMAT_KEY, str, get_ini_file());
				EndDialog(hwndDlg, 0);
			}
			break;
	}
	return 0;
}

void OpenMarkerList(COMMAND_T*)
{
	pMarkerList->Show(true, true);
}

void LoadMarkerList(COMMAND_T*)
{
	DialogBox(g_hInst,MAKEINTRESOURCE(IDD_LOAD),g_hwndParent,doLoadDialog);
}

void SaveMarkerList(COMMAND_T*)
{
	DialogBox(g_hInst,MAKEINTRESOURCE(IDD_SAVE),g_hwndParent,doSaveDialog);
}

void DeleteMarkerList(COMMAND_T*)
{
	DialogBox(g_hInst,MAKEINTRESOURCE(IDD_LOAD),g_hwndParent,doDeleteDialog);
}

void ExportFormat(COMMAND_T*)
{
	DialogBox(g_hInst,MAKEINTRESOURCE(IDD_FORMAT),g_hwndParent,doFormatDialog);
}

static COMMAND_T g_commandTable[] = 
{
	{ { { FSHIFT   | FCONTROL | FVIRTKEY, 'M', 0 }, "SWS: Open marker list" },					"SWSMARKERLIST1",  OpenMarkerList,    "Show SWS MarkerList",  },
	{ { DEFACCEL, NULL }, NULL, NULL, SWS_SEPARATOR, },
	{ { DEFACCEL,                                "SWS: Load marker set" },					"SWSMARKERLIST2",  LoadMarkerList,    "Load marker set...",   },
	{ { DEFACCEL,                                "SWS: Save marker set" },					"SWSMARKERLIST3",  SaveMarkerList,    "Save marker set...",   },
	{ { DEFACCEL,                                "SWS: Delete marker set" },					"SWSMARKERLIST4",  DeleteMarkerList,  "Delete marker set...", },
	{ { DEFACCEL, NULL }, NULL, NULL, SWS_SEPARATOR, },
	{ { DEFACCEL,                                "SWS: Copy marker set to clipboard" },		"SWSMARKERLIST5",  ListToClipboard,   "Copy marker set to clipboard", },
	{ { DEFACCEL,                                "SWS: Copy markers in time selection to clipboard (relative to selection start)" },	"SWSML_TOCLIPTIMESEL",  ListToClipboardTimeSel, NULL /*  "Copy markers in time selection to clipboard (relative to selection start)"*/, },
	{ { DEFACCEL,                                "SWS: Paste marker set from clipboard" },	"SWSMARKERLIST6",  ClipboardToList,   "Paste marker set from clipboard", },
	{ { DEFACCEL, NULL }, NULL, NULL, SWS_SEPARATOR, },
	{ { DEFACCEL,								"SWS: Renumber marker IDs" },				"SWSMARKERLIST7",  RenumberIds,		  "Reorder marker IDs", },
	{ { DEFACCEL,								"SWS: Renumber region IDs" },				"SWSMARKERLIST8",  RenumberRegions,   "Reorder region IDs", },
	{ { DEFACCEL, NULL }, NULL, NULL, SWS_SEPARATOR, },
	{ { DEFACCEL,								"SWS: Select next region" },				"SWS_SELNEXTREG",  SelNextRegion,     "Select next region", },
	{ { DEFACCEL,								"SWS: Select previous region" },			"SWS_SELPREVREG",  SelPrevRegion,     "Select prev region", },
	{ { DEFACCEL,								"SWS: Delete all markers" },				"SWSMARKERLIST9",  DeleteAllMarkers,  "Delete all markers", },
	{ { DEFACCEL,								"SWS: Delete all regions" },				"SWSMARKERLIST10", DeleteAllRegions,  "Delete all regions", },
	{ { DEFACCEL, NULL }, NULL, NULL, SWS_SEPARATOR, },
	{ { DEFACCEL,								"SWS: Export tracklist to clipboard" },		"SWSMARKERLIST11", ExportToClipboard, "Export tracklist to clipboard", },
	{ { DEFACCEL,								"SWS: Tracklist format" },					"SWSMARKERLIST12", ExportFormat,      "Tracklist format...", },
	{ {}, LAST_COMMAND, }, // Denote end of table
};

static bool ProcessExtensionLine(const char *line, ProjectStateContext *ctx, bool isUndo, struct project_config_extension_t *reg)
{
	LineParser lp(false);
	if (lp.parse(line) || lp.getnumtokens() < 1)
		return false;
	if (strcmp(lp.gettoken_str(0), "<MARKERLIST") != 0)
		return false; // only look for <MARKERLIST lines

	MarkerList* ml = g_savedLists.Get()->Add(new MarkerList(lp.gettoken_str(1), false));
  
	char linebuf[4096];
	while(true)
	{
		if (!ctx->GetLine(linebuf,sizeof(linebuf)) && !lp.parse(linebuf))
		{
			if (lp.getnumtokens() > 0 && lp.gettoken_str(0)[0] == '>')
				break;
			else if (lp.getnumtokens() == 5)
				ml->m_items.Add(new MarkerItem(&lp));
		}
	}
	return true;
}

static void SaveExtensionConfig(ProjectStateContext *ctx, bool isUndo, struct project_config_extension_t *reg)
{
	char str[512];
	for (int i = 0; i < g_savedLists.Get()->GetSize(); i++)
	{
		ctx->AddLine("<MARKERLIST \"%s\"", g_savedLists.Get()->Get(i)->m_name);
		for (int j = 0; j < g_savedLists.Get()->Get(i)->m_items.GetSize(); j++)
			ctx->AddLine(g_savedLists.Get()->Get(i)->m_items.Get(j)->ItemString(str, 512));
		ctx->AddLine(">");
	}
}

static void BeginLoadProjectState(bool isUndo, struct project_config_extension_t *reg)
{
	g_savedLists.Get()->Empty(true);
	g_savedLists.Cleanup();
	pMarkerList->Update();
}

static project_config_extension_t g_projectconfig = { ProcessExtensionLine, SaveExtensionConfig, BeginLoadProjectState, NULL };

// Seems damned "hacky" to me, but it works, so be it.
static int translateAccel(MSG *msg, accelerator_register_t *ctx)
{
	if (pMarkerList->IsActive())
	{
		if (msg->message == WM_KEYDOWN)
		{
			bool bCtrl  = GetAsyncKeyState(VK_CONTROL) & 0x8000 ? true : false;
			bool bAlt   = GetAsyncKeyState(VK_MENU)    & 0x8000 ? true : false;
			bool bShift = GetAsyncKeyState(VK_SHIFT)   & 0x8000 ? true : false;

			if (msg->wParam == VK_DELETE && !bCtrl && !bAlt && !bShift)
			{
				SendMessage(pMarkerList->GetHWND(), WM_COMMAND, DELETE_MSG, 0);
				return 1;
			}
			else if (msg->wParam == VK_UP && !bCtrl && !bAlt && !bShift)
			{
				SendMessage(pMarkerList->GetHWND(), WM_COMMAND, SELPREV_MSG, 0);
				return 1;
			}
			else if (msg->wParam == VK_DOWN && !bCtrl && !bAlt && !bShift)
			{
				SendMessage(pMarkerList->GetHWND(), WM_COMMAND, SELNEXT_MSG, 0);
				return 1;
			}
		}
		return -666;
	}
	return 0;
} 

static accelerator_register_t g_ar = { translateAccel, TRUE, NULL };

static void menuhook(int menuid, HMENU hMenu, int flag)
{
	if (menuid == MAINMENU_VIEW && flag == 0)
		AddToMenu(hMenu, g_commandTable[0].menuText, g_commandTable[0].accel.accel.cmd, 40075);
	else if (menuid == MAINMENU_EDIT && flag == 0)
		AddSubMenu(hMenu, SWSCreateMenu(g_commandTable), "SWS Marker utilites");
	else if (flag == 1)
		SWSCheckMenuItem(hMenu, g_commandTable[0].accel.accel.cmd, pMarkerList->IsValidWindow());
}

int MarkerListInit()
{
	if (!plugin_register("projectconfig",&g_projectconfig))
		return 0;
	if (!plugin_register("accelerator",&g_ar))
		return 0;

	SWSRegisterCommands(g_commandTable);

	if (!plugin_register("hookmenu", menuhook))
		return 0;

	pMarkerList = new SWS_MarkerListWnd();

	return 1;
}