# Wgrywanie skompilowanego firmware na KinCony B8M (ESP32-S3).
# Uzycie:
#   .\upload.ps1                         -> projekt 01_Test_v_0_01, port wykryty automatycznie (USB VID 303A)
#   .\upload.ps1 -Port COM5
#   .\upload.ps1 -Sketch 02_Inny_projekt -Port COM9
#   .\upload.ps1 -Sketch .\02_Inny\02_Inny.ino      (dowolny szkic z folderu projektu, rowniez w podfolderach)
#   .\upload.ps1 -Compile                -> najpierw kompilacja, potem wgranie
param(
  [string]$Sketch = "01_Test_v_0_01",
  [string]$Port = "",
  [switch]$Compile
)
$ErrorActionPreference = "Stop"
$cli  = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
$fqbn = "esp32:esp32:esp32s3:CDCOnBoot=cdc,USBMode=hwcdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB"
# -Sketch: nazwa folderu, sciezka do folderu albo do pliku .ino (wzgledna do projektu lub bezwzgledna)
$cand = if ([System.IO.Path]::IsPathRooted($Sketch)) { $Sketch } else { Join-Path $PSScriptRoot $Sketch }
if (-not (Test-Path $cand)) { throw "Nie ma: $cand" }
$item = Get-Item $cand
$sk   = if ($item.PSIsContainer) { $item.FullName } else { $item.DirectoryName }
$root = (Get-Item $PSScriptRoot).FullName
if (-not $sk.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) { throw "Szkic musi byc w folderze projektu: $root" }
$Sketch = Split-Path $sk -Leaf
if (-not (Test-Path (Join-Path $sk "$Sketch.ino"))) { throw "W folderze $sk brak pliku $Sketch.ino (Arduino wymaga nazwy jak folder)" }
$bld  = Join-Path $sk "build"

if (-not $Port) {
  $found = Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPDeviceID -like 'USB\VID_303A*' -and $_.Name -match '\((COM\d+)\)' } |
    ForEach-Object { [regex]::Match($_.Name, 'COM\d+').Value }
  $found = @($found)
  if ($found.Count -eq 0) { throw "Nie znaleziono plytki ESP32-S3 (USB VID 303A). Podaj port: -Port COMx" }
  if ($found.Count -gt 1) { throw "Znaleziono kilka plytek: $($found -join ', '). Podaj port: -Port COMx" }
  $Port = $found[0]
}
Write-Host "Projekt: $Sketch   Port: $Port"

if ($Compile -or -not (Test-Path (Join-Path $bld "$Sketch.ino.bin"))) {
  & $cli compile --fqbn $fqbn --output-dir $bld $sk
  if ($LASTEXITCODE -ne 0) { throw "Kompilacja nieudana" }
}
& $cli upload -p $Port --fqbn $fqbn --input-dir $bld $sk
if ($LASTEXITCODE -ne 0) { throw "Wgrywanie nieudane (sprobuj: przytrzymaj Download i nacisnij Reset)" }
Write-Host "Wgrano poprawnie na $Port"
