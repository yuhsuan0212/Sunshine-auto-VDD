<#
.SYNOPSIS
Hidden scheduled-task entry point; all recovery decisions belong to the helper.
#>
param(
    [Parameter(Mandatory)][string]$OwnerFile,
    [Parameter(Mandatory)][string]$ServiceName,
    [Parameter(Mandatory)][string]$StateFile
)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'managed-vdd.exe') recover $OwnerFile $ServiceName $StateFile
exit $LASTEXITCODE
