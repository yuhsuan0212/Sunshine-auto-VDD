#Requires -RunAsAdministrator
<#
.SYNOPSIS
Registers console-session recovery for abandoned managed-vdd transactions.
.DESCRIPTION
Run after deploying the matching helper while no stream/checkpoint is active.
The task runs at logon and once per minute while the chosen user is logged on.
The helper independently checks the console session, process identity, and service.
This does not promise display recovery before the user logs on.
#>
[CmdletBinding()]
param(
    [string]$SunshineDirectory = "$env:ProgramFiles\Sunshine",
    [Parameter(Mandatory)][string]$OwnerFile,
    [string]$ServiceName = 'SunshineService',
    [string]$TaskName = 'Sunshine Managed VDD Recovery',
    [switch]$ValidateOnly
)
$ErrorActionPreference = 'Stop'
$installRoot = (Resolve-Path -LiteralPath $SunshineDirectory).Path
$programRoot = [IO.Path]::GetFullPath($env:ProgramFiles).TrimEnd('\') + '\'
if (-not $installRoot.StartsWith($programRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The elevated recovery task must use a helper installed under Program Files.'
}
$helper = Join-Path $installRoot 'tools\managed-vdd.exe'
$state = Join-Path $installRoot 'config\display_device.state'
$owner = (Resolve-Path -LiteralPath $OwnerFile).Path
if (@($helper, $owner, $ServiceName, $state) | Where-Object { $_.Contains('"') }) {
    throw 'Recovery arguments must not contain quotation marks.'
}
if (-not (Test-Path -LiteralPath $helper -PathType Leaf)) { throw 'Install the updated managed-vdd.exe first.' }
$service = Get-CimInstance Win32_Service | Where-Object Name -eq $ServiceName
$wrapper = Join-Path $installRoot 'tools\sunshinesvc.exe'
if (-not $service -or $service.PathName.Trim('"') -ne $wrapper) { throw 'Sunshine service executable does not match the installation.' }
$inspection = & $helper inspect $owner
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect the owned VDD.' }
$device = ($inspection -join "`n") | ConvertFrom-Json
if ($device.checkpoint -or -not $device.disabled) {
    throw 'End the stream and recover any existing checkpoint before registering recovery. Old journals lack process ownership metadata.'
}
$user = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$runner = Join-Path $installRoot 'tools\managed-vdd-recover.ps1'
$arguments = '-NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File "{0}" -OwnerFile "{1}" -ServiceName "{2}" -StateFile "{3}"' -f $runner, $owner, $ServiceName, $state
$powershell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$action = New-ScheduledTaskAction -Execute $powershell -Argument $arguments -WorkingDirectory $installRoot
$triggers = @(
    (New-ScheduledTaskTrigger -AtLogOn -User $user),
    (New-ScheduledTaskTrigger -Once -At (Get-Date).AddMinutes(1) -RepetitionInterval (New-TimeSpan -Minutes 1))
)
$principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -MultipleInstances IgnoreNew -ExecutionTimeLimit (New-TimeSpan -Minutes 2) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable
$task = New-ScheduledTask -Action $action -Trigger $triggers -Principal $principal -Settings $settings
if ($ValidateOnly) {
    $task | ConvertTo-Json -Depth 8
    return
}
if (Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue) {
    throw "Task '$TaskName' already exists. Inspect/remove it before replacing it."
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'recover.ps1') -Destination $runner -Force
Register-ScheduledTask -TaskName $TaskName -InputObject $task | Out-Null
Write-Output "Registered '$TaskName' for $user. Recovery runs on console logon and every minute; no reboot was requested."
