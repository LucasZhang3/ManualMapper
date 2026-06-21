$src = 'C:\Users\Lucas\.cursor\projects\c-Users-Lucas-Downloads-manual-map-unknowncheats-me-manual-map\assets\c__Users_Lucas_AppData_Roaming_Cursor_User_workspaceStorage_empty-window_images_image-95b4283d-64b2-41a7-bf36-7170c670b412.png'
$dstDir = Join-Path $PSScriptRoot '..\manual_map\src\gui\assets'
$dst = Join-Path $dstDir 'app_icon.png'

if ( -not ( Test-Path -LiteralPath $dstDir ) )
{
    New-Item -ItemType Directory -Path $dstDir -Force | Out-Null
}

Copy-Item -LiteralPath $src -Destination $dst -Force
Write-Host "Copied to $dst"
