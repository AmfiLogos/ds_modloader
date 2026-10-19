# TEST ONLY: guest side of winharmony_test.py (Windows VM, Windows PowerShell 5.1, no admin, no Python).
# Every step writes C:\dsm\winharmony\out\<step>-<tag>.json last (the Mac driver polls for it with copyFileFromGuestToHost).
# Nothing is deleted: what is replaced moves to C:\dsm\winharmony\old\<stamp>\.
#   -Step setup    checks (no game running, Unity prefs windowed = mode 3), unpacks stage.zip, installs the kit into the
#                  test folder C:\dsm\winharmony\root with its install.bat (the VM's own install and shortcuts are left as
#                  they were), adds the BepInEx test mod samefn-bie, seeds the pinned BepInEx zip into root\cache and the
#                  game settings (windowed) into the profile's data folder. A kept game copy gets a fresh BepInEx.
#   -Step start    -Ids a,b: writes the mod profile (data\Mods\active.json) and starts the starter (the real start path:
#                  bie_prepare, Doorstop on or off, environment). Run it through runProgramInGuest -interactive.
#   -Step procs    pids of the processes running from the test folder (game, crash handler, starter).
#   -Step kill     stops them and waits until they are gone.
#   -Step chain    runs stage\chain\chaintest.exe (no game; after setup) and returns its exit code and log lines.
#   -Step collect  pings the multiplayer control port twice (frame counter, 3 s apart), copies the logs into one zip.
param([string]$Step = '', [string]$Tag = '0', [string]$Ids = '', [switch]$Download)
$ErrorActionPreference = 'Stop'
$H = 'C:\dsm\winharmony'; $ROOT = "$H\root"; $GAME = "$ROOT\game"; $PROF = "$ROOT\profiles\modded"; $DATA = "$PROF\data"
$OUT = "$H\out"; $STAGE = "$H\stage"
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'; $OLD = "$H\old\$stamp"
$REG = 'HKCU:\Software\CanOpener\Dungeon Settlers'; $MODE = 'Screenmanager Fullscreen mode_h3630240806'
New-Item -ItemType Directory -Force -Path $OUT | Out-Null
$r = [ordered]@{ step = $Step; tag = $Tag; ok = $false; why = '' }

function Put-Out { $tmp = "$OUT\$Step-$Tag.json.tmp"; $r | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 $tmp; Move-Item -Force $tmp "$OUT\$Step-$Tag.json" }
function Move-Old($p) { if (Test-Path -LiteralPath $p) { New-Item -ItemType Directory -Force -Path $OLD | Out-Null; Move-Item -LiteralPath $p -Destination "$OLD\$((Split-Path $p -Leaf))" -Force } }
function Utc($p) { if (Test-Path -LiteralPath $p) { (Get-Item -LiteralPath $p).LastWriteTimeUtc.ToString('o') } else { $null } }
function Games { @(Get-Process -Name DungeonSettlers -ErrorAction SilentlyContinue) }
# the game, Unity's crash handler and the starter running from the test folder
function Ours { @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path -like "$ROOT\*" }) }
function Reg-Mode { try { (Get-ItemProperty -Path $REG -Name $MODE).$MODE } catch { $null } }
function Rig-Frames($port) {
    try {
        $c = New-Object Net.Sockets.TcpClient; $c.ReceiveTimeout = 4000; $c.SendTimeout = 4000
        $c.Connect('127.0.0.1', [int]$port); $s = $c.GetStream()
        $w = New-Object IO.StreamWriter($s); $w.NewLine = "`n"
        # test control channel guard (ds_multiplayer src/rig_sock.c): the token in <profile>\ctl.token first, bare
        $tok = (Get-Content "$PROF\ctl.token" -ErrorAction SilentlyContinue | Select-Object -First 1)
        if ($tok) { $w.WriteLine($tok.Trim()) }
        $w.WriteLine('{"cmd":"ping"}'); $w.Flush()
        $l = (New-Object IO.StreamReader($s)).ReadLine(); $c.Close()
        if ($l -match '"frames":(\d+)') { return [int64]$Matches[1] }
    } catch { }
    return -1
}

try {
    switch ($Step) {
    'setup' {
        $g = Games
        if ($g.Count) { throw "a Dungeon Settlers game already runs in the VM (pid $($g[0].Id)): not touched" }
        $r.reg_mode = Reg-Mode
        if ($r.reg_mode -ne 3) { throw "Unity prefs: '$MODE' is '$($r.reg_mode)', not 3 (windowed): set it windowed first (VMWARE.md rule 3)" }
        Move-Old $STAGE
        Expand-Archive -Path "$H\stage.zip" -DestinationPath $STAGE -Force
        # a kept game copy (install.bat keeps it when its build is the kit's): BepInEx, profile, mods and cache start fresh
        if (Test-Path "$GAME\dsmodded_build.txt") {
            foreach ($n in 'BepInEx', 'dotnet', 'winhttp.dll', 'doorstop_config.ini', '.doorstop_version', 'changelog.txt', '.dsm_ok_bepinex') { Move-Old "$GAME\$n" }
            foreach ($n in 'profiles', 'mods', 'cache') { Move-Old "$ROOT\$n" }
            $r.kept_game_copy = $true
        } elseif (Test-Path $ROOT) { Move-Old $ROOT }
        # install.bat makes desktop + Start menu shortcuts: the VM's own ones come back afterwards
        $links = @(); $bak = "$H\lnkbak-$stamp"
        foreach ($d in [Environment]::GetFolderPath('Desktop'), "$env:APPDATA\Microsoft\Windows\Start Menu\Programs") {
            foreach ($f in 'Dungeon Settlers Modded.lnk', 'Dungeon Settlers Modded.bat') {
                $p = Join-Path $d $f; $b = $null
                if (Test-Path -LiteralPath $p) { New-Item -ItemType Directory -Force -Path $bak | Out-Null; $b = Join-Path $bak ("{0}-{1}" -f $links.Count, $f); Copy-Item -LiteralPath $p $b }
                $links += ,@($p, $b)
            }
        }
        $env:DSMODDED_ROOT = $ROOT; $env:DSMODDED_NOPAUSE = '1'
        Push-Location "$STAGE\kit"
        $log = "$OUT\install-$Tag.log"
        cmd.exe /d /c "install.bat < NUL > $log 2>&1"           # stdin NUL: a question of install.bat ends it (no hang)
        $r.install_exit = $LASTEXITCODE
        Pop-Location
        foreach ($l in $links) {
            if ($l[1]) { Copy-Item -LiteralPath $l[1] $l[0] -Force }
            elseif (Test-Path -LiteralPath $l[0]) { Move-Old $l[0] }
        }
        $r.install_tail = @(Get-Content $log -ErrorAction SilentlyContinue | Where-Object { $_.Trim() } | Select-Object -Last 8)
        if ($r.install_exit -ne 0) { throw "install.bat exit $($r.install_exit)" }
        Copy-Item -Recurse -Force "$STAGE\mods\samefn-bie" "$ROOT\mods\samefn-bie"
        if (-not $Download) { New-Item -ItemType Directory -Force -Path "$ROOT\cache" | Out-Null; Copy-Item -Force "$STAGE\cache\*" "$ROOT\cache\" }
        # the starter copies these from the normal game at its first start; done here so the copy is windowed for sure
        New-Item -ItemType Directory -Force -Path $DATA | Out-Null
        $low = Join-Path ([Environment]::GetFolderPath('UserProfile')) 'AppData\LocalLow\CanOpener\Dungeon Settlers'
        foreach ($f in 'OutGame.json', 'UserSetting.json', 'UserCampaignLog.json') { if (Test-Path "$low\$f") { Copy-Item -Force "$low\$f" "$DATA\$f" } }
        if (Test-Path "$DATA\UserSetting.json") {
            $t = [IO.File]::ReadAllText("$DATA\UserSetting.json")
            $t = $t -replace '"SaveCurrentScreenType"\s*:\s*\d+', '"SaveCurrentScreenType":1' -replace '"IsLanguageInitialized"\s*:\s*false', '"IsLanguageInitialized":true'
            [IO.File]::WriteAllText("$DATA\UserSetting.json", $t)
            if ($t -match '"SaveCurrentScreenType"\s*:\s*(\d+)') { $r.usersetting_screen_type = [int]$Matches[1] }
        }
        Set-Content -Path "$DATA\.copied_from_normal_game" -Value "winharmony test $stamp"
        $r.game_build = (Get-Content "$GAME\dsmodded_build.txt" -ErrorAction SilentlyContinue | Select-Object -First 1)
        $r.mods = @(Get-ChildItem "$ROOT\mods" -Directory | ForEach-Object Name)
        $r.ok = $true
    }
    'start' {
        $g = Games
        if ($g.Count) { throw "a Dungeon Settlers game already runs (pid $($g[0].Id))" }
        $r.reg_mode = Reg-Mode
        if ($r.reg_mode -ne 3) { throw "Unity prefs: '$MODE' is '$($r.reg_mode)', not 3 (windowed): not started" }
        $list = @($Ids -split ',' | Where-Object { $_ })
        New-Item -ItemType Directory -Force -Path "$DATA\Mods" | Out-Null
        $q = ($list | ForEach-Object { '"' + $_ + '"' }) -join ','
        [IO.File]::WriteAllText("$DATA\Mods\active.json", "{`"enabled`":[$q],`"order`":[$q]}`n")
        $r.ids = $list
        $r.start_utc = (Get-Date).ToUniversalTime().ToString('o')
        $p = Start-Process -FilePath "$ROOT\Dungeon Settlers Modded.exe" -WorkingDirectory $ROOT -PassThru
        $r.starter_pid = $p.Id
        $r.ok = $true
    }
    'collect' {
        $g = @(Games | Where-Object { $_.Path -like "$GAME\*" })
        $r.game_pids = @($g | ForEach-Object Id)
        $port = (Get-Content "$PROF\ctl.port" -ErrorAction SilentlyContinue | Select-Object -First 1)
        if (-not $port) { $port = '47811' }
        $r.port = $port
        $r.frames1 = Rig-Frames $port; Start-Sleep -Seconds 3; $r.frames2 = Rig-Frames $port
        $r.game_alive = @(Games | Where-Object { $_.Path -like "$GAME\*" }).Count -gt 0
        $r.now_utc = (Get-Date).ToUniversalTime().ToString('o')
        $r.logoutput_utc = Utc "$GAME\BepInEx\LogOutput.log"
        $r.samefn_log_utc = Utc "$GAME\BepInEx\samefn_win.log"
        $r.plugins = @(Get-ChildItem "$GAME\BepInEx\plugins" -Directory -ErrorAction SilentlyContinue | ForEach-Object Name)
        $r.plugins_off = @(Get-ChildItem "$GAME\BepInEx\plugins_off" -Directory -ErrorAction SilentlyContinue | ForEach-Object Name)
        $r.interop = Test-Path "$GAME\BepInEx\interop\Assembly-CSharp.dll"
        $cr = Join-Path $env:LOCALAPPDATA 'Temp\CanOpener\Dungeon Settlers\Crashes'
        $r.crash_dirs = @(Get-ChildItem $cr -Directory -ErrorAction SilentlyContinue | ForEach-Object { @{ name = $_.Name; utc = $_.LastWriteTimeUtc.ToString('o') } })
        $d = "$OUT\collect-$Tag"; Move-Old $d; New-Item -ItemType Directory -Force -Path $d | Out-Null
        foreach ($f in "$PROF\dsload.log", "$PROF\dsload.prev.log", "$PROF\rig.log", "$PROF\starter.log", "$PROF\player.log", "$PROF\player.prev.log",
                       "$GAME\BepInEx\LogOutput.log", "$GAME\BepInEx\samefn_win.log", "$GAME\BepInEx\config\BepInEx.cfg") {
            if (Test-Path -LiteralPath $f) {
                # the game keeps its logs open: read with sharing, then write the copy
                try { $fs = New-Object IO.FileStream($f, 'Open', 'Read', 'ReadWrite'); $ms = New-Object IO.MemoryStream; $fs.CopyTo($ms); $fs.Close()
                      [IO.File]::WriteAllBytes((Join-Path $d (Split-Path $f -Leaf)), $ms.ToArray()) } catch { $r.why += "copy $f failed: $($_.Exception.Message); " }
            }
        }
        if (Test-Path "$OUT\collect-$Tag.zip") { Move-Old "$OUT\collect-$Tag.zip" }
        Compress-Archive -Path "$d\*" -DestinationPath "$OUT\collect-$Tag.zip"
        $r.ok = $true
    }
    'procs' {
        $r.pids = @(Ours | ForEach-Object Id); $r.ok = $true
    }
    'kill' {
        $k = @(Ours); $r.killed = @($k | ForEach-Object { "$($_.Id) $($_.ProcessName)" })
        $k | Stop-Process -Force -ErrorAction SilentlyContinue
        for ($i = 0; $i -lt 30 -and @(Ours).Count; $i++) { Start-Sleep -Milliseconds 500 }
        $r.left = @(Ours | ForEach-Object Id); $r.ok = -not $r.left.Count
    }
    'chain' {
        # chaintest.exe (stage\chain, unpacked by setup): the loader's engine + BepInEx's Dobby detour, both orders, no game
        $d = "$STAGE\chain"
        if (-not (Test-Path "$d\chaintest.exe")) { throw "no $d\chaintest.exe (stage.zip without chain\)" }
        $p = Start-Process -FilePath "$d\chaintest.exe" -WorkingDirectory $d -WindowStyle Hidden -PassThru -Wait
        $r.exit = $p.ExitCode
        $r.log = @(Get-Content -LiteralPath "$d\chaintest.log" -ErrorAction SilentlyContinue | ForEach-Object { [string]$_ })
        $r.ok = ($p.ExitCode -eq 0)
    }
    default { throw "unknown step '$Step'" }
    }
} catch { $r.why += $_.Exception.Message }
Put-Out
