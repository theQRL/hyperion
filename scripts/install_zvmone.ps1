$ErrorActionPreference = "Stop"

# Needed for Invoke-WebRequest to work via CI.
$progressPreference = "silentlyContinue"

Invoke-WebRequest -URI "https://github.com/theQRL/zvmone/releases/download/v0.10.0/zvmone-0.10.0-windows-amd64.zip" -OutFile "zvmone.zip"
tar -xf zvmone.zip "bin/zvmone.dll"
mkdir deps
mv bin/zvmone.dll deps
