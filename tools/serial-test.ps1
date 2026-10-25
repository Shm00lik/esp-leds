# Sends leds/cmd JSON to a board running the SERIAL_TEST_MODE build.
# usage: serial-test.ps1 <commands file>  (each line: <seconds-to-hold> <json>)
$p = New-Object System.IO.Ports.SerialPort COM6,115200
$p.DtrEnable = $false; $p.RtsEnable = $false; $p.NewLine = "`n"
$p.Open()
Start-Sleep -Milliseconds 300
[Console]::Write($p.ReadExisting())
foreach ($l in Get-Content $args[0]) {
  if (-not $l.Trim()) { continue }
  $sec, $json = $l -split ' ', 2
  Write-Host ">>> $json"
  $p.WriteLine($json)
  $end = (Get-Date).AddSeconds([double]$sec)
  while ((Get-Date) -lt $end) { $s = $p.ReadExisting(); if ($s) { [Console]::Write($s) }; Start-Sleep -Milliseconds 100 }
}
$p.Close()
