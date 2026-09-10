[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^COM[0-9]+$')]
    [string]$Port,

    [string]$Ssid,

    [ValidateRange(10, 120)]
    [int]$TimeoutSeconds = 45
)

$ErrorActionPreference = 'Stop'

function New-ImprovPacket {
    param(
        [Parameter(Mandatory = $true)]
        [byte]$Type,

        [Parameter(Mandatory = $true)]
        [byte[]]$Data
    )

    if ($Data.Length -gt 255) {
        throw 'The Improv payload is too large.'
    }

    $packet = [Collections.Generic.List[byte]]::new()
    foreach ($value in [Text.Encoding]::ASCII.GetBytes('IMPROV')) {
        $packet.Add($value)
    }
    $packet.Add(1)
    $packet.Add($Type)
    $packet.Add([byte]$Data.Length)
    $packet.AddRange($Data)

    [uint32]$checksum = 0
    foreach ($value in $packet) {
        $checksum += $value
    }
    $packet.Add([byte]($checksum -band 0xff))
    $packet.Add(10)
    return ,$packet.ToArray()
}

function Get-ImprovFrames {
    param(
        [Parameter(Mandatory = $true)]
        [Collections.Generic.List[byte]]$Buffer
    )

    $header = [Text.Encoding]::ASCII.GetBytes('IMPROV')
    $frames = [Collections.Generic.List[object]]::new()
    $offset = 0

    while ($offset + 10 -le $Buffer.Count) {
        $headerMatches = $true
        for ($index = 0; $index -lt $header.Length; $index++) {
            if ($Buffer[$offset + $index] -ne $header[$index]) {
                $headerMatches = $false
                break
            }
        }
        if (-not $headerMatches) {
            $offset++
            continue
        }

        $dataLength = [int]$Buffer[$offset + 8]
        $frameLength = 10 + $dataLength
        if ($offset + $frameLength -gt $Buffer.Count) {
            break
        }

        [uint32]$calculatedChecksum = 0
        for ($index = 0; $index -lt 9 + $dataLength; $index++) {
            $calculatedChecksum += $Buffer[$offset + $index]
        }
        $receivedChecksum = $Buffer[$offset + 9 + $dataLength]
        if ([byte]($calculatedChecksum -band 0xff) -eq $receivedChecksum) {
            $data = [byte[]]::new($dataLength)
            if ($dataLength -gt 0) {
                $Buffer.CopyTo($offset + 9, $data, 0, $dataLength)
            }
            $frames.Add([pscustomobject]@{
                    Type = [byte]$Buffer[$offset + 7]
                    Data = $data
                })
            $offset += $frameLength
            if ($offset -lt $Buffer.Count -and $Buffer[$offset] -eq 10) {
                $offset++
            }
            continue
        }

        $offset++
    }

    if ($offset -gt 0) {
        $Buffer.RemoveRange(0, $offset)
    }
    return $frames
}

function Get-ImprovRpcStrings {
    param(
        [Parameter(Mandatory = $true)]
        [byte[]]$Data
    )

    if ($Data.Length -lt 2) {
        return @()
    }

    $declaredLength = [int]$Data[1]
    $limit = [Math]::Min($Data.Length, 2 + $declaredLength)
    $values = [Collections.Generic.List[string]]::new()
    $offset = 2
    while ($offset -lt $limit) {
        $length = [int]$Data[$offset]
        $offset++
        if ($offset + $length -gt $limit) {
            break
        }
        $values.Add([Text.Encoding]::UTF8.GetString($Data, $offset, $length))
        $offset += $length
    }
    return $values.ToArray()
}

$connectedPorts = @([IO.Ports.SerialPort]::GetPortNames())
if ($Port -notin $connectedPorts) {
    throw "Serial port $Port is not connected."
}

if ([string]::IsNullOrWhiteSpace($Ssid)) {
    $interfaceOutput = & netsh.exe wlan show interfaces 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw 'Unable to read the current Wi-Fi connection.'
    }
    $connectedSsids = @(
        foreach ($line in $interfaceOutput) {
            if ($line -match '^\s*SSID\s*:\s*(.+?)\s*$') {
                $Matches[1]
            }
        }
    )
    if ($connectedSsids.Count -ne 1) {
        throw 'Connect this PC to the target Wi-Fi network or pass -Ssid explicitly.'
    }
    $Ssid = $connectedSsids[0]
}

$profileOutput = & netsh.exe wlan show profile "name=$Ssid" key=clear 2>$null
if ($LASTEXITCODE -ne 0) {
    throw 'The target Wi-Fi profile is not stored for this Windows user.'
}

$password = $null
foreach ($line in $profileOutput) {
    if ($line -match '^\s*Key Content\s*:\s*(.*?)\s*$') {
        $password = $Matches[1]
        break
    }
}
if ($null -eq $password) {
    $authenticationLine = $profileOutput | Where-Object { $_ -match '^\s*Authentication\s*:' } | Select-Object -First 1
    if ($authenticationLine -notmatch ':\s*Open\s*$') {
        throw 'Windows did not expose a key for the target Wi-Fi profile.'
    }
    $password = ''
}

$ssidBytes = [Text.Encoding]::UTF8.GetBytes($Ssid)
$passwordBytes = [Text.Encoding]::UTF8.GetBytes($password)
if ($ssidBytes.Length -gt 255 -or $passwordBytes.Length -gt 255 -or
    4 + $ssidBytes.Length + $passwordBytes.Length -gt 255) {
    throw 'The Wi-Fi SSID or key is too long for Improv Serial.'
}

$rpcData = [Collections.Generic.List[byte]]::new()
$rpcData.Add(1)
$rpcData.Add([byte](2 + $ssidBytes.Length + $passwordBytes.Length))
$rpcData.Add([byte]$ssidBytes.Length)
$rpcData.AddRange($ssidBytes)
$rpcData.Add([byte]$passwordBytes.Length)
$rpcData.AddRange($passwordBytes)

$serial = [IO.Ports.SerialPort]::new()
$serial.PortName = $Port
$serial.BaudRate = 115200
$serial.Parity = [IO.Ports.Parity]::None
$serial.DataBits = 8
$serial.StopBits = [IO.Ports.StopBits]::One
$serial.Handshake = [IO.Ports.Handshake]::None
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$serial.ReadTimeout = 100
$serial.WriteTimeout = 2000

$buffer = [Collections.Generic.List[byte]]::new()
$deviceName = $null
$provisioned = $false
$lastError = 0

try {
    $serial.Open()
    Start-Sleep -Milliseconds 2500
    $serial.DiscardInBuffer()

    $deviceInfoRequest = New-ImprovPacket -Type 3 -Data ([byte[]](3, 0))
    $serial.Write($deviceInfoRequest, 0, $deviceInfoRequest.Length)
    Start-Sleep -Milliseconds 250

    $settingsRequest = New-ImprovPacket -Type 3 -Data $rpcData.ToArray()
    $serial.Write($settingsRequest, 0, $settingsRequest.Length)

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline -and -not $provisioned -and $lastError -eq 0) {
        $available = $serial.BytesToRead
        if ($available -gt 0) {
            $chunk = [byte[]]::new($available)
            $read = $serial.Read($chunk, 0, $available)
            for ($index = 0; $index -lt $read; $index++) {
                $buffer.Add($chunk[$index])
            }

            foreach ($frame in Get-ImprovFrames -Buffer $buffer) {
                if ($frame.Type -eq 1 -and $frame.Data.Length -eq 1 -and $frame.Data[0] -eq 4) {
                    $provisioned = $true
                } elseif ($frame.Type -eq 2 -and $frame.Data.Length -eq 1) {
                    $lastError = [int]$frame.Data[0]
                } elseif ($frame.Type -eq 4 -and $frame.Data.Length -ge 2 -and $frame.Data[0] -eq 3) {
                    $info = @(Get-ImprovRpcStrings -Data $frame.Data)
                    if ($info.Count -ge 4) {
                        $deviceName = $info[3]
                    }
                }
            }
        } else {
            Start-Sleep -Milliseconds 50
        }
    }
} finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
    $serial.Dispose()
    $password = $null
    $passwordBytes = $null
    $rpcData = $null
}

if ($lastError -ne 0) {
    throw "Improv Serial reported error code $lastError while provisioning $Port."
}
if (-not $provisioned) {
    throw "Timed out while waiting for $Port to join Wi-Fi."
}

if ([string]::IsNullOrWhiteSpace($deviceName)) {
    $deviceName = 'the ESPHome device'
}

Write-Output ([pscustomobject]@{
        Port       = $Port
        DeviceName = $deviceName
        State      = 'provisioned'
    })
