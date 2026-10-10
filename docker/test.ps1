# Run an Ubuntu 24.04 test suite from Windows. Pass one suite name, or none for core.
$ErrorActionPreference = 'Stop'

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    throw 'Install Docker Desktop and start it before running Ubuntu tests.'
}

Set-Location (Join-Path $PSScriptRoot '..')
& docker compose run --rm ubuntu @args
exit $LASTEXITCODE
