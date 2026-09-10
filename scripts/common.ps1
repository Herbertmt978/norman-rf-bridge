function Assert-NormanRfEspHomeVersion {
    $expectedVersion = '2026.4.1'
    $versionOutput = & python -m esphome version 2>&1
    $versionExitCode = $LASTEXITCODE
    if ($versionExitCode -ne 0) {
        throw 'Unable to determine the installed ESPHome version.'
    }

    $match = [regex]::Match(($versionOutput -join "`n"), 'Version:\s*([^\s]+)')
    if (-not $match.Success -or $match.Groups[1].Value -ne $expectedVersion) {
        throw "This project is tested with ESPHome $expectedVersion. Install that exact version before building or flashing."
    }
}

function Protect-NormanRfBuildDirectory([string]$Path) {
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
    $resolvedPath = (Resolve-Path -LiteralPath $Path).Path.TrimEnd('\')
    if ($resolvedPath -ne $Path.TrimEnd('\')) {
        throw "Unexpected build directory resolution: $resolvedPath"
    }

    $existingAcl = Get-Acl -LiteralPath $resolvedPath
    if ($existingAcl.AreAccessRulesProtected) {
        return
    }

    $currentSid = [Security.Principal.WindowsIdentity]::GetCurrent().User
    $acl = [Security.AccessControl.DirectorySecurity]::new()
    $acl.SetAccessRuleProtection($true, $false)
    $acl.SetOwner($currentSid)

    $inheritance = [Security.AccessControl.InheritanceFlags]'ContainerInherit, ObjectInherit'
    $propagation = [Security.AccessControl.PropagationFlags]::None
    $allow = [Security.AccessControl.AccessControlType]::Allow
    foreach ($sidText in @($currentSid.Value, 'S-1-5-18', 'S-1-5-32-544')) {
        $sid = [Security.Principal.SecurityIdentifier]::new($sidText)
        $rule = [Security.AccessControl.FileSystemAccessRule]::new(
            $sid,
            [Security.AccessControl.FileSystemRights]::FullControl,
            $inheritance,
            $propagation,
            $allow
        )
        [void]$acl.AddAccessRule($rule)
    }
    Set-Acl -LiteralPath $resolvedPath -AclObject $acl
}
