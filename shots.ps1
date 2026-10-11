# Renders test frames at the given times (seconds of flight) into shots\ as PNGs.
param([double[]]$Times = @(0.5, 1.9, 10))
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force shots | Out-Null
Remove-Item shots\* -ErrorAction SilentlyContinue
foreach ($t in $Times) { Start-Process -Wait .\Starfield.exe -ArgumentList "/x shots\t$t $t" }
Add-Type -AssemblyName System.Drawing
Get-ChildItem shots\*.bmp | ForEach-Object {
    $b = [System.Drawing.Bitmap]::FromFile($_.FullName)
    $b.Save(($_.FullName -replace '\.bmp$', '.png'), [System.Drawing.Imaging.ImageFormat]::Png)
    $b.Dispose()
}
