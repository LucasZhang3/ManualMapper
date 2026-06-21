Add-Type -AssemblyName System.Drawing

$pngPath = Join-Path $PSScriptRoot '..\manual_map\src\gui\assets\app_icon.png'
$icoPath = Join-Path $PSScriptRoot '..\manual_map\src\gui\assets\app_icon.ico'

if ( -not ( Test-Path -LiteralPath $pngPath ) )
{
    Write-Error "PNG not found: $pngPath"
    exit 1
}

$src = [System.Drawing.Bitmap]::FromFile( (Resolve-Path -LiteralPath $pngPath).Path )
$sizes = @( 16, 32, 48, 256 )
$icon = $null

foreach ( $size in $sizes )
{
    $bmp = New-Object System.Drawing.Bitmap $size, $size
    $g = [System.Drawing.Graphics]::FromImage( $bmp )
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.DrawImage( $src, 0, 0, $size, $size )
    $g.Dispose( )

    $hIcon = $bmp.GetHicon( )
    $tmp = [System.Drawing.Icon]::FromHandle( $hIcon )

    if ( $null -eq $icon )
    {
        $icon = $tmp
    }
    else
    {
        $icon = New-Object System.Drawing.Icon( $icon, $size, $size )
    }

    $bmp.Dispose( )
}

$fs = [System.IO.File]::Create( $icoPath )
$icon.Save( $fs )
$fs.Close( )
$src.Dispose( )
$icon.Dispose( )

Write-Host "Created $icoPath"
