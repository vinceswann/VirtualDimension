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

#include "StdAfx.h"
#include "window.h"
#include "VirtualDimension.h"
#include "movewindow.h"
#include <Shellapi.h>
#include "PlatformHelper.h"
#include "window.h"

#ifdef __GNUC__
#define MIM_STYLE 0x10
#define MNS_CHECKORBMP 0x04000000
#endif

HINSTANCE HookWindow(HWND hWnd, DWORD dwProcessId, int data, HANDLE minToTrayEvent);
bool UnHookWindow(HINSTANCE hInstance, DWORD dwProcessId, HWND hWnd);

ITaskbarList* Window::m_tasklist = NULL;

Window::Window(HWND hWnd): m_hOwnedWnd(GetOwnedWindow(hWnd)), AlwaysOnTop(m_hOwnedWnd),
                           m_hWnd(hWnd), m_hidden(false), m_MinToTray(false), 
                           m_transp(m_hOwnedWnd), m_transpLevel(128),
                           m_autoSaveSettings(false), m_autosize(false), m_autopos(false),
                           m_hIcon(NULL), m_hDefaulIcon(NULL), m_style(0), m_HookDllHandle(NULL)
{
   Settings s;
   Settings::Window settings(&s);

   if (m_tasklist == NULL)
   {
      CoCreateInstance(CLSID_TaskbarList, NULL, CLSCTX_ALL, IID_ITaskbarList, (LPVOID*)&m_tasklist);
      if (m_tasklist != NULL)
         m_tasklist->HrInit();
   }
   else
      m_tasklist->AddRef();

   //Try to see if there are some special settings for this window
   GetClassName(m_hWnd, m_className, sizeof(m_className)/sizeof(TCHAR));
   OpenSettings(settings, false);

   if (settings.IsValid())
   {
      RECT rect;

      //Load settings for this window
      m_desk = settings.LoadOnAllDesktops() ? NULL : deskMan->GetCurrentDesktop();

      SetMinimizeToTray(settings.LoadMinimizeToTray());
      SetAlwaysOnTop(settings.LoadAlwaysOnTop());

      SetTransparent(settings.LoadEnableTransparency());
      SetTransparencyLevel(settings.LoadTransparencyLevel());

      m_autoSaveSettings = settings.LoadAutoSaveSettings();
      m_autosize = settings.LoadAutoSetSize();
      m_autopos = settings.LoadAutoSetPos();

      if ( (m_autosize || m_autopos) && settings.LoadPosition(&rect) )
         SetWindowPos(m_hOwnedWnd, 0, 
                      rect.left, rect.top, rect.right-rect.left, rect.bottom-rect.top,
                      SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS |
                      (m_autopos?0:SWP_NOMOVE) | (m_autosize?0:SWP_NOSIZE));
   }
   else
      //Find out on which desktop the window is
      m_desk = deskMan->GetCurrentDesktop();

   m_hMinToTrayEvent = CreateEvent(NULL, TRUE, m_MinToTray, NULL);
   if (winMan->IsIntegrateWithShell())
      Hook();
}

Window::~Window(void)
{
   ULONG count;

   UnHook();

   if (m_hMinToTrayEvent)
      CloseHandle(m_hMinToTrayEvent);

   if (m_hDefaulIcon)
      DestroyIcon(m_hDefaulIcon);

   if (m_autoSaveSettings)
      SaveSettings();

   count = m_tasklist->Release();
   if (count == 0)
      m_tasklist = NULL;
}

void Window::MoveToDesktop(Desktop * desk)
{
   Desktop * oldDesk;

   if (desk == m_desk)
      return;

   oldDesk = m_desk;
   m_desk = desk;

   if (IsOnDesk(deskMan->GetCurrentDesktop()))
   {
      if (IsInTray())
         trayManager->AddIcon(this);
      else
         ShowWindow();
   }
   else
   {
      if (IsInTray())
         trayManager->DelIcon(this);
      else
         HideWindow();
   }

   if (IsOnDesk(NULL))  //on all desktops
      deskMan->UpdateLayout();
   else
   {
      if (oldDesk != NULL)
      {
         oldDesk->UpdateLayout();
         m_desk->UpdateLayout();
      }
      else
         deskMan->UpdateLayout();
   }
}

void Window::ShowWindow()
{
   if (!m_hidden)
      return;

   //Check if hung before doing anything
   if (!SendMessageTimeout(m_hWnd, (int)NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 500, NULL))
      return;

   //Restore the window's style
   if (m_style)
      SetWindowLongPtr(m_hWnd, GWL_EXSTYLE, m_style);

   //Show the icon
   m_tasklist->AddTab(m_hWnd);

   //Restore the application if needed
   if (!m_iconic)
      ::ShowWindowAsync(m_hWnd, SW_SHOWNOACTIVATE);

   m_hidden = false;
}

void Window::HideWindow()
{
   if (m_hidden)
      return;

   //Check if hung before doing anything
   if (!SendMessageTimeout(m_hWnd, (int)NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 500, NULL))
      return;

   if (!winMan->IsShowAllWindowsInTaskList())
      m_style = GetWindowLongPtr(m_hWnd, GWL_EXSTYLE);
   else
      m_style = 0;

   //Minimize the application
   m_iconic = IsIconic();
   if (!m_iconic)
      ::ShowWindow(m_hWnd, SW_SHOWMINNOACTIVE);

   //Hide the icon
   m_tasklist->DeleteTab(m_hWnd);

   //disable the window so that it does not appear in task list
   if (!winMan->IsShowAllWindowsInTaskList())
   {
      LONG_PTR style = (m_style & ~WS_EX_APPWINDOW) | WS_EX_TOOLWINDOW;
      SetWindowLongPtr(m_hWnd, GWL_EXSTYLE, style);
   }
   
   m_hidden = true;
}

bool Window::IsOnCurrentDesk() const
{ 
   return IsOnDesk(deskMan->GetCurrentDesktop());
}

HICON Window::GetIcon(void)
{
   if (GetObjectType(m_hIcon))
      return m_hIcon;

   m_hIcon = NULL;
     	
   //Get normal icon
	if (SendMessageTimeout(m_hWnd, WM_GETICON, ICON_SMALL, 0, SMTO_ABORTIFHUNG, 100, (LPDWORD)&m_hIcon) &&
       m_hIcon)
      return m_hIcon;

   //Get drag icon
   if (SendMessageTimeout(m_hWnd, WM_QUERYDRAGICON, 0, 0, SMTO_ABORTIFHUNG, 100, (LPDWORD)&m_hIcon) &&
       m_hIcon)
      return m_hIcon;

   //Get class icon
   m_hIcon = (HICON) GetClassLong( m_hWnd, GCL_HICONSM );
   if (m_hIcon)
      return m_hIcon;

   //Return default icon
   if (m_hDefaulIcon)
      return m_hDefaulIcon;

   //No default icon yet: get it from application file, or use generic default icon
   TCHAR lpFileName[256];
   PlatformHelper::GetWindowFileName(m_hWnd, lpFileName, 256);
   if (!ExtractIconEx(lpFileName, 0, NULL, &m_hDefaulIcon, 1))
      m_hDefaulIcon = (HICON) LoadImage(vdWindow, MAKEINTRESOURCE(IDI_DEFAPP_SMALL), IMAGE_ICON, 16, 16, LR_SHARED);

   return m_hDefaulIcon;
}

void Window::InsertMenuItem(HMENU menu, MENUITEMINFO& mii, HBITMAP bmp, UINT id, LPSTR str)
{
   mii.hbmpItem = bmp;
   mii.wID = id;
   mii.dwTypeData = str;
   ::InsertMenuItem(menu, (UINT)-1, TRUE, &mii);
}

HBITMAP Window::LoadBmpRes(int id)
{
   return (HBITMAP)LoadImage(vdWindow, MAKEINTRESOURCE(id), IMAGE_BITMAP, 
                             16, 16, LR_SHARED|LR_LOADTRANSPARENT|LR_LOADMAP3DCOLORS);
}

HMENU Window::BuildMenu()
{
   HMENU hMenu;
   MENUITEMINFO mii;
   MENUINFO mi;

   //Create the menu
   hMenu = CreatePopupMenu();

   //Set its style
   mi.cbSize = sizeof(MENUINFO);
   mi.fMask = MIM_STYLE;
   mi.dwStyle = MNS_CHECKORBMP;
   SetMenuInfo(hMenu, &mi);

   //Now add the items
   mii.cbSize = sizeof(MENUITEMINFO);
   mii.fMask = MIIM_BITMAP | MIIM_ID | MIIM_STRING | MIIM_STATE;

   mii.fState = IsAlwaysOnTop() ? MFS_CHECKED : MFS_UNCHECKED;
   InsertMenuItem(hMenu, mii, NULL, VDM_TOGGLEONTOP, "Always on top");
   mii.fState = IsMinimizeToTray() ? MFS_CHECKED : MFS_UNCHECKED;
   InsertMenuItem(hMenu, mii, NULL, VDM_TOGGLEMINIMIZETOTRAY, "Minimize to tray");
   if (m_transp.IsTransparencySupported())
   {
      mii.fState = m_transp.GetTransparencyLevel() == 255 ? MFS_UNCHECKED : MFS_CHECKED;
      InsertMenuItem(hMenu, mii, NULL, VDM_TOGGLETRANSPARENCY, "Transparent");
   }

   AppendMenu(hMenu, MF_SEPARATOR, 0, 0);
   mii.fState = (m_desk==NULL) ? MFS_CHECKED : MFS_UNCHECKED;
   InsertMenuItem(hMenu, mii, NULL, VDM_TOGGLEALLDESKTOPS, "All desktops");
   mii.fState = MFS_UNCHECKED;
   AppendMenu(hMenu, MF_STRING, VDM_MOVEWINDOW, "Change desktop...");

   AppendMenu(hMenu, MF_SEPARATOR, 0, 0);
   InsertMenuItem(hMenu, mii, HBMMENU_POPUP_RESTORE, VDM_ACTIVATEWINDOW, "Activate");
   if (IsIconic() || IsZoomed(m_hWnd) )
      InsertMenuItem(hMenu, mii, HBMMENU_POPUP_RESTORE, VDM_RESTORE, "Restore");
   if (!IsIconic())
      InsertMenuItem(hMenu, mii, HBMMENU_POPUP_MINIMIZE, VDM_MINIMIZE, "Minimize");
   if (!IsZoomed(m_hWnd))
   {
      InsertMenuItem(hMenu, mii, HBMMENU_POPUP_MAXIMIZE, VDM_MAXIMIZE, "Maximize");
      InsertMenuItem(hMenu, mii, LoadBmpRes(IDB_MAXIMIZE_VERT), VDM_MAXIMIZEHEIGHT, "Maximize Height");
      InsertMenuItem(hMenu, mii, LoadBmpRes(IDB_MAXIMIZE_HORIZ), VDM_MAXIMIZEWIDTH, "Maximize Width");
   }
   InsertMenuItem(hMenu, mii, HBMMENU_POPUP_CLOSE, VDM_CLOSE, "Close");
   InsertMenuItem(hMenu, mii, LoadBmpRes(IDB_KILL), VDM_KILL, "Kill");

   AppendMenu(hMenu, MF_SEPARATOR, 0, 0);
   InsertMenuItem(hMenu, mii, NULL, VDM_PROPERTIES, "Properties");

   return hMenu;
}

void Window::OnMenuItemSelected(HMENU /*menu*/, int cmdId)
{
   switch(cmdId)
   {
   case VDM_ACTIVATEWINDOW:
      Activate();
      break;

   case VDM_TOGGLEONTOP:
      ToggleOnTop();
      break;

   case VDM_TOGGLEMINIMIZETOTRAY:
      ToggleMinimizeToTray();
      break;

   case VDM_TOGGLETRANSPARENCY:
      ToggleTransparent();
      break;

   case VDM_TOGGLEALLDESKTOPS:
      ToggleAllDesktops();
      break;

   case VDM_MOVEWINDOW:
      SelectDesktopForWindow(this);
      break;

   case VDM_RESTORE:
      Restore();
      break;

   case VDM_MINIMIZE:
      Minimize();
      break;

   case VDM_MAXIMIZE:
      Maximize();
      break;

   case VDM_MAXIMIZEHEIGHT:
      MaximizeHeight();
      break;

   case VDM_MAXIMIZEWIDTH:
      MaximizeWidth();
      break;
      
   case VDM_CLOSE:
      PostMessage(m_hWnd, WM_SYSCOMMAND, SC_CLOSE, 0);
      break;

   case VDM_KILL:
      Kill();
      break;

   case VDM_PROPERTIES:
      DisplayWindowProperties();
      break;

   default:
      break;
   }
}

void Window::SetMinimizeToTray(bool totray)
{
   m_MinToTray = totray;

   if (m_MinToTray)
      SetEvent(m_hMinToTrayEvent);
   else
      ResetEvent(m_hMinToTrayEvent);

   if (IsIconic() && IsOnCurrentDesk())
   {
      if (m_MinToTray)
      {  
         // Move minimized icon from taskbar to tray
         trayManager->AddIcon(this);
         HideWindow();
      }
      else
      {
         // Move minimized icon from tray to taskbar
         trayManager->DelIcon(this);
         ShowWindow();
      }
   }
}

void Window::ToggleMinimizeToTray()
{
   SetMinimizeToTray(!IsMinimizeToTray());
}

void Window::ToggleOnTop()
{
   SetAlwaysOnTop(!IsAlwaysOnTop());
}

void Window::SetOnAllDesktops(bool onall)
{
   if (onall)
      MoveToDesktop(NULL);
   else if (IsOnAllDesktops())
      MoveToDesktop(deskMan->GetCurrentDesktop());
}

void Window::ToggleAllDesktops()
{
   SetOnAllDesktops(!IsOnAllDesktops());
}

void Window::SetTransparent(bool transp)
{
   m_transp.SetTransparencyLevel(transp ? m_transpLevel : (unsigned char)255);
}

void Window::ToggleTransparent()
{
   SetTransparent(!IsTransparent());
}

void Window::SetTransparencyLevel(unsigned char level)
{
   //Update the variable
   m_transpLevel = level;
   
   //Refresh the display
   SetTransparent(IsTransparent());
}

void Window::Activate()
{
   if (IsIconic())
      Restore();
   if (!IsOnCurrentDesk())
      deskMan->SwitchToDesktop(m_desk);
   SetForegroundWindow(m_hOwnedWnd);
}

void Window::Restore()
{
   if (IsIconic())
   {
      m_iconic = false;

      if (IsOnCurrentDesk())
      {
         if (IsMinimizeToTray())
         {
            trayManager->DelIcon(this);
            ShowWindow();
         }
         else
            OpenIcon(m_hWnd);
      }
   }
   else if (IsZoomed(m_hWnd))
      ::ShowWindow(m_hWnd, SW_RESTORE);
}

void Window::Minimize()
{
   if (IsOnCurrentDesk())
   {
      if (IsMinimizeToTray())
      {
         trayManager->AddIcon(this);
         HideWindow();
      }
      else
         ::ShowWindow(m_hWnd, SW_MINIMIZE);
   }
   m_iconic = true;
}

void Window::Maximize()
{
   ::ShowWindow(m_hWnd, SW_MAXIMIZE);
}

void Window::MaximizeHeight()
{
   RECT rect;
   RECT screen;
   HWND hWnd = GetOwnedWindow();

   GetWindowRect(hWnd, &rect);
   SystemParametersInfo(SPI_GETWORKAREA, 0, &screen, 0);
   MoveWindow( hWnd, rect.left, screen.top, 
               rect.right-rect.left, screen.bottom-screen.top,
               TRUE);
}

void Window::MaximizeWidth()
{
   RECT rect;
   RECT screen;
   HWND hWnd = GetOwnedWindow();

   GetWindowRect(hWnd, &rect);
   SystemParametersInfo(SPI_GETWORKAREA, 0, &screen, 0);
   MoveWindow( hWnd, screen.left, rect.top, 
               screen.right-screen.left, rect.bottom - rect.top, 
               TRUE);
}

void Window::Kill()
{
   HANDLE hProcess;
   DWORD pId;
   
   GetWindowThreadProcessId(m_hWnd, &pId);
   hProcess = OpenProcess( PROCESS_TERMINATE, 0, pId );
   if (hProcess == NULL)
      return;

   if (winMan->ConfirmKillWindow())
      TerminateProcess( hProcess, 9);
   CloseHandle (hProcess);
}

LRESULT Window::OnTrayIconMessage(HWND /*hWnd*/, UINT /*message*/, WPARAM /*wParam*/, LPARAM lParam)
{
   switch(lParam)
   {
   case WM_RBUTTONDOWN:
   case WM_CONTEXTMENU:
      OnContextMenu();
      break;

   case WM_LBUTTONDOWN:
      OnMenuItemSelected(NULL, VDM_ACTIVATEWINDOW);
      break;
   }

   return 0;
}

void Window::OnContextMenu()
{
   HMENU hMenu;
   HRESULT res;
   POINT pt;
   
   hMenu = BuildMenu();

   GetCursorPos(&pt);
   SetForegroundWindow(vdWindow);
   res = TrackPopupMenu(hMenu, TPM_RETURNCMD|TPM_RIGHTBUTTON, pt.x, pt.y, 0, vdWindow, NULL);

   if (res >= WM_USER)
      OnMenuItemSelected(hMenu, res);
   else
      PostMessage(vdWindow, WM_COMMAND, res, 0);

   DestroyMenu(hMenu);
}

void Window::Hook()
{
   if (m_HookDllHandle)
      return;

   HWND hWnd = GetOwnedWindow();
   GetWindowThreadProcessId(hWnd, &m_dwProcessId);
   m_HookDllHandle = HookWindow(hWnd, m_dwProcessId, (int)this, m_hMinToTrayEvent);
}

void Window::UnHook()
{
   if (m_HookDllHandle)
      UnHookWindow(m_HookDllHandle, m_dwProcessId, m_hOwnedWnd);
   m_HookDllHandle = NULL;
}
