/* 
 * Virtual Dimension -  a free, fast, and feature-full virtual desktop manager 
 * for the Microsoft Windows platform.
 * Copyright (C) 2003 Francois Ferrand
 *
 * This program is free software; you can redistribute it and/or modify it under 
 * the terms of the GNU General Public License as published by the Free Software 
 * Foundation; either version 2 of the License, or (at your option) any later 
 * version.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT 
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS 
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with 
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple 
 * Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#ifndef __HOTKEYCONFIG_H__
#define __HOTKEYCONFIG_H__

#ifdef __GNUC__

typedef struct tagNMITEMACTIVATE {
    NMHDR hdr;
    int iItem;
    int iSubItem;
    UINT uNewState;
    UINT uOldState;
    UINT uChanged;
    POINT ptAction;
    LPARAM lParam;
    UINT uKeyFlags;
} NMITEMACTIVATE,*LPNMITEMACTIVATE;

#endif

#include <list>
#include "HotkeyManager.h"

using namespace std;

class ConfigurableHotkey: public HotKeyManager::EventHandler
{
   friend class ShortcutsConfigurationDlg;

public:
   ConfigurableHotkey();
   virtual ~ConfigurableHotkey();
   virtual int GetHotkey() const;
   virtual void SetHotkey(int hotkey);

   virtual LPCSTR GetName() const = 0;
   
protected:
   int m_hotkey;

private:
   int m_tempHotkey;
   void Commit()  { SetHotkey(m_tempHotkey); }
};

class ShortcutsConfigurationDlg
{
public:
   static DLGPROC GetWindowProc()                              { return (DLGPROC)DlgProc; }
   
   static void RegisterHotkey(ConfigurableHotkey * hotkey)     { m_hotkeys.push_back(hotkey); }
   static void UnRegisterHotkey(ConfigurableHotkey * hotkey)   { m_hotkeys.remove(hotkey); }

   ShortcutsConfigurationDlg(HWND hwnd);
   ~ShortcutsConfigurationDlg();

protected:
   void InsertItem(ConfigurableHotkey* hotkey);
   ConfigurableHotkey* GetItemHotkey(int index);
   void SetItemShortcut(int index, int shortcut);
   int GetItemShortcut(int index);
   void BeginEdit(int item);
   void EndEdit();

   LRESULT OnApply();
   void OnClick(LPNMITEMACTIVATE lpnmitem);
   void OnRightClick(LPNMITEMACTIVATE lpnmitem);
   void OnSetFocus();

   bool IsEditing() const                                      { return IsWindowVisible(m_editCtrl) ? true : false; }

   HWND m_hDlg;
   HWND m_editCtrl;
   HWND m_listViewWnd;
   int m_editedItemIndex;

   static list<ConfigurableHotkey*> m_hotkeys;
   static LRESULT CALLBACK DlgProc(HWND hDlg, UINT message, WPARAM /*wParam*/, LPARAM lParam);
};

#endif /*__HOTKEYCONFIG_H__*/