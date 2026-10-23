# Automated test harness for smart_parking_v9.
# Two ways to run it:
#   REAL BOARD:  powershell -File run_tests.ps1 -Port COM10 [-Slow] [-Soak] [-Only "A-"]
#                Close the Arduino IDE Serial Monitor first (only one program can hold the port).
#                The tests read `status` as the text picture, so before flashing set
#                `const bool TEXT_MAP = true;` in the sketch (in the shipped build `status`
#                prints only a summary and the picture goes to the web page).
#   SIMULATOR:   powershell -File run_tests.ps1 -Sim [-Slow] [-Soak] [-Only "A-"] [-Sketch <path to .ino>]
#                Builds host_sim\ (the real .ino compiled on the PC with a simulated serial port,
#                clock, sensors, LED and web handlers) and runs the same tests against it. No board needed.
#   -Slow  : include the 60 s reservation-timeout tests (~2 min)
#   -Soak  : include the 5-min sensor-noise test and the 500-car ID-limit test
#   -Only  : run only tests whose id starts with this prefix
# Each test resets the board (RTS toggle, or a fresh simulator process) so it starts from a clean state.
# Tests of kind 'sim' (SIM-*, VIS-*) need the simulator: they move the sensors and read the web payload.
param([string]$Port = "COM10", [switch]$Sim, [string]$Sketch = "", [switch]$Slow, [switch]$Soak, [string]$Only = "")
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if ($Sketch -eq "") { $Sketch = Join-Path $here '..\firmware\smart_parking_v9\smart_parking_v9.ino' }
$Sketch = (Resolve-Path $Sketch).Path

# Lot geometry is read from the firmware source so the distance tests follow whatever the .ino says.
$ino = Get-Content $Sketch -Raw -Encoding UTF8
function Get-Const([string]$name) { [double]([regex]::Match($ino, "const float $name\s*=\s*([\d.]+)").Groups[1].Value) }
# measured road lengths of the model (cm) and the sensor thresholds, all read from the .ino
$mE0 = Get-Const 'ENT_TO_FLOOR0_CM'; $mER = Get-Const 'ENT_TO_RAMP_CM'; $mRamp = Get-Const 'RAMP_CM'; $mF1 = Get-Const 'FLOOR1_TO_FIRST_CM'
$mSW = Get-Const 'SPOT_WIDTH_CM'; $mSL = Get-Const 'SPOT_LENGTH_CM'
$script:unit = [regex]::Match($ino, 'DIST_UNIT\s*=\s*"([^"]+)"').Groups[1].Value
$script:gateCarBelow = Get-Const 'GATE_CAR_BELOW_CM'   # entrance: a reading below this is a car
$script:arrive = Get-Const 'SPOT_ARRIVE_CM'; $script:leave = Get-Const 'SPOT_LEAVE_CM'
$ci = [Globalization.CultureInfo]::InvariantCulture
# independent hand formula: floor 0 = entrance->floor-0 lane + k stall widths + into the stall;
# floor 1 = entrance->ramp + ramp + floor-1 entrance->lane + k stall widths + into the stall
$script:wantDist = @(); for ($k = 0; $k -lt 7; $k++) { $script:wantDist += ($mE0 + $k * $mSW + $mSL) }
for ($k = 0; $k -lt 7; $k++) { $script:wantDist += ($mER + $mRamp + $mF1 + $k * $mSW + $mSL) }

$script:sp = $null; $script:buf = ""; $script:boot = @()
$script:tr = New-Object System.Collections.Generic.List[string]
$script:fails = $null; $script:infos = $null
$script:tests = @(); $script:results = @()

# ---------------------------------------------------------------- serial layer
# Simulator: a drop-in replacement for System.IO.Ports.SerialPort that talks to host_sim.exe over pipes.
# Toggling RtsEnable true->false (what Reset-Board does) starts a fresh simulator process = a board reset.
if ($Sim) {
  Add-Type -TypeDefinition @"
using System;
using System.Diagnostics;
using System.Text;
using System.Threading;
public class FakePort {
  public string NewLine = "\n"; public int ReadBufferSize; public int WriteTimeout; public bool DtrEnable;
  public string Exe; bool rts; Process p; int gen; StringBuilder buf = new StringBuilder(); object lk = new object();
  public FakePort(string exe) { Exe = exe; }
  public bool IsOpen { get { return p != null && !p.HasExited; } }
  public void Open() { Start(); }
  public void Close() { Stop(); }
  void Stop() { if (p != null) { try { if (!p.HasExited) p.Kill(); } catch (Exception) { } p = null; } }
  void Start() {
    Stop();
    ProcessStartInfo psi = new ProcessStartInfo(Exe);
    psi.UseShellExecute = false; psi.RedirectStandardInput = true; psi.RedirectStandardOutput = true; psi.CreateNoWindow = true;
    psi.StandardOutputEncoding = Encoding.ASCII;
    lock (lk) { gen++; buf.Length = 0; }
    int my = gen; Process proc = Process.Start(psi); p = proc;
    Thread t = new Thread(delegate() {
      char[] c = new char[4096]; int n;
      while ((n = proc.StandardOutput.Read(c, 0, c.Length)) > 0) { lock (lk) { if (my == gen) buf.Append(c, 0, n); } }
    });
    t.IsBackground = true; t.Start();
  }
  public bool RtsEnable { get { return rts; } set { bool was = rts; rts = value; if (was && !value) Start(); } }
  public int BytesToRead { get { lock (lk) { return buf.Length; } } }
  public string ReadExisting() { lock (lk) { string s = buf.ToString(); buf.Length = 0; return s; } }
  public void Write(string s) { byte[] b = Encoding.ASCII.GetBytes(s); p.StandardInput.BaseStream.Write(b, 0, b.Length); p.StandardInput.BaseStream.Flush(); }
}
"@
  # three builds of the same sketch: text picture (what the tests parse), as shipped (web only), WiFi off
  $simDir = Join-Path $env:TEMP 'parking_sim'
  New-Item -ItemType Directory $simDir -Force | Out-Null
  $build = Join-Path $here 'host_sim\build.ps1'
  $script:simText = Join-Path $simDir 'sim_text.exe'; $script:simWeb = Join-Path $simDir 'sim_web.exe'; $script:simNoWeb = Join-Path $simDir 'sim_noweb.exe'
  $script:simExit = Join-Path $simDir 'sim_exit.exe'   # optional two-lane entrance sensor switched on (exit ~22, entry ~26)
  Write-Output "building simulators from $Sketch ..."
  [void](& $build -Sketch $Sketch -Out $script:simText -TextMap)
  [void](& $build -Sketch $Sketch -Out $script:simWeb)
  [void](& $build -Sketch $Sketch -Out $script:simNoWeb -NoWeb)
  [void](& $build -Sketch $Sketch -Out $script:simExit -TextMap -ExitLane)
}

function Open-Port {
  if ($script:sp -and $script:sp.IsOpen) { $script:sp.Close() }
  if ($Sim) { $p = New-Object FakePort $script:simText }
  else      { $p = New-Object System.IO.Ports.SerialPort $Port, 9600, 'None', 8, 'One' }
  $p.NewLine = "`n"; $p.ReadBufferSize = 262144; $p.WriteTimeout = 10000
  $p.Open(); $script:sp = $p
}

# SIM only: restart the board as a different build (text picture / as shipped / WiFi off) for the current test
function Use-Build([string]$exe) { $script:sp.Exe = $exe; Reset-Board }

# Collect lines until the line has been quiet for $quietMs (or $maxMs elapsed).
function Read-Burst([int]$quietMs = 400, [int]$maxMs = 8000) {
  $lines = New-Object System.Collections.Generic.List[string]
  $sw = [Diagnostics.Stopwatch]::StartNew(); $last = 0
  while ($sw.ElapsedMilliseconds -lt $maxMs) {
    if ($script:sp.BytesToRead -gt 0) {
      $script:buf += $script:sp.ReadExisting(); $last = $sw.ElapsedMilliseconds
      while (($i = $script:buf.IndexOf("`n")) -ge 0) {
        $lines.Add($script:buf.Substring(0, $i).TrimEnd("`r"))
        $script:buf = $script:buf.Substring($i + 1)
      }
    } else {
      if (($sw.ElapsedMilliseconds - $last) -ge $quietMs) { break }
      Start-Sleep -Milliseconds 10
    }
  }
  foreach ($l in $lines) { $script:tr.Add("   < $l") }
  return $lines.ToArray()
}

function Cmd([string]$c, [int]$quiet = 400, [int]$max = 8000) {
  $d = @(Read-Burst 30 150)
  $bad = @($d -match '^\[(ENTRY|PARK|LEAVE)\]')
  if ($bad.Count -gt 0 -and $script:fails -ne $null) { $script:fails.Add("UNSOLICITED event (sensor noise?): " + ($bad -join ' || ')) }
  $script:tr.Add("> $c")
  $script:sp.Write($c + "`n")
  return @(Read-Burst $quiet $max)
}

function Reset-Board {
  if (-not ($script:sp -and $script:sp.IsOpen)) { Open-Port }
  for ($try = 0; $try -lt 3; $try++) {
    $p = $script:sp
    $p.DtrEnable = $false; $p.RtsEnable = $true; Start-Sleep -Milliseconds 150; $p.RtsEnable = $false
    $script:buf = ""
    $b = @(Read-Burst 1800 8000)
    if (@($b -match '\[BOOT\]').Count -gt 0) { $script:boot = $b; return }
  }
  throw "board did not reboot / no [BOOT] banner after 3 resets"
}

# ---------------------------------------------------------------- assertions
function Ok([bool]$c, [string]$m) { if (-not $c) { $script:fails.Add($m) } }
function Has($lines, [string]$re, [string]$m) {
  if (@(@($lines) -match $re).Count -eq 0) { $script:fails.Add("$m   [expected /$re/ got: " + ((@($lines) -join ' | ')) + "]") }
}
function HasNot($lines, [string]$re, [string]$m) {
  if (@(@($lines) -match $re).Count -gt 0) { $script:fails.Add("$m   [unexpected /$re/ in: " + ((@($lines) -join ' | ')) + "]") }
}
function Info([string]$m) { $script:infos.Add($m) }

# ---------------------------------------------------------------- helpers
function NewCar {
  $r = @(Cmd "new")
  $hit = @($r -match '\[ENTRY\]\s+car #\d+')
  if ($hit.Count -eq 0) { return $null }
  $m = [regex]::Match($hit[0], '\[ENTRY\]\s+car #(\d+)\s+-> go to spot (\d+) \(floor (\d)\)\s+dist ([\d.]+) (?:cm|m)')
  return [pscustomobject]@{ Id = [int]$m.Groups[1].Value; Spot = [int]$m.Groups[2].Value; Dist = [double]$m.Groups[4].Value; Lines = $r }
}

# v9 `status` = a [STATUS] summary line + the text picture of the lot (needs TEXT_MAP = true or WiFi off, see the header).
# Every stall in the picture is a cell like  |  6      free    |  or  | 10   [#3 WRONG] |  or  |  0*  RES #4 45s |
# and the cars that have not parked yet are listed on a  WAITING: #2 -> spot 2 (60s) | #3 -> no spot  line.
function GetStatus {
  $r = @(Cmd "status" 400 12000)
  $st = @{ Spots = @{}; Occ = -1; Res = -1; Free = -1; Blocked = 0; Next = ''; Waiting = @(); Lines = $r }
  foreach ($l in $r) {
    if ($l -match '^\[STATUS\]\s+(\d+) occupied, (\d+) reserved, (?:(\d+) blocked, )?(\d+) free') {
      $st.Occ = [int]$Matches[1]; $st.Res = [int]$Matches[2]; $st.Free = [int]$Matches[4]
      if ($Matches[3]) { $st.Blocked = [int]$Matches[3] }
    } elseif ($l -match '^\[NEXT\]') { $st.Next = $l }
    elseif ($l -match '^\s+WAITING:\s*(.*)$') {
      foreach ($w in ($Matches[1] -split '\s\|\s')) {
        if ($w -match '^#(\d+) -> spot (\d+) \((\d+)s\)') { $st.Waiting += @{ Car = [int]$Matches[1]; Spot = [int]$Matches[2]; Left = [int]$Matches[3] } }
        elseif ($w -match '^#(\d+) -> no spot') { $st.Waiting += @{ Car = [int]$Matches[1]; Spot = -1; Left = 0 } }
      }
    } else {
      foreach ($m in [regex]::Matches($l, '\|\s*(\d{1,2})\*?\s+(free|>> NEXT <<|//BLOCKED//|RES #(\d+) \d+s|\[#(\d+) (OK|WRONG)\])\s*\|')) {
        $n = [int]$m.Groups[1].Value; $t = $m.Groups[2].Value
        $o = @{ State = 'free'; Car = -1; Correct = $null }
        if ($m.Groups[4].Success) { $o.State = 'OCCUPIED'; $o.Car = [int]$m.Groups[4].Value; $o.Correct = ($m.Groups[5].Value -eq 'OK') }
        elseif ($m.Groups[3].Success) { $o.State = 'RESERVED'; $o.Car = [int]$m.Groups[3].Value }
        elseif ($t -eq '//BLOCKED//') { $o.State = 'BLOCKED' }
        $st.Spots[$n] = $o
      }
    }
  }
  return $st
}

function Def([string]$id, [string]$group, [string]$title, [scriptblock]$body, [string]$kind = "") {
  if ($kind -eq "" -and $id -match '^(SIM|VIS)-') { $kind = 'sim' }   # these need the simulator (sensor control / web payload)
  $script:tests +=[pscustomobject]@{ Id = $id; Group = $group; Title = $title; Body = $body; Kind = $kind }
}

# ================================================================ TESTS
# ---- Boot / sensors -------------------------------------------------------
Def 'BOOT-1' 'Boot' 'Boot banner: 14 spots, A* distances (nearest/farthest) match the .ino constants, 60 s timeout' {
  Has $script:boot '\[BOOT\]\s+ready: 14 spots, 2 floors' 'spot count banner'
  $near = [regex]::Escape($script:wantDist[0].ToString('F1', $ci)); $far = [regex]::Escape($script:wantDist[13].ToString('F1', $ci))
  Has $script:boot "nearest spot $near $($script:unit) \| farthest $far $($script:unit)" 'A* distance banner'
  Has $script:boot "entrance sensor: .* car is counted below $([regex]::Escape($script:gateCarBelow.ToString('F1', $ci))) cm" 'entrance threshold banner'
  Has $script:boot "spot 0 sensor: car arrives below $([regex]::Escape($script:arrive.ToString('F1', $ci))) cm, leaves above $([regex]::Escape($script:leave.ToString('F1', $ci))) cm" 'spot threshold banner'
  Has $script:boot 'reservations expire after 60 s' 'timeout banner'
}
Def 'BOOT-2' 'Boot' 'Clean state after reset: 14 free, next car -> spot 0' {
  $s = GetStatus
  Ok ($s.Free -eq 14 -and $s.Occ -eq 0 -and $s.Res -eq 0) "expected 14 free, got occ=$($s.Occ) res=$($s.Res) free=$($s.Free)"
  $near = [regex]::Escape($script:wantDist[0].ToString('F1', $ci))
  Has $s.Lines "next car -> spot 0 \(floor 0\)\s+dist $near $($script:unit)" '[NEXT] after boot'
  Ok ($s.Blocked -eq 0) 'nothing may be BLOCKED right after boot (sensor sees only the floor)'
}
Def 'SENS-1' 'Sensors' 'Idle 20 s: no phantom entry/park/leave events (sensor noise)' {
  $r = @(Read-Burst 99999 20000)
  HasNot $r '\[(ENTRY|PARK|LEAVE|REROUTE)\]' 'phantom event while nothing is happening'
}
Def 'SENS-2' 'Sensors' 'check command: ~6 readings, valid echoes, idle values beyond thresholds' {
  $r = @(Cmd "check" 800 7000)
  $rows = @($r -match '^\[CHECK\]\s+entrance')
  Ok ($rows.Count -ge 5 -and $rows.Count -le 8) "expected 5-8 readings in 3 s, got $($rows.Count)"
  $miss = 0; $spots = @(); $ents = @()
  foreach ($l in $rows) {
    $m = [regex]::Match($l, 'entrance\s+([\d.]+|--)\s*(cm)?\s*\|\s*spot\s+([\d.]+|--)')
    if ($m.Groups[1].Value -eq '--' -or $m.Groups[3].Value -eq '--') { $miss++ }
    if ($m.Groups[3].Value -match '^[\d.]+$') { $spots += [double]$m.Groups[3].Value }
    if ($m.Groups[1].Value -match '^[\d.]+$') { $ents += [double]$m.Groups[1].Value }
  }
  if ($spots.Count) { Info ("spot sensor idle range: {0:N1} .. {1:N1} cm (car arrives below $($script:arrive))" -f ($spots | Measure-Object -Minimum).Minimum, ($spots | Measure-Object -Maximum).Maximum) }
  if ($ents.Count) { Info ("entrance sensor idle range: {0:N1} .. {1:N1} cm (a car is counted below $($script:gateCarBelow))" -f ($ents | Measure-Object -Minimum).Minimum, ($ents | Measure-Object -Maximum).Maximum) }
  Ok ($miss -eq 0) "$miss of $($rows.Count) readings had no echo (--)"
  Ok (@($spots | Where-Object { $_ -lt $script:arrive }).Count -eq 0) "spot sensor read below the arrive threshold ($($script:arrive) cm) with nothing on spot 0"
  Ok (@($ents | Where-Object { $_ -lt $script:gateCarBelow }).Count -eq 0) "entrance sensor read below the car threshold ($($script:gateCarBelow) cm) with nothing at the entrance - the wall must be farther"
  $after = @(Read-Burst 1500 2000)
  HasNot $after '\[CHECK\]\s+entrance' 'check output continues after the 3 s window'
}

Def 'SENS-3' 'Sensors' 'check [sec]: window length honoured (1, 5, 60 accepted; default 3), stops on time' {
  $r = @(Cmd "check 1" 600 4000)
  Has $r 'sensor readings for 1 seconds' 'check 1 banner'
  $n = @($r -match '^\[CHECK\]\s+entrance').Count
  Ok ($n -ge 1 -and $n -le 3) "check 1 should give 1-3 rows, got $n"
  [void](Read-Burst 1500 3000)
  $r = @(Cmd "check 5" 800 9000)
  Has $r 'sensor readings for 5 seconds' 'check 5 banner'
  $n = @($r -match '^\[CHECK\]\s+entrance').Count
  Ok ($n -ge 7 -and $n -le 11) "check 5 should give ~9 rows (5 s / 0.56 s), got $n"
  $after = @(Read-Burst 1800 2500)
  HasNot $after '\[CHECK\]\s+entrance' 'rows after the 5 s window'
  Has @(Cmd "check 60" 800 3000) 'sensor readings for 60 seconds' 'upper bound 60 accepted'
}

# ---- A* distances ----------------------------------------------------------
Def 'DIST-1' 'A*' 'Fill the lot: all 14 spot distances match an independent hand formula built from the .ino constants' {
  # Hand formula (independent of the firmware's A*), constants read from the .ino:
  #  floor 0 spot k: (1+k)*WIDTH + DEPTH
  #  floor 1 spot k: 7*WIDTH + RAMP (arrival cell) + k*WIDTH along the lane + DEPTH into the spot
  $want = $script:wantDist
  for ($i = 0; $i -lt 14; $i++) {
    $c = NewCar
    if ($c -eq $null) { Ok $false "new #$i produced no car"; return }
    Ok ($c.Id -eq $i) "car id: expected $i got $($c.Id)"
    Ok ($c.Spot -eq $i) "car $i spot: expected $i got $($c.Spot)"
    Ok ([math]::Abs($c.Dist - $want[$i]) -lt 0.05) "car $i dist: expected $($want[$i]) got $($c.Dist)"
    if ($i -eq 0) { Has $c.Lines '\[PHYSICAL\]' 'spot 0 not tagged PHYSICAL' } else { HasNot $c.Lines '\[PHYSICAL\]' "spot $i wrongly tagged PHYSICAL" }
  }
}
Def 'FULL-1' 'A*' 'Lot full: 15th car turned away, no ID burned, spot reusable after leave' {
  for ($i = 0; $i -lt 14; $i++) { [void](NewCar) }
  $r = @(Cmd "new")
  Has $r 'LOT FULL' '15th car must be turned away'
  HasNot $r '\[ENTRY\]\s+car' 'no car may be created when full'
  $s = GetStatus
  Has $s.Lines 'lot full' '[NEXT] must say lot full'
  [void](Cmd "leave 3")
  $c = NewCar
  Ok ($c -ne $null -and $c.Spot -eq 3) "freed spot 3 should be next, got $(if($c){$c.Spot}else{'none'})"
  Ok ($c -ne $null -and $c.Id -eq 14) "id must not be consumed by the rejected car (expect 14, got $(if($c){$c.Id}else{'none'}))"
}

# ---- Assignment logic ------------------------------------------------------
Def 'A-1' 'Assignment' 'Regression: first car gets ID 0 (no phantom cars from boot noise)' {
  $c = NewCar
  Ok ($c.Id -eq 0) "first car id = $($c.Id), expected 0"
  Ok ($c.Spot -eq 0) "first car spot = $($c.Spot), expected 0"
}
Def 'A-2' 'Assignment' 'Regression: consecutive new cars never share a spot' {
  $seen = @{}
  for ($i = 0; $i -lt 6; $i++) { $c = NewCar; Ok (-not $seen.ContainsKey($c.Spot)) "spot $($c.Spot) assigned twice"; $seen[$c.Spot] = 1 }
}
Def 'A-3' 'Assignment' 'Park at assigned spot -> CORRECT, status OCCUPIED/correct, next advances' {
  $c = NewCar
  $r = @(Cmd "park $($c.Spot) $($c.Id)")
  Has $r 'CORRECT' 'park at own spot'
  HasNot $r 'WRONG' 'must not be wrong'
  Has $r '\[NEXT\]\s+next car -> spot 1 ' 'next recommendation'
  $s = GetStatus
  Ok ($s.Spots[0].State -eq 'OCCUPIED' -and $s.Spots[0].Correct -eq $true) 'status row for spot 0'
}
Def 'A-4' 'Assignment' 'Wrong spot: own reservation released immediately and reused by next car' {
  $a = NewCar
  $r = @(Cmd "park 5 $($a.Id)")
  Has $r 'WRONG SPOT!\s+\(assigned spot 0 released\)' 'wrong-spot message'
  $c = NewCar
  Ok ($c.Spot -eq 0) "released spot 0 should be next, got $($c.Spot)"
  $s = GetStatus
  Ok ($s.Spots[5].State -eq 'OCCUPIED' -and $s.Spots[5].Correct -eq $false) 'spot 5 should be OCCUPIED WRONG'
}
Def 'A-5' 'Assignment' 'Taking another car''s reserved spot: that car is rerouted and can park CORRECT' {
  $a = NewCar; $b = NewCar    # a->0, b->1
  $r = @(Cmd "park 1 $($a.Id)")
  Has $r "\[REROUTE\]\s+car #$($b.Id): spot 1 was taken, go to spot 0 " 'b rerouted to the freed spot 0'
  $r2 = @(Cmd "park 0 $($b.Id)")
  Has $r2 'CORRECT' 'b parks at its new spot'
}
Def 'A-6' 'Assignment' 'Reroute chain keeps one assignment per car' {
  $a = NewCar; $b = NewCar; $c = NewCar              # 0,1,2
  [void](Cmd "park 1 $($a.Id)")                       # b -> 0
  $r = @(Cmd "park 0 $($c.Id)")                       # c takes b's spot, b -> 2
  Has $r "\[REROUTE\]\s+car #$($b.Id): spot 0 was taken, go to spot 2 " 'second reroute'
  $s = GetStatus
  Ok ($s.Occ -eq 2 -and $s.Res -eq 1) "expected 2 occupied 1 reserved, got $($s.Occ)/$($s.Res)"
  Ok ($s.Spots[2].State -eq 'RESERVED' -and $s.Spots[2].Car -eq $b.Id) 'b should hold spot 2'
}
Def 'A-7' 'Assignment' 'Full lot + wrong park: displaced car gets the freed spot (no "no free spot left")' {
  for ($i = 0; $i -lt 14; $i++) { [void](NewCar) }
  $r = @(Cmd "park 5 13")
  Has $r '\[REROUTE\]\s+car #5: spot 5 was taken, go to spot 13 ' 'car 5 should get car 13''s old spot'
  HasNot $r 'no free spot left' 'must not strand a car'
}
Def 'A-8' 'Assignment' 'Same car cannot park twice (regression: double parking)' {
  $a = NewCar
  [void](Cmd "park 0 $($a.Id)")
  $r = @(Cmd "park 3 $($a.Id)")
  Has $r 'already parked at spot 0' 'second park must be rejected'
  $s = GetStatus
  Ok ($s.Occ -eq 1 -and $s.Spots[3].State -eq 'free') 'state must be unchanged'
}
Def 'A-9' 'Assignment' 'Cannot park on an occupied spot; other car keeps its reservation' {
  $a = NewCar; $b = NewCar
  [void](Cmd "park 0 $($a.Id)")
  $r = @(Cmd "park 0 $($b.Id)")
  Has $r "already taken by car #$($a.Id)" 'occupied spot'
  $s = GetStatus
  Ok ($s.Spots[1].State -eq 'RESERVED' -and $s.Spots[1].Car -eq $b.Id) 'b must keep reservation for spot 1'
}
Def 'A-10' 'Assignment' 'Leave: parked / reserved-only / twice / alias cancel / ids never reused' {
  $a = NewCar; $b = NewCar; $c = NewCar
  [void](Cmd "park 0 $($a.Id)")
  Has @(Cmd "leave $($a.Id)") 'left spot 0 - exited the lot' 'leave parked'
  Has @(Cmd "leave $($b.Id)") 'exited the lot without parking' 'leave while still driving'
  Has @(Cmd "leave $($b.Id)") 'not in the lot' 'second leave'
  Has @(Cmd "cancel $($c.Id)") 'exited the lot without parking' 'cancel alias'
  $s = GetStatus
  Ok ($s.Free -eq 14) "all spots should be free, got $($s.Free)"
  $d = NewCar
  Ok ($d.Id -eq 3) "ids must not be reused: expected 3 got $($d.Id)"
  Has @(Cmd "park 0 $($a.Id)") 'not in the lot' 'exited car cannot park'
}
Def 'A-11' 'Assignment' 'Leaving a WRONG-parked car frees the spot it really used, not its old one' {
  $a = NewCar
  [void](Cmd "park 4 $($a.Id)")
  [void](Cmd "leave $($a.Id)")
  $s = GetStatus
  Ok ($s.Free -eq 14) "all 14 should be free, got occ=$($s.Occ) res=$($s.Res)"
}
Def 'A-12' 'Assignment' 'Park on a free, unreserved spot by a car with its own reservation' {
  $a = NewCar
  $r = @(Cmd "park 9 $($a.Id)")
  Has $r 'WRONG SPOT!' 'wrong spot'
  HasNot $r '\[REROUTE\]' 'nobody to reroute'
}
Def 'A-13' 'Assignment' 'Input validation: bad spot / id / usage, state untouched' {
  $a = NewCar
  $cases = @(
    @('park 14 0', 'invalid spot number'), @('park -1 0', 'invalid spot number'),
    @('park 0 -1', 'invalid car id'), @('park 0 99', 'is not in the lot'),
    @('park 0', 'usage: park'), @('park', 'usage: park'), @('park a b', 'usage: park'),
    @('leave', 'usage: leave'), @('leave x', 'usage: leave'), @('cancel', 'usage: leave'),
    @('leave -1', 'invalid car id'), @('leave 99', 'is not in the lot'),
    @('park -99999 0', 'invalid spot number'), @('park 100000 0', 'usage: park'), @('park -100000 0', 'usage: park'),
    @('park 0 100000', 'usage: park'), @('leave 100000', 'usage: leave'), @('leave 99999', 'is not in the lot'))
  foreach ($c in $cases) { Has @(Cmd $c[0]) $c[1] ("cmd '" + $c[0] + "'") }
  $s = GetStatus
  Ok ($s.Res -eq 1 -and $s.Occ -eq 0) "state changed by invalid commands (occ=$($s.Occ) res=$($s.Res))"
}

# ---- Parser edge cases -----------------------------------------------------
Def 'P-1' 'Parser' 'Empty and whitespace-only lines are harmless; next command still works' {
  [void](Cmd "" 300); [void](Cmd "   " 300); [void](Cmd "`t" 300)
  $c = NewCar
  Ok ($c -ne $null -and $c.Id -eq 0) 'new after blank lines'
}
Def 'P-2' 'Parser' 'Unknown command names itself in the error and prints help' {
  $r = @(Cmd "bogus")
  Has $r '\[ERROR\]\s+unknown command: bogus' 'error names the command'
  Has $r '\[HELP\]' 'help follows'
  Has @(Cmd "help") '\[HELP\]' 'help'
}
Def 'P-3' 'Parser' 'Commands are case-insensitive (NEW, Park, LEAVE, Status, CHECK, Help, Cancel)' {
  $c = $null
  $r = @(Cmd "NEW"); Has $r '\[ENTRY\]\s+car #0' '"NEW"'
  $r = @(Cmd "Status"); Has $r '^\[STATUS\]' '"Status"'
  $r = @(Cmd "PARK 0 0"); Has $r 'CORRECT' '"PARK 0 0"'
  $r = @(Cmd "Check 1" 600 3000); Has $r 'sensor readings for 1 seconds' '"Check 1"'
  [void](Read-Burst 1200 2500)
  $r = @(Cmd "HELP"); Has $r '\[HELP\]' '"HELP"'
  $r = @(Cmd "LeAvE 0"); Has $r 'left spot 0' '"LeAvE 0"'
  $r = @(Cmd "NEW"); Has $r '\[ENTRY\]\s+car #1' 'second car'
  $r = @(Cmd "CANCEL 1"); Has $r 'exited the lot without parking' '"CANCEL 1"'
}
Def 'P-4' 'Parser' 'Number parsing: trailing junk, decimals, hex, signs, extra spaces/tabs' {
  $a = NewCar
  Has @(Cmd "park 1 $($a.Id)x") 'usage: park' 'trailing junk'
  Has @(Cmd "park 1.5 $($a.Id)") 'usage: park' 'decimal spot'
  Has @(Cmd "park 0x1 $($a.Id)") 'usage: park' 'hex'
  Has @(Cmd "park  0`t $($a.Id)") 'CORRECT' 'multiple spaces + tab'
}
Def 'P-5' 'Parser' 'Overflow: huge numbers are rejected, never wrap into a valid id/spot' {
  $a = NewCar
  $r1 = @(Cmd "park 99999999999999999999 $($a.Id)")
  HasNot $r1 '\[PARK\]' 'overflowing spot must not park'
  $r2 = @(Cmd "leave 4294967296")
  HasNot $r2 'left spot|exited the lot' 'overflowing id 2^32 must not wrap to id 0'
  $r3 = @(Cmd "leave 4294967297")
  HasNot $r3 'left spot|exited the lot' 'overflowing id 2^32+1 must not wrap'
}
Def 'P-6' 'Parser' 'Regression: token longer than 15 chars is rejected, not split into two arguments' {
  $a = NewCar; $b = NewCar
  $r = @(Cmd "leave 000000000000000000")   # 18 zeros: old tokenizer cut at 15 chars and read it as car 0
  HasNot $r 'exited the lot' 'car #0 removed by a malformed 18-digit argument'
  Has $r 'usage: leave' '18-digit argument must be a usage error'
  $r = @(Cmd "leave 0000000000000001")     # exactly 16 chars -> too long
  HasNot $r 'exited the lot' '16-char argument must not work'
  $r = @(Cmd "leave 000000000000001")      # exactly 15 chars -> still a valid argument (car 1)
  Has $r "car #$($b.Id)\s+exited the lot" '15-char argument is the longest legal one'
}
Def 'P-10' 'Parser' 'Command word longer than 15 chars: reported as too long, no crash' {
  $r = @(Cmd ('n' * 20))
  Has $r 'unknown command: \(too long\)' 'too-long command message'
  $c = NewCar; Ok ($c -ne $null) 'board must still work'
}
Def 'P-7' 'Parser' 'Very long line (200 chars) and 47/48-char boundary lines do not crash' {
  $r = @(Cmd ('a' * 200)); Has $r '\[HELP\]' '200-char line'
  $r = @(Cmd ('b' * 47));  Has $r '\[HELP\]' '47-char line'
  $r = @(Cmd ('c' * 48));  Has $r '\[HELP\]' '48-char line'
  $c = NewCar; Ok ($c -ne $null) 'board unresponsive after long lines'
}
Def 'P-8' 'Parser' 'Line endings: LF and CRLF work; CR-only does not execute' {
  $script:sp.Write("new`r`n"); $r = @(Read-Burst 500 3000)
  Has $r '\[ENTRY\]' 'CRLF'
  $script:sp.Write("new`r"); $r = @(Read-Burst 800 2000)
  if (@($r -match '\[ENTRY\]').Count -eq 0) { Info 'CR-only terminator is ignored (Serial Monitor must be set to "Newline" or "Both NL & CR")' }
}
Def 'P-9' 'Parser' 'Wrong argument count is rejected with a usage line and changes nothing' {
  $a = NewCar
  $cases = @(
    @('new 5 6', 'usage: new'), @('status foo', 'usage: status'),
    @("park 0 $($a.Id) 77", 'usage: park'), @("leave $($a.Id) 1", 'usage: leave'), @("cancel $($a.Id) 1", 'usage: leave'),
    @('check 1 2', 'usage: check'), @('check 0', 'usage: check'), @('check 61', 'usage: check'), @('check -3', 'usage: check'), @('check abc', 'usage: check'))
  foreach ($c in $cases) {
    $r = @(Cmd $c[0])
    Has $r $c[1] ("cmd '" + $c[0] + "'")
    HasNot $r '\[(ENTRY|PARK|LEAVE)\]|sensor readings' ("cmd '" + $c[0] + "' must have no effect")
  }
  $s = GetStatus
  Ok ($s.Res -eq 1 -and $s.Occ -eq 0) "state changed (occ=$($s.Occ) res=$($s.Res))"
}

# ---- Stress / timing -------------------------------------------------------
Def 'S-1' 'Stress' 'Burst of 10 commands in one write: all answered' {
  $script:sp.Write(("new`n" * 10)); $r = @(Read-Burst 800 8000)
  Ok (@($r -match '\[ENTRY\]\s+car').Count -eq 10) "expected 10 [ENTRY], got $(@($r -match '\[ENTRY\]\s+car').Count)"
}
Def 'S-2' 'Stress' 'Burst of 30 status requests: none lost or truncated (RX buffer)' {
  $script:sp.Write(("status`n" * 30)); $r = @(Read-Burst 2500 90000)
  $n = @($r -match '^\[STATUS\]').Count
  Ok ($n -eq 30) "expected 30 [STATUS] headers, got $n (RX buffer overflow / dropped commands)"
}
Def 'S-3' 'Stress' 'Command latency: response to "new" within 800 ms' {
  $times = @()
  for ($i = 0; $i -lt 6; $i++) {
    [void](Read-Burst 30 150)
    $sw = [Diagnostics.Stopwatch]::StartNew(); $script:sp.Write("new`n")
    while ($script:sp.BytesToRead -eq 0 -and $sw.ElapsedMilliseconds -lt 3000) { Start-Sleep -Milliseconds 2 }
    $times += $sw.ElapsedMilliseconds
    [void](Read-Burst 300 3000)
  }
  $mx = ($times | Measure-Object -Maximum).Maximum
  Info ("latency ms: " + ($times -join ', '))
  Ok ($mx -le 800) "max latency $mx ms"
}

# ---- Model-based fuzz ------------------------------------------------------
function M-Free($m) { for ($i = 0; $i -lt 14; $i++) { if (-not $m.occ.ContainsKey($i) -and -not $m.res.ContainsKey($i)) { return $i } }; return -1 }
function M-Of($h, $car) { foreach ($k in @($h.Keys)) { if ($h[$k] -eq $car) { return [int]$k } }; return -1 }
function M-Reserve($m, $car) { $s = M-Free $m; if ($s -ge 0) { $m.res[$s] = $car; $m.at[$s] = Get-Date }; return $s }

# Applies an op to the reference model, returns the list of regexes the board's reply must satisfy.
function M-Do($m, $op) {
  switch ($op.k) {
    'new' {
      $s = M-Free $m
      if ($s -lt 0) { return @('LOT FULL') }
      $id = $m.next; $m.next++; $m.inLot[$id] = 1; [void](M-Reserve $m $id)
      return @("\[ENTRY\]\s+car #$id\s+-> go to spot $s ")
    }
    'junk' { return @('\[HELP\]') }
    'park' {
      $sp = $op.spot; $id = $op.id
      if ($sp -lt 0 -or $sp -ge 14) { return @('invalid spot number') }
      if ($id -lt 0) { return @('invalid car id') }
      if (-not $m.inLot.ContainsKey($id)) { return @("car #$id is not in the lot") }
      $at = M-Of $m.occ $id
      if ($at -ge 0) { return @("car #$id is already parked at spot $at") }
      if ($m.occ.ContainsKey($sp)) { return @("spot $sp already taken by car #$($m.occ[$sp])") }
      $own = M-Of $m.res $id
      $disp = -1; if ($m.res.ContainsKey($sp) -and $m.res[$sp] -ne $id) { $disp = $m.res[$sp] }
      if ($own -ge 0) { $m.res.Remove($own) }
      if ($m.res.ContainsKey($sp)) { $m.res.Remove($sp) }
      $m.occ[$sp] = $id
      $verdict = if ($own -eq $sp) { 'CORRECT' } else { 'WRONG SPOT!' }
      $exp = @("\[PARK\]\s+car #$id\s+@ spot $sp .*-> $([regex]::Escape($verdict))")
      if ($disp -ge 0) {
        $t = M-Reserve $m $disp
        if ($t -ge 0) { $exp += "\[REROUTE\]\s+car #${disp}: spot $sp was taken, go to spot $t " }
        else {
          $exp += "\[REROUTE\]\s+car #${disp}: spot $sp was taken and the lot is full"
          $exp += "\[LEAVE\]\s+car #${disp} must leave"
          $m.inLot.Remove($disp)
        }
      }
      return $exp
    }
    'leave' {
      $id = $op.id
      if ($id -lt 0) { return @('invalid car id') }
      if (-not $m.inLot.ContainsKey($id)) { return @("car #$id is not in the lot") }
      $at = M-Of $m.occ $id
      $m.inLot.Remove($id)
      if ($at -ge 0) { $m.occ.Remove($at); return @("\[LEAVE\]\s+car #$id\s+left spot $at - exited the lot") }
      $r = M-Of $m.res $id; if ($r -ge 0) { $m.res.Remove($r) }
      return @("\[LEAVE\]\s+car #$id\s+exited the lot without parking")
    }
  }
}

Def 'FUZZ-1' 'Model' 'Differential fuzz (110 random ops, seed 20260), board vs reference model of the spec' {
  $m = @{ next = 0; inLot = @{}; res = @{}; occ = @{}; at = @{} }
  $rng = New-Object System.Random 20260
  for ($n = 1; $n -le 110; $n++) {
    # keep reservations younger than the 60 s timeout, otherwise the model (no clock) would diverge
    $old = $null
    foreach ($k in @($m.res.Keys)) { if (((Get-Date) - $m.at[$k]).TotalSeconds -gt 40) { $old = $m.res[$k]; break } }
    $cars = @($m.inLot.Keys)
    if ($old -ne $null) { $op = @{ k = 'leave'; id = [int]$old }; $line = "leave $old" }
    else {
      $r = $rng.Next(100)
      if ($r -lt 30) { $op = @{ k = 'new' }; $line = 'new' }
      elseif ($r -lt 65) {
        if ($cars.Count -gt 0 -and $rng.Next(100) -lt 85) { $id = [int]$cars[$rng.Next($cars.Count)] } else { $id = $rng.Next(-1, $m.next + 3) }
        $own = M-Of $m.res $id
        if ($own -ge 0 -and $rng.Next(100) -lt 40) { $sp = $own } else { $sp = $rng.Next(-1, 15) }
        $op = @{ k = 'park'; spot = $sp; id = $id }; $line = "park $sp $id"
      }
      elseif ($r -lt 90) {
        if ($cars.Count -gt 0 -and $rng.Next(100) -lt 90) { $id = [int]$cars[$rng.Next($cars.Count)] } else { $id = $rng.Next(-1, $m.next + 3) }
        $op = @{ k = 'leave'; id = $id }; $line = "leave $id"
      }
      else { $op = @{ k = 'junk' }; $line = 'zzz' }
    }
    $exp = M-Do $m $op
    $resp = @(Cmd $line 350)
    foreach ($rx in $exp) {
      if (@($resp -match $rx).Count -eq 0) {
        $script:fails.Add("op #$n '$line' diverged from model. expected /$rx/ got: " + ($resp -join ' | '))
        return
      }
    }
    if ($n % 15 -eq 0) {
      $s = GetStatus
      for ($i = 0; $i -lt 14; $i++) {
        $want = if ($m.occ.ContainsKey($i)) { "OCCUPIED:$($m.occ[$i])" } elseif ($m.res.ContainsKey($i)) { "RESERVED:$($m.res[$i])" } else { 'free:-1' }
        $got = "$($s.Spots[$i].State):$($s.Spots[$i].Car)"
        if ($want -ne $got) { $script:fails.Add("after op #$n status spot ${i}: model=$want board=$got"); return }
      }
      if ($Sim) {   # the web payload must tell the same story as the model (state/car per spot, who is waiting)
        $j = Http-State
        if ($j -eq $null) { $script:fails.Add("after op #$n no /state JSON"); return }
        for ($i = 0; $i -lt 14; $i++) {
          $want = if ($m.occ.ContainsKey($i)) { "2:$($m.occ[$i])" } elseif ($m.res.ContainsKey($i)) { "1:$($m.res[$i])" } else { '0:-1' }
          $got = "$($j.sp[$i].s):$($j.sp[$i].car)"
          if ($want -ne $got) { $script:fails.Add("after op #$n JSON spot ${i}: model=$want json=$got"); return }
        }
        $parked = @($m.occ.Values | ForEach-Object { [int]$_ })
        $wantWait = @($m.inLot.Keys | ForEach-Object { [int]$_ } | Where-Object { $parked -notcontains $_ } | Sort-Object)
        $gotWait = @($j.wait | ForEach-Object { [int]$_.car } | Sort-Object)
        if (($wantWait -join ',') -ne ($gotWait -join ',')) { $script:fails.Add("after op #$n JSON waiting list: model=[$($wantWait -join ',')] json=[$($gotWait -join ',')]"); return }
        if ($j.waitN -ne $wantWait.Count) { $script:fails.Add("after op #$n JSON waitN=$($j.waitN), model $($wantWait.Count)"); return }
      }
    }
  }
}

# ---- Slow: reservation timeout --------------------------------------------
Def 'TO-1' 'Timeout' 'Reservation expires after 60 s (+-4); parked car never times out; staggered cars expire in order' {
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $a = NewCar                              # t~0, never parks
  [void](Read-Burst 99999 19000)
  $b = NewCar                              # t~20
  [void](Read-Burst 99999 4000)
  [void](Cmd "park $($b.Spot) $($b.Id)")   # t~25 parks correctly
  [void](Read-Burst 99999 10000)
  $c = NewCar                              # t~36
  $tA = $null; $tC = $null; $tB = $null
  while ($sw.Elapsed.TotalSeconds -lt 110) {
    $r = @(Read-Burst 99999 1000)
    foreach ($l in $r) {
      if ($l -match "\[TIMEOUT\] car #$($a.Id) ") { $tA = $sw.Elapsed.TotalSeconds }
      if ($l -match "\[TIMEOUT\] car #$($c.Id) ") { $tC = $sw.Elapsed.TotalSeconds }
      if ($l -match "\[TIMEOUT\] car #$($b.Id) ") { $tB = $sw.Elapsed.TotalSeconds }
    }
    if ($tA -ne $null -and $tC -ne $null) { break }
  }
  Ok ($tA -ne $null) 'car A never timed out'
  Ok ($tB -eq $null) 'parked car B must never get a TIMEOUT'
  Ok ($tC -ne $null) 'car C never timed out'
  if ($tA -ne $null) { Info ("A timed out at t={0:N1}s (created t~0)" -f $tA); Ok ($tA -ge 58 -and $tA -le 66) "A timeout at $tA s, expected ~60" }
  if ($tC -ne $null -and $tA -ne $null) { Ok ($tC -gt $tA) 'timeouts out of order' }
  # what happens to the stranded car afterwards
  $s = GetStatus
  Ok ($s.Spots[$a.Spot].State -eq 'free') "A's spot should be free after timeout, is $($s.Spots[$a.Spot].State)"
  $r = @(Cmd "park $($a.Spot) $($a.Id)")
  if (@($r -match 'CORRECT').Count -gt 0) { Info 'late arrival at its ORIGINAL spot counts CORRECT' } else { Info 'late arrival at its ORIGINAL spot counts as WRONG (open design question)' }
  Has @(Cmd "leave $($a.Id)") 'left spot|exited the lot' 'stranded car can still leave'
} 'slow'

Def 'TO-2' 'Timeout' 'Full lot, stranded car (timed out) parks on a reserved spot: displaced car must leave cleanly' {
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $a = NewCar                              # car 0 -> spot 0, will time out first
  [void](Read-Burst 99999 36000)
  for ($i = 1; $i -le 13; $i++) { [void](NewCar) }   # cars 1..13 reserve spots 1..13 at t~36-43 s
  $saw = $false
  while ($sw.Elapsed.TotalSeconds -lt 75 -and -not $saw) {
    $r = @(Read-Burst 99999 1000)
    if (@($r -match '\[TIMEOUT\] car #0 ').Count -gt 0) { $saw = $true }
  }
  Ok $saw 'car 0 never timed out'
  $z = NewCar                              # takes the released spot 0 -> lot completely reserved again
  Ok ($z -ne $null -and $z.Spot -eq 0) "new car should get released spot 0, got $(if($z){$z.Spot}else{'none'})"
  $r = @(Cmd "park 5 0")                   # car 0 has NO reservation; spot 5 belongs to car 5; nothing is free
  Has $r '\[PARK\]\s+car #0\s+@ spot 5 .*WRONG SPOT!' 'stranded car parks wrong'
  HasNot $r 'released\)' 'stranded car had no reservation to release'
  Has $r '\[REROUTE\]\s+car #5: spot 5 was taken and the lot is full' 'displaced car told the lot is full'
  Has $r '\[LEAVE\]\s+car #5 must leave' 'displaced car exits'
  Has $r '\[NEXT\]\s+lot full' 'no spot is recommended'
  Has @(Cmd "leave 5") 'car #5 is not in the lot' 'car 5 must be gone'
  Has @(Cmd "park 5 5") 'not in the lot' 'car 5 cannot park any more'
  $s = GetStatus
  Ok ($s.Occ -eq 1 -and $s.Res -eq 13) "expected 1 occupied + 13 reserved, got $($s.Occ)/$($s.Res)"
  Ok ($s.Spots[5].State -eq 'OCCUPIED' -and $s.Spots[5].Car -eq 0) 'spot 5 must hold car 0'
} 'slow'

# ---- Soak: sensor glitch statistics ---------------------------------------
Def 'SENS-4' 'Sensors' 'Idle 5 min: sensor glitch rate (<2 cm / no echo) and phantom [SENSOR] events' {
  $rows = 0; $tiny = 0; $noecho = 0; $run = 0; $maxRun = 0; $events = @(); $spotVals = @()
  for ($w = 0; $w -lt 5; $w++) {
    $script:sp.Write("check 60`n")
    $r = @(Read-Burst 99999 61500)
    foreach ($l in $r) {
      if ($l -match '^\[SENSOR\]|^\[WARN\]|^\[ENTRY\]|^\[PARK\]|^\[LEAVE\]') { $events += $l }
      $m = [regex]::Match($l, '^\[CHECK\]\s+entrance\s+([\d.]+|--)\s*(?:cm)?\s*\|\s*spot\s+([\d.]+|--)')
      if ($m.Success) {
        $rows++
        $s = $m.Groups[2].Value
        if ($s -eq '--') { $noecho++ }
        elseif ([double]$s -lt 2.0) { $tiny++; $run++; if ($run -gt $maxRun) { $maxRun = $run } }
        else { $run = 0; $spotVals += [double]$s }
      }
    }
  }
  Info ("{0} readings: {1} tiny (<2 cm, impossible for HC-SR04) = {2:P1}, {3} no-echo = {4:P1}, longest run of tiny readings = {5}" -f $rows, $tiny, ($tiny / [math]::Max($rows, 1)), $noecho, ($noecho / [math]::Max($rows, 1)), $maxRun)
  if ($spotVals.Count) { Info ("valid spot readings: {0:N1} .. {1:N1} cm" -f ($spotVals | Measure-Object -Minimum).Minimum, ($spotVals | Measure-Object -Maximum).Maximum) }
  Ok ($rows -ge 400) "only $rows readings collected in 5 min"
  Ok ($events.Count -eq 0) ("phantom events while idle: " + ($events -join ' || '))
} 'soak'

# ---- Soak: id limit --------------------------------------------------------
Def 'SOAK-1' 'Soak' 'ID cap: 500 new+leave cycles; behaviour at id 500 and afterwards' {
  $errAt = -1
  for ($b = 0; $b -lt 104; $b++) {
    $cmds = ""
    for ($k = 0; $k -lt 5; $k++) { $id = $b * 5 + $k; $cmds += "new`nleave $id`n" }
    $script:sp.Write($cmds)
    $r = @(Read-Burst 500 20000)
    if ($errAt -lt 0 -and @($r -match 'car id limit reached').Count -gt 0) { $errAt = $b * 5 }
  }
  Ok ($errAt -eq 500) "id limit message first seen in batch starting at id $errAt (expected 500)"
  $r = @(Cmd "new")
  Has $r 'car id limit reached' 'after the cap `new` must fail clearly'
  Info 'After 500 cars the firmware cannot admit any more cars until reboot (carExited[] is never recycled).'
  $s = GetStatus
  Ok ($s.Free -eq 14) "lot should be empty again, free=$($s.Free)"
} 'soak'

# ================================================================ SIMULATOR-ONLY TESTS (kind 'sim')
# These move the simulated sensors (~entrance / ~spot / ~glitch), read the LED and call the web handlers,
# which a real board cannot be asked to do from a script.
function SimCmd([string]$ctl, [int]$quiet = 300, [int]$max = 4000) {
  $script:tr.Add("> $ctl"); $script:sp.Write($ctl + "`n"); return @(Read-Burst $quiet $max)
}
function Wait-Lines([int]$ms) { return @(Read-Burst 99999 $ms) }          # just collect everything the board prints for $ms
function Http-State {
  $r = @(SimCmd '~http state'); $l = @($r -match '^\[HTTP\] \{')
  if ($l.Count -eq 0) { return $null }
  return ($l[0].Substring(7) | ConvertFrom-Json)
}
function Led { $r = @(SimCmd '~led' 100 1000); return [int][regex]::Match(($r -join ' '), 'led=(\d)').Groups[1].Value }
function Led-Samples([int]$n = 12, [int]$gapMs = 100) { $v = @(); for ($i = 0; $i -lt $n; $i++) { $v += (Led); Start-Sleep -Milliseconds $gapMs }; return $v }

# Entrance sensor as measured on the model: car ~22 cm, empty road (the wall) ~30 cm; a car is counted below GATE_CAR_BELOW_CM (25 cm).
# A car needs 2 readings in a row (~1.1 s) and is then counted once until the road reads empty twice.
Def 'SIM-1' 'Sensors' "Entrance sensor (car ~22 cm, wall ~30 cm): one car per visit, a car that stays counts once, noise does not re-arm it, the $($script:gateCarBelow) cm threshold is exact" {
  $lo = ($script:gateCarBelow - 0.1).ToString('F1', $ci); $hi = ($script:gateCarBelow + 0.1).ToString('F1', $ci)
  $r = @(SimCmd '~entrance 22' 300 300) + (Wait-Lines 2200)
  Has $r '\[SENSOR\] entrance: car detected at 22\.0 cm' 'entrance event'
  Ok (@($r -match '\[ENTRY\]\s+car #0').Count -eq 1) 'exactly one car #0 for the first detection'
  HasNot $r 'exit lane' 'the default build has no exit lane'
  $r = Wait-Lines 2500
  HasNot $r '\[ENTRY\]' 'a car that stays in front of the sensor must not create more cars'
  [void](SimCmd '~entrance 0.5' 300 300); [void](Wait-Lines 2500)          # electrical noise (<2 cm) is ignored, it is not "empty"
  $r = @(SimCmd '~entrance 22' 300 300) + (Wait-Lines 2500)
  HasNot $r '\[ENTRY\]' 'noise while a car stands there must not let the same car be counted again'
  [void](SimCmd '~entrance 0' 300 300); [void](Wait-Lines 2000)            # no echo = open road = empty
  $r = @(SimCmd '~entrance 22' 300 300) + (Wait-Lines 2200)
  Has $r '\[ENTRY\]\s+car #1' 'after the road was empty a new car is counted'
  [void](SimCmd '~entrance 30' 300 300); [void](Wait-Lines 2000)
  $r = @(SimCmd "~entrance $hi" 300 300) + (Wait-Lines 2200)
  HasNot $r '\[(SENSOR|ENTRY)\]' "$hi cm counts as the empty road: no car"
  [void](SimCmd '~entrance 30' 300 300); [void](Wait-Lines 2000)
  $r = @(SimCmd "~entrance $lo" 300 300) + (Wait-Lines 2200)
  Has $r '\[ENTRY\]\s+car #2' "$lo cm is a car: car #2"
}
Def 'SIM-9' 'Sensors' 'Optional two-lane entrance sensor (exit ~22 cm / entry ~26 cm): exit removes the car at spot 0, then the lowest parked id; empty lot only warns; band edge 24.0 cm' {
  Use-Build $script:simExit
  $r = @(SimCmd '~entrance 22' 300 300) + (Wait-Lines 2200)
  Has $r '\[SENSOR\] exit lane: car detected at 22\.0 cm' 'exit-lane event'
  Has $r '\[WARN\]\s+exit lane saw a car but no car is in the lot' 'nothing to remove -> warning only'
  HasNot $r '\[LEAVE\]' 'nobody to leave'
  [void](SimCmd '~entrance 30' 300 300); [void](Wait-Lines 2000)
  $a = NewCar; $b = NewCar; $c = NewCar                                      # 0 -> spot 0, 1 -> spot 1, 2 -> spot 2
  [void](Cmd "park 2 $($c.Id)"); [void](Cmd "park 1 $($b.Id)"); [void](Cmd "park 0 $($a.Id)")
  $r = @(SimCmd '~entrance 24.1' 300 300) + (Wait-Lines 2200)
  Has $r '\[ENTRY\]\s+car #3' '24.1 cm is the entry lane (not exit): a new car'
  HasNot $r '\[LEAVE\]' '24.1 cm must not remove anybody'
  [void](SimCmd '~entrance 30' 300 300); [void](Wait-Lines 2000)
  $r = @(SimCmd '~entrance 23.9' 300 300) + (Wait-Lines 2200)
  Has $r '\[SENSOR\] exit lane: car detected at 23\.9 cm' '23.9 cm is the exit lane'
  Has $r 'removing car #0' 'the car parked at spot 0 is the one that leaves'
  Has $r '\[LEAVE\]\s+car #0\s+left spot 0' 'car 0 left'
  $r = Wait-Lines 2500
  HasNot $r '\[LEAVE\]' 'a car that stays in the exit lane is removed only once'
  [void](SimCmd '~entrance 30' 300 300); [void](Wait-Lines 2000)
  $r = @(SimCmd '~entrance 22' 300 300) + (Wait-Lines 2200)
  Has $r '\[LEAVE\]\s+car #1\s+left spot 1' 'next exit removes the lowest parked id (car 1)'
  $j = Http-State
  Ok ($j -ne $null -and @($j.sp | Where-Object { $_.s -eq 2 }).Count -eq 1) 'only car 2 is still parked'
}
Def 'SIM-10' 'Sensors' 'Optional exit lane: a car leaves spot 0 and then drives through the exit lane - counted once (no second car removed)' {
  Use-Build $script:simExit
  $a = NewCar; $b = NewCar
  [void](Cmd "park 1 $($b.Id)")
  [void](SimCmd '~spot 23' 300 300); $r = Wait-Lines 3500
  Has $r '\[PARK\]\s+car #0\s+@ spot 0' 'car 0 parks at spot 0 by the sensor'
  [void](SimCmd '~spot 25.8' 300 300); $r = Wait-Lines 3500
  Has $r '\[LEAVE\]\s+car #0\s+left spot 0 \(sensor\)' 'spot sensor removes car 0'
  $r = @(SimCmd '~entrance 22' 300 300) + (Wait-Lines 2200)
  Has $r 'already removed' 'the exit lane recognises the car that just left spot 0'
  HasNot $r '\[LEAVE\]' 'car 1 must not be removed for the same departure'
  $j = Http-State
  Ok (@($j.sp | Where-Object { $_.s -eq 2 -and $_.car -eq 1 }).Count -eq 1) 'car 1 is still parked'
}
Def 'SIM-2' 'Sensors' 'Spot 0 sensor: reserved car parks CORRECT after ~2 s of confirmation, LED steady; leaving frees the spot' {
  $c = NewCar
  Ok ($c.Spot -eq 0) 'first car is sent to the physical spot'
  Ok ((Led) -eq 0) 'LED off while the spot is only reserved'
  $r = @(SimCmd '~spot 23' 300 300) + (Wait-Lines 900)
  HasNot $r '\[(SENSOR|PARK)\]' 'no event before the 4-sample confirmation (~2 s)'
  $r = Wait-Lines 2500
  Has $r '\[SENSOR\] spot 0: object detected' 'spot sensor event'
  Has $r '\[PARK\]\s+car #0\s+@ spot 0 .*CORRECT' 'reserved car is the one that parked'
  $led = Led-Samples 8 80
  Ok (@($led | Where-Object { $_ -ne 1 }).Count -eq 0) "LED must be steady on for a correct car, saw: $($led -join '')"
  $r = @(SimCmd '~spot 25.8' 300 300) + (Wait-Lines 3200)
  Has $r '\[SENSOR\] spot 0: clear' 'clear event'
  Has $r '\[LEAVE\]\s+car #0\s+left spot 0 \(sensor\)' 'car leaves the lot when the sensor clears'
  Ok ((Led) -eq 0) 'LED off after the car left'
  $s = GetStatus
  Ok ($s.Free -eq 14) "lot should be empty, free=$($s.Free)"
}
Def 'SIM-3' 'Sensors' 'Wrong car at spot 0: newest unparked car parks WRONG, its own reservation is released, LED blinks' {
  $a = NewCar; $b = NewCar                                  # a->0, b->1
  [void](Cmd "park 7 $($a.Id)")                             # a leaves spot 0 unreserved (WRONG at 7)
  [void](SimCmd '~spot 23' 300 300)
  $r = Wait-Lines 3200
  Has $r "\[PARK\]\s+car #$($b.Id)\s+@ spot 0 .*WRONG SPOT!\s+\(assigned spot 1 released\)" 'car b parks at the physical spot, wrong'
  $led = Led-Samples 16 80
  Ok (@($led | Where-Object { $_ -eq 1 }).Count -gt 0 -and @($led | Where-Object { $_ -eq 0 }).Count -gt 0) "LED must blink for a wrong car, saw: $($led -join '')"
  $s = GetStatus
  Ok ($s.Spots[0].State -eq 'OCCUPIED' -and $s.Spots[0].Car -eq $b.Id -and $s.Spots[0].Correct -eq $false) 'status shows spot 0 occupied WRONG'
  Ok ($s.Spots[1].State -eq 'free' -or $s.Spots[1].State -eq 'OCCUPIED') 'b''s old reservation (spot 1) is gone'
}
Def 'SIM-4' 'Sensors' 'Spot 0 BLOCKED: object with no car on its way is never assigned; clears when the object leaves' {
  $r = @(SimCmd '~spot 23' 300 300) + (Wait-Lines 3200)
  Has $r '\[WARN\]\s+no car is on its way - spot 0 is BLOCKED' 'blocked warning'
  HasNot $r '\[PARK\]' 'nobody may be parked by a stray object'
  $s = GetStatus
  Ok ($s.Blocked -eq 1 -and $s.Spots[0].State -eq 'BLOCKED') 'status must show spot 0 BLOCKED'
  Has $s.Lines '\[NEXT\]\s+next car -> spot 1 ' 'next recommendation skips the blocked spot'
  $c = NewCar
  Ok ($c.Spot -eq 1) "new car must not be sent to the blocked spot (got $($c.Spot))"
  $r = @(SimCmd '~spot 25.8' 300 300) + (Wait-Lines 3200)
  Has $r '\[SENSOR\] spot 0: clear' 'object removed'
  Has $r '\[NEXT\]\s+next car -> spot 0 ' 'spot 0 is the nearest again'
  HasNot $r '\[LEAVE\]' 'nobody was parked there, nobody leaves'
}
Def 'SIM-5' 'Sensors' "Hysteresis (measured: empty 25-26 cm; car 2.5 cm high 22.5-23.5 cm, 1.5 cm high 23.5-24.5 cm): arrive below $($script:arrive), leave above $($script:leave); the dead zone between changes nothing" {
  $mid = (($script:arrive + $script:leave) / 2).ToString('F2', $ci)
  foreach ($empty in '25.0', '26.0') {                                     # the measured empty range never arrives
    [void](SimCmd "~spot $empty" 300 300); $r = Wait-Lines 3200
    HasNot $r '\[SENSOR\]' "empty spot reading $empty cm must not count as a car"
  }
  [void](SimCmd "~spot $mid" 300 300); $r = Wait-Lines 3200
  HasNot $r '\[SENSOR\]' "empty spot: $mid cm is in the dead zone, above the arrive threshold"
  foreach ($car in '24.5', '23.5') {                                       # farthest reading of a 1.5 cm and of a 2.5 cm high car
    [void](SimCmd "~spot $car" 300 300); $r = Wait-Lines 3200
    Has $r '\[SENSOR\] spot 0: object detected' "a car reading $car cm arrives"
    [void](SimCmd "~spot $mid" 300 300); $r = Wait-Lines 3200
    HasNot $r '\[SENSOR\]|\[LEAVE\]' "occupied spot: $mid cm is inside the dead zone, must stay occupied"
    [void](SimCmd '~spot 25.0' 300 300); $r = Wait-Lines 3200
    Has $r '\[SENSOR\] spot 0: clear' 'the nearest measured empty reading (25.0 cm) leaves'
  }
}
Def 'SIM-6' 'Sensors' 'Noise immunity: bursts of <2 cm / no-echo readings make no phantom events, and a real car still parks once and never "leaves"' {
  [void](SimCmd '~glitch 40' 300 300)
  $r = Wait-Lines 15000
  HasNot $r '\[(SENSOR|ENTRY|PARK|LEAVE|WARN)\]' 'phantom event from sensor noise on an empty spot'
  [void](SimCmd '~glitch 20' 300 300)       # 4 clean readings in a row are needed to confirm, so keep the noise survivable
  $c = NewCar
  [void](SimCmd '~spot 23' 300 300)
  $r = Wait-Lines 25000
  Ok (@($r -match '\[PARK\]\s+car #0').Count -eq 1) "the real car must park exactly once, got $(@($r -match '\[PARK\]').Count) PARK events"
  HasNot $r '\[LEAVE\]' 'noise must not make a present car leave'
}
Def 'SIM-7' 'Sensors' 'Sensor event while a car is only reserved elsewhere is handled once (no double park from the physical spot)' {
  $a = NewCar; [void](Cmd "park 0 $($a.Id)")               # serial already put car 0 on spot 0
  [void](SimCmd '~spot 23' 300 300)
  $r = Wait-Lines 3200
  Has $r '\[SENSOR\] spot 0: object detected' 'sensor still reports the object'
  HasNot $r '\[PARK\]' 'spot 0 is already occupied in the books: the sensor must not park a second car'
  $s = GetStatus
  Ok ($s.Occ -eq 1) "exactly one car parked, got $($s.Occ)"
}

Def 'SIM-8' 'Sensors' 'Single false readings (a believable 15 cm on an empty spot / 40 cm on a parked one) are not events: 4 in a row are needed' {
  [void](SimCmd '~spike 10 15' 300 300)
  $r = Wait-Lines 15000
  HasNot $r '\[(SENSOR|PARK|LEAVE|WARN)\]' 'an isolated 15 cm reading on an empty spot must not create a car / block the spot'
  [void](SimCmd '~spike 0' 300 300)
  $c = NewCar
  [void](SimCmd '~spot 23' 300 300); $r = Wait-Lines 3500
  Has $r '\[PARK\]\s+car #0' 'the real car parks'
  [void](SimCmd '~spike 10 40' 300 300)
  $r = Wait-Lines 15000
  HasNot $r '\[(SENSOR|LEAVE)\]' 'isolated "clear" readings must not make a parked car leave'
}

# ---- v9: the data behind the picture (web payload / text picture / summary) ----
Def 'VIS-1' 'Visual' 'Boot payload (/state): shape, map, 14 free spots, next = spot 0, nothing waiting' {
  $j = Http-State
  Ok ($j -ne $null) 'no JSON came back from /state'
  if ($j -eq $null) { return }
  Ok ($j.floors -eq 2 -and $j.per -eq 7) "floors/per floor: $($j.floors)/$($j.per)"
  Ok ($j.unit -eq $script:unit) "unit: $($j.unit)"
  Ok ($j.geo.e0 -eq $mE0 -and $j.geo.er -eq $mER -and $j.geo.ramp -eq $mRamp -and $j.geo.f1 -eq $mF1 -and $j.geo.sw -eq $mSW -and $j.geo.sl -eq $mSL) "geo must carry the measured lengths: $($j.geo | ConvertTo-Json -Compress)"
  Ok ($j.geo.car.Count -eq 3 -and [double]$j.geo.car[0] -eq 4 -and [double]$j.geo.car[1] -eq 2 -and [double]$j.geo.car[2] -eq 2.5) "car dimensions $($j.geo.car -join 'x') (expected 4 x 2 x 2.5)"
  for ($i = 0; $i -lt 14; $i++) { Ok ($j.sp[$i].f -eq [int]($i -ge 7) -and $j.sp[$i].k -eq ($i % 7)) "spot $i floor/position: $($j.sp[$i].f)/$($j.sp[$i].k)" }
  Ok ($j.sp.Count -eq 14) "expected 14 spots, got $($j.sp.Count)"
  Ok (@($j.sp | Where-Object { $_.s -ne 0 }).Count -eq 0) 'all spots free'
  Ok ($j.next -eq 0 -and $j.real -eq 0) "next=$($j.next) real=$($j.real)"
  Ok ([math]::Abs($j.nextd - $script:wantDist[0]) -lt 0.05) "nextd=$($j.nextd) want $($script:wantDist[0])"
  Ok ($j.waitN -eq 0 -and $j.wait.Count -eq 0 -and $j.seq -eq 0 -and $j.ttl -eq 60) "waitN=$($j.waitN) seq=$($j.seq) ttl=$($j.ttl)"
  for ($i = 0; $i -lt 14; $i++) { Ok ([math]::Abs($j.sp[$i].d - $script:wantDist[$i]) -lt 0.05) "spot $i distance $($j.sp[$i].d) vs $($script:wantDist[$i])" }
}
Def 'VIS-2' 'Visual' 'Payload and text picture agree: states, car ids, OK/WRONG, waiting list, next spot' {
  $c = @(); for ($i = 0; $i -lt 5; $i++) { $c += (NewCar) }       # cars 0..4 -> spots 0..4
  [void](Cmd "park 0 0"); [void](Cmd "park 9 1"); [void](Cmd "park 3 2")   # 0 correct, 1 wrong at 9, 2 wrong at 3 (displaces car 3)
  [void](Cmd "leave 4")
  $s = GetStatus; $j = Http-State
  if ($j -eq $null) { Ok $false 'no JSON'; return }
  for ($i = 0; $i -lt 14; $i++) {
    $p = $j.sp[$i]; $w = $s.Spots[$i]
    $js = @('free', 'RESERVED', 'OCCUPIED', 'BLOCKED')[$p.s]
    Ok ($js -eq $w.State) "spot ${i}: json says $js, picture says $($w.State)"
    if ($p.s -eq 1 -or $p.s -eq 2) { Ok ($p.car -eq $w.Car) "spot ${i}: json car $($p.car) vs picture car $($w.Car)" }
    if ($p.s -eq 2) { Ok (($p.ok -eq 1) -eq $w.Correct) "spot ${i}: json ok=$($p.ok) vs picture correct=$($w.Correct)" }
  }
  Ok ($j.waitN -eq $s.Waiting.Count) "waiting count: json $($j.waitN) vs picture $($s.Waiting.Count)"
  foreach ($w in $s.Waiting) {
    $m = @($j.wait | Where-Object { $_.car -eq $w.Car })
    Ok ($m.Count -eq 1 -and $m[0].spot -eq $w.Spot) "waiting car $($w.Car): json spot $(if($m.Count){$m[0].spot}else{'missing'}) vs picture $($w.Spot)"
  }
  $waitIds = @($j.wait | ForEach-Object { $_.car } | Sort-Object)
  Ok (($waitIds -join ',') -eq '3') "only car 3 (displaced, rerouted) is still waiting, got: $($waitIds -join ',')"
  Ok ($s.Next -match "spot $($j.next) ") "[NEXT] line vs json.next=$($j.next): $($s.Next)"
}
Def 'VIS-3' 'Visual' 'The status counter (seq): +1 per valid status, unchanged by other commands and by a rejected "status x"' {
  Ok ((Http-State).seq -eq 0) 'seq starts at 0'
  [void](Cmd 'status'); [void](Cmd 'status'); [void](Cmd 'STATUS')
  Ok ((Http-State).seq -eq 3) "three status commands -> seq 3, got $((Http-State).seq)"
  [void](NewCar); [void](Cmd 'status foo'); [void](Cmd 'bogus'); [void](Cmd 'check 1'); [void](Wait-Lines 1500)
  Ok ((Http-State).seq -eq 3) "other commands must not bump seq, got $((Http-State).seq)"
}
Def 'VIS-4' 'Visual' 'Waiting list follows the car: appears on new, disappears on park / leave / cancel, keeps arrival order' {
  $a = NewCar; $b = NewCar; $c = NewCar
  $w = @((Http-State).wait | ForEach-Object { $_.car })
  Ok (($w -join ',') -eq '0,1,2') "waiting after 3 new: $($w -join ',')"
  [void](Cmd "park 0 0"); $w = @((Http-State).wait | ForEach-Object { $_.car })
  Ok (($w -join ',') -eq '1,2') "after car 0 parked: $($w -join ',')"
  [void](Cmd "leave 1"); $w = @((Http-State).wait | ForEach-Object { $_.car })
  Ok (($w -join ',') -eq '2') "after car 1 left: $($w -join ',')"
  [void](Cmd "cancel 2"); $j = Http-State
  Ok ($j.waitN -eq 0 -and $j.wait.Count -eq 0) "after car 2 cancelled: waitN=$($j.waitN)"
  Ok (@($j.sp | Where-Object { $_.s -eq 2 }).Count -eq 1) 'car 0 is still parked'
}
Def 'VIS-5' 'Visual' 'Shipped build (WiFi page up): status prints only [STATUS] + [VISUAL] + [NEXT]; no picture on events; WiFi-off build falls back to the text picture' {
  Use-Build $script:simWeb
  Has $script:boot '\[WEB\]\s+picture page: join WiFi "SmartParking" then open http://192\.168\.4\.1' 'WiFi hint at boot'
  HasNot $script:boot '^\+-{17}\+' 'the web build must not draw the text picture at boot'
  $r = @(Cmd 'new'); HasNot $r '^\+-+' 'no picture after an event when the page is running'; Has $r '\[NEXT\]' 'NEXT line'
  $r = @(Cmd 'status')
  Has $r '^\[STATUS\] 0 occupied, 1 reserved, 13 free' 'summary'
  Has $r '^\[VISUAL\] picture refreshed: http://192\.168\.4\.1$' '[VISUAL] line'
  HasNot $r '^\+-+|FLOOR \d - |WAITING:' 'no text picture on status when the page is running'
  Ok ($r.Count -eq 3) "status must print exactly 3 lines, got $($r.Count): $($r -join ' | ')"
  $root = @(SimCmd '~http root'); Has $root 'root code=200 type=text/html bytes=\d{4,}' 'GET / serves the page'
  Use-Build $script:simNoWeb
  HasNot $script:boot '\[WEB\]' 'WiFi-off build must not mention WiFi'
  Has $script:boot '^\+-{17}\+' 'WiFi-off build draws the text picture at boot'
  $r = @(Cmd 'status'); HasNot $r '\[VISUAL\]' 'no [VISUAL] line without the page'; Has $r 'WAITING: none' 'text picture has the waiting line'
  Has @(SimCmd '~http state') 'n/a' 'no web handlers in the WiFi-off build'
}
Def 'VIS-6' 'Visual' 'Page file sanity: served page is complete, fetches /state, one script block, balanced tags, no external resources' {
  $page = Get-Content (Join-Path (Split-Path $Sketch) 'parking_page.h') -Raw -Encoding UTF8
  $html = $page.Substring($page.IndexOf('R"PAGE(') + 7)        # skip the explanatory comment above the string
  Has @($html) 'fetch\(''/state''' 'page polls /state'
  Ok ([regex]::Matches($html, '<script>').Count -eq 1 -and [regex]::Matches($html, '</script>').Count -eq 1) 'exactly one script block'
  Ok ([regex]::Matches($html, '(?i)<html').Count -eq 1 -and [regex]::Matches($html, '(?i)</html>').Count -eq 1 -and [regex]::Matches($html, '(?i)</body>').Count -eq 1) 'html/body closed once'
  HasNot @($html) '(?i)(src|href)\s*=\s*["'']?https?://|url\(\s*["'']?https?:|@import|//cdn|cdnjs|googleapis' 'external resource: the board''s WiFi has no internet'
  Ok ($page.Contains(')PAGE";') -and $page.Contains('R"PAGE(')) 'raw-string delimiters intact'
  Ok ([regex]::Matches($page, '\)PAGE"').Count -eq 1) 'the raw-string terminator must not appear inside the page'
  $r = @(SimCmd '~http root'); $m = [regex]::Match(($r -join ' '), 'bytes=(\d+)')
  Ok ($m.Success -and [int]$m.Groups[1].Value -gt 8000 -and [int]$m.Groups[1].Value -lt 60000) "page size $($m.Groups[1].Value) bytes (expected 8k-60k)"
}

# ================================================================ RUNNER
Open-Port
$run = @($script:tests | Where-Object {
    ($Only -eq "" -or $_.Id.StartsWith($Only)) -and
    ($_.Kind -ne 'slow' -or $Slow) -and ($_.Kind -ne 'soak' -or $Soak) -and ($_.Kind -ne 'sim' -or $Sim) })
$target = if ($Sim) { "the host simulator" } else { $Port }
Write-Output ("Running {0} tests on {1}" -f $run.Count, $target)
foreach ($t in $run) {
  $script:fails = New-Object System.Collections.Generic.List[string]
  $script:infos = New-Object System.Collections.Generic.List[string]
  $start = $script:tr.Count
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $status = 'PASS'
  try { if ($Sim) { $script:sp.Exe = $script:simText }; Reset-Board; $script:tr.Add("=== $($t.Id) $($t.Title)"); $start = $script:tr.Count; & $t.Body | Out-Null }
  catch { $script:fails.Add("EXCEPTION: $($_.Exception.Message) @ line $($_.InvocationInfo.ScriptLineNumber)"); $status = 'ERROR' }
  if ($script:fails.Count -gt 0 -and $status -ne 'ERROR') { $status = 'FAIL' }
  $slice = if ($status -ne 'PASS') { ($script:tr.GetRange($start, $script:tr.Count - $start) -join "`n") } else { '' }
  $script:results += [pscustomobject]@{ Id = $t.Id; Group = $t.Group; Title = $t.Title; Status = $status; Fails = @($script:fails); Infos = @($script:infos); Secs = [math]::Round($sw.Elapsed.TotalSeconds, 1); Log = $slice }
  Write-Output ("{0,-7} {1,-5} {2}  ({3}s)" -f $t.Id, $status, $t.Title, [math]::Round($sw.Elapsed.TotalSeconds, 1))
  foreach ($f in $script:fails) { Write-Output ("          ! " + $f) }
  foreach ($i in $script:infos) { Write-Output ("          i " + $i) }
}
if ($script:sp -and $script:sp.IsOpen) { $script:sp.Close() }

# ---- report -----------------------------------------------------------------
$p = @($script:results | Where-Object Status -eq 'PASS').Count
$f = @($script:results | Where-Object Status -eq 'FAIL').Count
$e = @($script:results | Where-Object Status -eq 'ERROR').Count
$md = New-Object System.Collections.Generic.List[string]
$md.Add("# Test report - $(Split-Path $Sketch -Leaf)"); $md.Add(""); $md.Add("Run: $(Get-Date -Format 'yyyy-MM-dd HH:mm')  Target: $target"); $md.Add("")
$md.Add("**$p passed, $f failed, $e errors** of $($script:results.Count)"); $md.Add("")
$md.Add("| ID | Group | Result | Title |"); $md.Add("|---|---|---|---|")
foreach ($r in $script:results) { $md.Add("| $($r.Id) | $($r.Group) | $($r.Status) | $($r.Title) |") }
foreach ($r in ($script:results | Where-Object { $_.Status -ne 'PASS' -or $_.Infos.Count -gt 0 })) {
  $md.Add(""); $md.Add("## $($r.Id) - $($r.Status) - $($r.Title)")
  foreach ($x in $r.Fails) { $md.Add("- FAIL: $x") }
  foreach ($x in $r.Infos) { $md.Add("- note: $x") }
  if ($r.Log) { $md.Add(""); $md.Add('```'); $md.Add($r.Log); $md.Add('```') }
}
Set-Content -Path (Join-Path $here 'report.md') -Value $md -Encoding UTF8
Write-Output ""
Write-Output "SUMMARY: $p passed, $f failed, $e errors   (report: tests\report.md)"
