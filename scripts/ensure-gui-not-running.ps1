$ErrorActionPreference = "SilentlyContinue"
$names = @("manual_map_gui", "manual_map")

foreach ($name in $names) {
    Get-Process -Name $name | Stop-Process -Force
}

Start-Sleep -Milliseconds 300

$running = Get-Process -Name "manual_map_gui" -ErrorAction SilentlyContinue

if ($running) {
    Write-Host ""
    Write-Host "BUILD BLOCKED: manual_map_gui.exe is still running (PID $($running.Id))."
    Write-Host "Close the Manual Map Injector window, or end the process in Task Manager."
    Write-Host "If you used Run as Admin, open Task Manager as Administrator to end it."
    Write-Host ""
    exit 1
}

exit 0
