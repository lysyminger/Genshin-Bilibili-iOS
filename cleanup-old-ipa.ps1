# Permanently remove only the v0.1-v0.11 IPA builds/reports, then empty Recycle Bin.
$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
$outputRoot = [IO.Path]::GetFullPath((Join-Path $taskRoot 'Genshin-Bilibili-iOS\output'))
$outputItem = Get-Item -LiteralPath $outputRoot
if (($outputItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw 'The output directory must not be a reparse point.'
}
$buildNames = @(
    'Yuanshen_7.1.0_1241_diagnostic_v0.1'
    'Yuanshen_7.1.0_1241_diagnostic_v0.2'
    'Yuanshen_7.1.0_1241_diagnostic_v0.3'
    'Yuanshen_7.1.0_1241_diagnostic_v0.4'
    'Yuanshen_7.1.0_1241_diagnostic_v0.5'
    'Yuanshen_7.1.0_1241_diagnostic_v0.6'
    'Yuanshen_7.1.0_1241_bridge_v0.7'
    'Yuanshen_7.1.0_1241_bili_component_v0.8'
    'Yuanshen_7.1.0_1241_bili_login_v0.9'
    'Yuanshen_7.1.0_1241_bili_login_v0.10'
    'Yuanshen_7.1.0_1241_bili_login_v0.11'
)
$targets = @(foreach ($name in $buildNames) {
    foreach ($extension in @('.ipa', '.report.json')) {
        $targetPath = [IO.Path]::GetFullPath((Join-Path $outputRoot ($name + $extension)))
        if (-not [string]::Equals([IO.Path]::GetDirectoryName($targetPath), $outputRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'A target resolved outside the output directory.'
        }
        if (Test-Path -LiteralPath $targetPath) {
            $item = Get-Item -LiteralPath $targetPath
            if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw 'A target is not a regular file.'
            }
            $item
        }
    }
})
$bytes = ($targets | Measure-Object -Property Length -Sum).Sum
$targets | Select-Object Name, Length | Format-Table -AutoSize
foreach ($item in $targets) {
    Remove-Item -LiteralPath $item.FullName -Force
    if (Test-Path -LiteralPath $item.FullName) { throw ('File still exists: ' + $item.Name) }
}
$trashShell = New-Object -ComObject Shell.Application
if ($trashShell.Namespace(10).Items().Count -gt 0) {
    Clear-RecycleBin -Force -ErrorAction Stop
}
if ($trashShell.Namespace(10).Items().Count -ne 0) { throw 'Recycle Bin is not empty.' }
Write-Host ('Deleted {0} files, {1:N2} GB. Recycle Bin is empty.' -f $targets.Count, ($bytes / 1e9))
