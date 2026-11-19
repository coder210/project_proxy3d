# 帧同步对照机制：一键跑 C++ / C# 同场景同输入，diff 逐位一致性。
# 用法：在任意目录 powershell -File bench/parity/run_parity.ps1
# 前置：已编译 bench/parity/build/Debug/parity_harness.exe、game_cs 已 dotnet build、joy build/Debug/example_csharp.exe 存在。

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)   # 项目根（bench/parity 上两层）
$joyRun = 'd:/github/joy/build/Debug'
$bak = Join-Path $joyRun '_parity_bak'
$csOut = Join-Path $root 'game_cs/out'
$snapDir = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Path $snapDir -Force | Out-Null

# 1) C++ 快照
& (Join-Path $PSScriptRoot 'build/Debug/parity_harness.exe') |
    Out-File -Encoding ascii (Join-Path $snapDir 'cpp_snapshot.txt')

# 2) 临时把项目 JoyGame.dll 部署进 joy 宿主，跑 C# 快照
New-Item -ItemType Directory -Path $bak -Force | Out-Null
Copy-Item "$joyRun/JoyGame.dll" "$bak/JoyGame.dll" -Force
Copy-Item "$joyRun/lib/Joy.Bindings.dll" "$bak/Joy.Bindings.dll" -Force
Copy-Item "$csOut/JoyGame.dll" "$joyRun/JoyGame.dll" -Force
Copy-Item "$csOut/Joy.Bindings.dll" "$joyRun/lib/Joy.Bindings.dll" -Force
try {
    Push-Location $joyRun
    $env:SDL_VIDEODRIVER = 'dummy'
    $env:PROXY_PARITY = '1'
    # 宿主把日志写到 stderr，PowerShell 在 Stop 模式下会把 native stderr 当异常；
    # 调用宿主期间临时降为 Continue，之后恢复。
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & ./example_csharp.exe --smoke 1 2>&1 |
        Select-String -Pattern 'MAP|OBS|WALK|ECELL|WAVE|ENEMY|WAVECLR|IC |ICJ |PMSEED|MPENT|MPWORLD|PF|AI |COL |ML |BL |TICK|INPUT|ENTRIES' |
        ForEach-Object { $_.Line -replace '^\[cs\] ', '' } |
        Out-File -Encoding ascii (Join-Path $snapDir 'cs_snapshot.txt')
    $ErrorActionPreference = $prev
} finally {
    Remove-Item Env:PROXY_PARITY -ErrorAction SilentlyContinue
    Pop-Location
    # 3) 恢复 joy 宿主原 dll（保证 ctest 不受影响）
    Copy-Item "$bak/JoyGame.dll" "$joyRun/JoyGame.dll" -Force
    Copy-Item "$bak/Joy.Bindings.dll" "$joyRun/lib/Joy.Bindings.dll" -Force
    Remove-Item $bak -Recurse -Force
}

# 4) diff
$a = Get-Content (Join-Path $snapDir 'cpp_snapshot.txt') -Encoding ascii
$b = Get-Content (Join-Path $snapDir 'cs_snapshot.txt') -Encoding ascii
$d = Compare-Object -ReferenceObject $a -DifferenceObject $b
if ($d) {
    Write-Host "FAIL: C++/C# snapshot mismatch ($($d.Count) diffs)" -ForegroundColor Red
    $d | Format-Table -AutoSize
    exit 1
} else {
    Write-Host "PASS: C++/C# snapshots byte-identical ($($a.Count) ticks)" -ForegroundColor Green
    exit 0
}
