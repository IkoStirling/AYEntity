param([Parameter(Mandatory=$true)][string]$Worker,[Parameter(Mandatory=$true)][string]$OutputDir,[int]$TimeoutSeconds=70,[switch]$CompareTrace)
$ErrorActionPreference='Stop'
$ownedOutput=[IO.Path]::GetFullPath($OutputDir)
$run=Join-Path $ownedOutput ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run -Force | Out-Null
$reservation=[Net.Sockets.UdpClient]::new(0)
$port=$reservation.Client.LocalEndPoint.Port
$reservation.Dispose()
$peers=@()
try {
    foreach($member in 1,2){
        $arguments=@($member,$port,('"'+(Join-Path $run "$member.state")+'"'),('"'+(Join-Path $run 'session.rpl')+'"'))
        $peers+=Start-Process -FilePath $Worker -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run "$member.log") -RedirectStandardError (Join-Path $run "$member.error")
        $null=$peers[-1].Handle
        if($member -eq 1){Start-Sleep -Milliseconds 200}
    }
    foreach($peer in $peers){if(-not $peer.WaitForExit($TimeoutSeconds*1000)){throw 'Network worker timed out'}}
    foreach($member in 1,2){Get-Content (Join-Path $run "$member.log");Get-Content (Join-Path $run "$member.error")}
    foreach($peer in $peers){if($peer.ExitCode -ne 0){throw "Network worker exit $($peer.ExitCode)"}}
    $a=[IO.File]::ReadAllBytes((Join-Path $run '1.state'));$b=[IO.File]::ReadAllBytes((Join-Path $run '2.state'))
    if([Convert]::ToBase64String($a) -cne [Convert]::ToBase64String($b)){throw 'Peer checkpoint bytes differ'}
    if($CompareTrace){
        $a=[IO.File]::ReadAllBytes((Join-Path $run '1.state.trace'));$b=[IO.File]::ReadAllBytes((Join-Path $run '2.state.trace'))
        if([Convert]::ToBase64String($a) -cne [Convert]::ToBase64String($b)){throw 'Peer per-tick traces differ'}
    }
    Write-Output "PASS actual AYNetwork two-process state bytes, retransmission and replay/seek; artifacts $run"
} finally {foreach($peer in $peers){if(-not $peer.HasExited){Stop-Process -Id $peer.Id -Force}}}
