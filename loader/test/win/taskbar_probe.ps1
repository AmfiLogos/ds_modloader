# wintaskbar probe: the game's top-level windows (styles, owner) + its taskbar button (UI Automation on the taskbar)
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices; using System.Collections.Generic;
public static class W {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern IntPtr GetWindowLongPtrW(IntPtr h, int i);
  [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint c);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  public static List<string> Dump(uint want) {
    var r = new List<string>();
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p != want) return true;
      var c = new StringBuilder(64); GetClassNameW(h, c, 64); var t = new StringBuilder(128); GetWindowTextW(h, t, 128);
      long st = GetWindowLongPtrW(h, -16).ToInt64(), ex = GetWindowLongPtrW(h, -20).ToInt64();
      r.Add(String.Format("hwnd=0x{0:x} class={1} title='{2}' visible={3} style=0x{4:x8} exstyle=0x{5:x8} NOACTIVATE={6} TOOLWINDOW={7} APPWINDOW={8} TOPMOST={9} owner=0x{10:x}",
        h.ToInt64(), c, t, IsWindowVisible(h) ? 1 : 0, st, ex, (ex & 0x08000000) != 0 ? 1 : 0, (ex & 0x80) != 0 ? 1 : 0, (ex & 0x40000) != 0 ? 1 : 0, (ex & 0x8) != 0 ? 1 : 0, GetWindow(h, 4).ToInt64()));
      return true; }, IntPtr.Zero);
    return r;
  }
}
"@
foreach ($p in Get-Process DungeonSettlers -ErrorAction SilentlyContinue) {
  "pid=$($p.Id) MainWindowHandle=0x{0:x} MainWindowTitle='$($p.MainWindowTitle)'" -f $p.MainWindowHandle.ToInt64()
  foreach ($l in [W]::Dump([uint32]$p.Id)) { if ($l -match "visible=1|UnityWndClass") { "  $l" } }
}
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$root = [System.Windows.Automation.AutomationElement]::RootElement
$tray = $root.FindFirst([System.Windows.Automation.TreeScope]::Children, (New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ClassNameProperty, "Shell_TrayWnd")))
if ($tray) {
  $all = $tray.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
  $names = @(); foreach ($e in $all) { $n = $e.Current.Name; if ($n -match "Dungeon") { $names += "$($e.Current.ControlType.ProgrammaticName):'$n'" } }
  "taskbar buttons matching Dungeon: " + ($(if ($names.Count) { $names -join ' | ' } else { 'NONE' }))
} else { "taskbar: Shell_TrayWnd not found" }
