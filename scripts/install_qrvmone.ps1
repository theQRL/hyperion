$ErrorActionPreference = "Stop"

# Needed for Invoke-WebRequest to work via CI.
$progressPreference = "silentlyContinue"

Invoke-WebRequest -URI "https://github.com/theQRL/qrvmone/releases/download/v0.10.0/qrvmone-0.10.0-windows-amd64.zip" -OutFile "qrvmone.zip"
tar -xf qrvmone.zip "bin/qrvmone.dll"
mkdir deps
mv bin/qrvmone.dll deps
