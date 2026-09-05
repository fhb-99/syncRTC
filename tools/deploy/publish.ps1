[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet('gate', 'realtime', 'media', 'all')]
    [string]$Service,

    [switch]$DryRun,
    [switch]$KeepStaging,

    [ValidateRange(30, 1800)]
    [int]$TimeoutSeconds = 300,

    [ValidateNotNullOrEmpty()]
    [string]$WslDistribution = 'Ubuntu-24.04'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$script:WslSourceRoot = '/home/ikun1/project/SyncRTC'
$script:SshAlias = 'aliyun'
$script:RemoteRoot = '/opt/syncrtc/backend'
$script:ReleaseId = '{0}-{1}' -f (Get-Date -Format 'yyyyMMdd-HHmmss'), ([Guid]::NewGuid().ToString('N').Substring(0, 8))
$script:TempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("syncrtc-publish-{0}" -f $script:ReleaseId)
$script:PackageRoot = Join-Path $script:TempRoot $script:ReleaseId
$script:SourceRoot = Join-Path $script:TempRoot 'source'
$script:RemoteStage = "$($script:RemoteRoot)/updates/$($script:ReleaseId)"
$script:Images = @{}

$serviceInfo = @{
    gate = @{
        Directory = 'GateServer'; Binary = 'GateServer'
        Tests = '^(mysql_mgr_registration_test|logic_system_token_test)$'
    }
    realtime = @{
        Directory = 'RealtimeServer'; Binary = 'RealtimeServer'
        Tests = '^(session_test|logic_system_test|meeting_info_test)$'
    }
    media = @{
        Directory = 'MediaServer'; Binary = 'MediaServer'
        Tests = '^media_room_test$'
    }
}
$selectedServices = if ($Service -eq 'all') { @('media', 'realtime', 'gate') } else { @($Service) }

function Protect-Output {
    param([AllowNull()][object[]]$Lines)
    foreach ($line in $Lines) {
        $text = [string]$line
        $text = $text -replace '(?i)((?:password|passwd|token|secret|authorization)\s*[=:]\s*)[^\s,;]+', '${1}[REDACTED]'
        $text = $text -replace '(?i)((?:smtp|turn)[_-]?(?:password|passwd|secret|token)\s*[=:]\s*)[^\s,;]+', '${1}[REDACTED]'
        Write-Host $text
    }
}

function Invoke-Native {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$Description = $FilePath,
        [switch]$Capture,
        [switch]$Redact
    )
    if ($Redact) {
        # 长时间构建和测试逐行输出，避免首次建立镜像缓存时看起来像挂起。
        & $FilePath @Arguments 2>&1 | ForEach-Object { Protect-Output @($_) }
        $exitCode = $LASTEXITCODE
        $output = @()
    }
    else {
        $output = & $FilePath @Arguments 2>&1
        $exitCode = $LASTEXITCODE
        if (-not $Capture) { $output | ForEach-Object { Write-Host $_ } }
    }
    if ($exitCode -ne 0) { throw "$Description 失败，退出码 $exitCode" }
    if ($Capture) { return ($output -join "`n").Trim() }
}

function Get-WslValue {
    param([Parameter(Mandatory = $true)][string[]]$Command)
    # 某些 WSL 版本会把 localhost 代理提示写到 stderr，且编码与 PowerShell
    # 不一致。机器可读值只取 stdout；退出码仍被严格检查。
    $value = & wsl.exe -d $WslDistribution -- @Command 2>$null
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) { throw "WSL 检查失败，退出码 $exitCode" }
    return (($value | ForEach-Object { [string]$_ }) -join "`n").Trim()
}

function Test-LocalEnvironment {
    Write-Host '[1/7] 检查 Windows/WSL/Docker/SSH 环境'
    if (-not (Test-Path -LiteralPath (Join-Path $script:ProjectRoot '.git'))) {
        throw "Windows 项目根目录不是 Git 工作区: $($script:ProjectRoot)"
    }
    Invoke-Native 'git.exe' @('-C', $script:ProjectRoot, 'status', '--short', '--branch') 'Windows Git 状态检查'
    Invoke-Native 'wsl.exe' @('-d', $WslDistribution, '--', 'test', '-d', "$($script:WslSourceRoot)/.git") 'WSL 权威源码检查'
    Invoke-Native 'wsl.exe' @('-d', $WslDistribution, '--', 'git', '-C', $script:WslSourceRoot, 'status', '--short', '--branch') 'WSL Git 状态检查'

    # docker --version 只证明客户端存在；必须让 docker version 成功读取 Server，
    # 否则在创建任何远端暂存目录之前立即退出。
    Invoke-Native 'docker.exe' @('version') 'Docker Client/Server 检查'
    $serverOs = Invoke-Native 'docker.exe' @('info', '--format', '{{.OSType}}/{{.Architecture}}') 'Docker Server 架构检查' -Capture
    if ($serverOs -ne 'linux/x86_64') { throw "Docker Server 必须是 linux/x86_64，当前为 $serverOs" }
    Invoke-Native 'ssh.exe' @('-o', 'BatchMode=yes', '-o', "ConnectTimeout=$TimeoutSeconds", $script:SshAlias, 'test -d /opt/syncrtc/backend') 'SSH 与云端部署根目录检查'
}

function New-SourceSnapshot {
    Write-Host '[2/7] 从 WSL 权威工作区生成不可变源码快照'
    New-Item -ItemType Directory -Path $script:TempRoot, $script:PackageRoot, $script:SourceRoot -Force | Out-Null

    $branchBefore = Get-WslValue @('git', '-C', $script:WslSourceRoot, 'branch', '--show-current')
    $commitBefore = Get-WslValue @('git', '-C', $script:WslSourceRoot, 'rev-parse', 'HEAD')
    $statusBefore = Get-WslValue @('bash', '-lc', "cd '$($script:WslSourceRoot)' && git status --porcelain=v1 --untracked-files=all | sha256sum | cut -d' ' -f1")
    $dirtyText = Get-WslValue @('git', '-C', $script:WslSourceRoot, 'status', '--porcelain=v1', '--untracked-files=all')
    $isDirty = -not [string]::IsNullOrWhiteSpace($dirtyText)

    $archivePath = Join-Path $script:TempRoot 'source.tar'
    # wsl.exe 直接传递反斜杠时会被 Linux 参数解析吞掉，先改成 WSL 能稳定
    # 识别的 E:/... 形式再转换。
    $wslArchive = Get-WslValue @('wslpath', '-a', ($archivePath.Replace('\', '/')))
    $snapshotPaths = @('syncRTC-server/db/init/01-schema.sql')
    foreach ($name in $selectedServices) { $snapshotPaths += "syncRTC-server/$($serviceInfo[$name].Directory)" }
    $tarArguments = @(
        '-d', $WslDistribution, '--', 'tar', '-C', $script:WslSourceRoot,
        '--exclude=.git', '--exclude=.env', '--exclude=.env.*',
        '--exclude=node_modules', '--exclude=*/node_modules',
        '--exclude=build', '--exclude=*/build', '--exclude=*/build-*',
        '--exclude=bin', '--exclude=*/bin',
        '--exclude=*/conf/config.ini', '--exclude=*/config.json',
        '-cf', $wslArchive
    ) + $snapshotPaths
    Invoke-Native 'wsl.exe' $tarArguments 'WSL 源码归档'

    $branchAfter = Get-WslValue @('git', '-C', $script:WslSourceRoot, 'branch', '--show-current')
    $commitAfter = Get-WslValue @('git', '-C', $script:WslSourceRoot, 'rev-parse', 'HEAD')
    $statusAfter = Get-WslValue @('bash', '-lc', "cd '$($script:WslSourceRoot)' && git status --porcelain=v1 --untracked-files=all | sha256sum | cut -d' ' -f1")
    if ($branchBefore -ne $branchAfter -or $commitBefore -ne $commitAfter -or $statusBefore -ne $statusAfter) {
        throw '生成快照期间 WSL 工作区状态发生变化，已停止发布；请在源码稳定后重试'
    }

    Invoke-Native 'tar.exe' @('-xf', $archivePath, '-C', $script:SourceRoot) '解压源码快照'
    Remove-Item -LiteralPath $archivePath -Force
    $buildSupport = Join-Path $script:SourceRoot '.publisher/cmake'
    New-Item -ItemType Directory -Path $buildSupport -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'cmake/gRPCConfig.cmake') -Destination $buildSupport
    foreach ($name in $selectedServices) {
        $cmakePath = Join-Path $script:SourceRoot "syncRTC-server/$($serviceInfo[$name].Directory)/CMakeLists.txt"
        if (-not (Test-Path -LiteralPath $cmakePath)) { throw "源码快照缺少 $name 的 CMakeLists.txt" }
    }

    $script:SourceMetadata = [ordered]@{
        branch = $branchBefore
        commit = $commitBefore
        dirty = $isDirty
        status_fingerprint = $statusBefore
    }
    Write-Host "源码: branch=$branchBefore commit=$commitBefore dirty=$isDirty"
}

function New-TestInfrastructure {
    $suffix = [Guid]::NewGuid().ToString('N').Substring(0, 10)
    $network = "syncrtc-publish-$suffix"
    $mysql = "syncrtc-publish-mysql-$suffix"
    $redis = "syncrtc-publish-redis-$suffix"
    $secret = ([Guid]::NewGuid().ToString('N') + [Guid]::NewGuid().ToString('N'))
    $infraDir = Join-Path $script:TempRoot "test-infra-$suffix"
    New-Item -ItemType Directory -Path $infraDir -Force | Out-Null

    $mysqlEnv = Join-Path $infraDir 'mysql.env'
    $mysqlClient = Join-Path $infraDir 'mysql-client.cnf'
    $redisConfig = Join-Path $infraDir 'redis.conf'
    $runtimeConfig = Join-Path $infraDir 'config.ini'
    Set-Content $mysqlEnv @("MYSQL_ROOT_PASSWORD=$secret", 'MYSQL_DATABASE=syncrtc', 'MYSQL_USER=syncrtc', "MYSQL_PASSWORD=$secret") -Encoding utf8NoBOM
    Set-Content $mysqlClient @('[client]', 'user=syncrtc', "password=$secret", 'host=127.0.0.1') -Encoding utf8NoBOM
    Set-Content $redisConfig @('bind 0.0.0.0', 'protected-mode no', 'port 6379', "requirepass $secret") -Encoding utf8NoBOM
    Set-Content $runtimeConfig @(
        '[GateServer]', 'Port=8081', '[VarifyServer]', 'Host=127.0.0.1', 'Port=50051',
        '[RealtimeServer]', 'Host=127.0.0.1', 'Port=8090',
        '[Redis]', "Host=$redis", 'Port=6379', "Password=$secret",
        '[Mysql]', "Host=$mysql", 'Port=3306', 'User=syncrtc', "Password=$secret", 'Database=syncrtc',
        '[MediaServer]', 'InternalSocketPath=/tmp/syncrtc-mediaserver.sock'
    ) -Encoding utf8NoBOM

    Invoke-Native 'docker.exe' @('network', 'create', $network) '创建隔离测试网络' -Capture | Out-Null
    try {
        $schema = Join-Path $script:SourceRoot 'syncRTC-server/db/init/01-schema.sql'
        Invoke-Native 'docker.exe' @(
            'run', '-d', '--name', $mysql, '--network', $network, '--env-file', $mysqlEnv,
            '-v', "${schema}:/docker-entrypoint-initdb.d/01-schema.sql:ro",
            '-v', "${mysqlClient}:/tmp/mysql-client.cnf:ro", 'mysql:8.4'
        ) '启动临时 MySQL' -Capture | Out-Null
        Invoke-Native 'docker.exe' @('run', '-d', '--name', $redis, '--network', $network, '-v', "${redisConfig}:/usr/local/etc/redis/redis.conf:ro", 'redis:7-alpine', 'redis-server', '/usr/local/etc/redis/redis.conf') '启动临时 Redis' -Capture | Out-Null

        $deadline = (Get-Date).AddSeconds([Math]::Min($TimeoutSeconds, 180))
        $ready = $false
        do {
            # MySQL 初始化阶段会先启动再停止一个临时实例，不能只匹配第一次
            # "ready for connections"。确认临时实例和最终实例均已就绪，再用
            # 只读客户端配置做真实认证探测。
            $logs = & docker.exe logs $mysql 2>&1
            $readyCount = [regex]::Matches(($logs -join "`n"), 'ready for connections').Count
            if ($readyCount -ge 2) {
                # 从同一 Docker 网络中的独立容器发起 TCP 认证，避免把容器内
                # localhost 可用误判成其他构建容器也能连接。
                & docker.exe run --rm --network $network -v "${mysqlClient}:/tmp/mysql-client.cnf:ro" --entrypoint mysqladmin mysql:8.4 --defaults-extra-file=/tmp/mysql-client.cnf --host=$mysql ping --silent 2>$null | Out-Null
                if ($LASTEXITCODE -eq 0) { $ready = $true; break }
            }
            Start-Sleep -Seconds 2
        } while ((Get-Date) -lt $deadline)
        if (-not $ready) {
            $logs = & docker.exe logs $mysql 2>&1
            Protect-Output $logs
            throw '临时 MySQL 未在超时时间内完成认证就绪检查'
        }
        Start-Sleep -Seconds 2
        return [pscustomobject]@{ Network = $network; Mysql = $mysql; Redis = $redis; Config = $runtimeConfig }
    }
    catch {
        & docker.exe rm -f $mysql $redis 2>$null | Out-Null
        & docker.exe network rm $network 2>$null | Out-Null
        throw
    }
}

function Remove-TestInfrastructure {
    param([Parameter(Mandatory = $true)]$Infrastructure)
    & docker.exe rm -f $Infrastructure.Mysql $Infrastructure.Redis 2>$null | Out-Null
    & docker.exe network rm $Infrastructure.Network 2>$null | Out-Null
}

function Build-And-TestService {
    param([Parameter(Mandatory = $true)][string]$Name)
    Write-Host "[3/7] 构建并测试 $Name"
    $tag = "syncrtc-publisher:$($script:ReleaseId)-$Name"
    $script:Images[$Name] = $tag
    Invoke-Native 'docker.exe' @(
        'build', '--progress', 'plain', '--platform', 'linux/amd64', '--target', 'build', '--tag', $tag,
        '--build-arg', "SERVICE=$Name", '--file', (Join-Path $PSScriptRoot 'Dockerfile.build'), $script:SourceRoot
    ) "$Name Ubuntu 22.04 构建" -Redact

    $infra = $null
    try {
        $testArgs = @('run', '--rm')
        if ($Name -in @('gate', 'realtime')) {
            $infra = New-TestInfrastructure
            $containerConfig = "/opt/syncrtc/backend/syncRTC-server/$($serviceInfo[$Name].Directory)/bin/conf/config.ini"
            $testArgs += @('--network', $infra.Network, '-v', "$($infra.Config):${containerConfig}:ro")
        }
        # libdatachannel 会向同一 CTest 目录注册未随本项目构建的第三方自测程序。
        # 这里只运行仓库明确声明的服务测试，避免把第三方未构建目标误判为项目失败。
        $testArgs += @(
            $tag, 'ctest', '--test-dir', "/build/$Name", '--output-on-failure',
            '--timeout', '90', '--tests-regex', $serviceInfo[$Name].Tests
        )
        Invoke-Native 'docker.exe' $testArgs "$Name 自动化测试" -Redact
    }
    finally { if ($null -ne $infra) { Remove-TestInfrastructure $infra } }

    $artifactDir = Join-Path $script:PackageRoot "artifacts/$Name"
    New-Item -ItemType Directory -Path $artifactDir -Force | Out-Null
    $containerId = Invoke-Native 'docker.exe' @('create', $tag) '创建产物导出容器' -Capture
    try { Invoke-Native 'docker.exe' @('cp', "${containerId}:/out/$($serviceInfo[$Name].Binary)", "$artifactDir/") "$Name 产物导出" }
    finally { & docker.exe rm -f $containerId 2>$null | Out-Null }

    $elfInfo = Invoke-Native 'docker.exe' @('run', '--rm', '--entrypoint', 'readelf', $tag, '-h', "/out/$($serviceInfo[$Name].Binary)") "$Name ELF 检查" -Capture
    if ($elfInfo -notmatch 'Class:\s+ELF64' -or $elfInfo -notmatch 'Machine:\s+Advanced Micro Devices X86-64') {
        throw "$Name 构建产物不是 Linux x86-64 ELF"
    }
    $artifact = Join-Path $artifactDir $serviceInfo[$Name].Binary
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $artifact).Hash.ToLowerInvariant()
    Write-Host "${Name}: 自动化测试通过，ELF=x86-64，SHA256=$hash"
}

function New-ReleasePackage {
    Write-Host '[4/7] 生成严格白名单发布包'
    $metadata = [ordered]@{
        release_id = $script:ReleaseId; service = $Service
        generated_at = (Get-Date).ToUniversalTime().ToString('o')
        source = $script:SourceMetadata; services = $selectedServices; tests = 'passed'
        acceptance_boundary = 'process-port-uds-basic-endpoint-only; not end-to-end media'
    }
    $metadata | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $script:PackageRoot 'metadata.json') -Encoding utf8NoBOM
    Copy-Item (Join-Path $PSScriptRoot 'remote-release.sh') (Join-Path $script:PackageRoot 'remote-release.sh')

    $allowed = @('metadata.json', 'remote-release.sh')
    foreach ($name in $selectedServices) { $allowed += "artifacts/$name/$($serviceInfo[$name].Binary)" }
    $allowed = $allowed | Sort-Object
    $manifestLines = foreach ($relative in $allowed) {
        $fullPath = Join-Path $script:PackageRoot ($relative -replace '/', [IO.Path]::DirectorySeparatorChar)
        if (-not (Test-Path $fullPath -PathType Leaf)) { throw "发布白名单文件缺失: $relative" }
        "{0}  {1}" -f (Get-FileHash -Algorithm SHA256 -LiteralPath $fullPath).Hash.ToLowerInvariant(), $relative
    }
    # sha256sum 的清单必须使用 Linux LF；PowerShell 的默认 CRLF 会让远端把
    # 回车解析进文件名，导致校验失败。
    $manifestPath = Join-Path $script:PackageRoot 'manifest.sha256'
    [IO.File]::WriteAllText($manifestPath, (($manifestLines -join "`n") + "`n"), [Text.Encoding]::ASCII)

    $actual = Get-ChildItem $script:PackageRoot -File -Recurse | ForEach-Object { [IO.Path]::GetRelativePath($script:PackageRoot, $_.FullName).Replace('\', '/') } | Sort-Object
    $expected = @($allowed + 'manifest.sha256') | Sort-Object
    if (($actual -join "`n") -ne ($expected -join "`n")) { throw '发布包包含白名单之外的文件' }
}

function Invoke-RemoteRelease {
    Write-Host '[5/7] 上传到远端时间戳暂存目录并执行发布'
    $mkdirCommand = "set -e; stage='$($script:RemoteStage)'; case `"`$stage`" in /opt/syncrtc/backend/updates/$($script:ReleaseId)) ;; *) exit 65;; esac; sudo install -d -m 0755 -o `"`$(id -un)`" -g `"`$(id -gn)`" -- `"`$stage`""
    Invoke-Native 'ssh.exe' @('-o', 'BatchMode=yes', '-o', "ConnectTimeout=$TimeoutSeconds", $script:SshAlias, $mkdirCommand) '创建远端暂存目录'
    $uploadItems = @(
        (Join-Path $script:PackageRoot 'manifest.sha256'), (Join-Path $script:PackageRoot 'metadata.json'),
        (Join-Path $script:PackageRoot 'remote-release.sh'), (Join-Path $script:PackageRoot 'artifacts')
    )
    Invoke-Native 'scp.exe' (@('-r', '-o', 'BatchMode=yes', '-o', "ConnectTimeout=$TimeoutSeconds") + $uploadItems + @("$($script:SshAlias):$($script:RemoteStage)/")) '上传发布白名单包'
    $remoteArgs = @('sudo', 'bash', "$($script:RemoteStage)/remote-release.sh", '--service', $Service, '--release-id', $script:ReleaseId)
    if ($DryRun) { $remoteArgs += '--dry-run' }
    if ($KeepStaging) { $remoteArgs += '--keep-staging' }
    Invoke-Native 'ssh.exe' @('-o', 'BatchMode=yes', '-o', "ConnectTimeout=$TimeoutSeconds", $script:SshAlias, ($remoteArgs -join ' ')) '远端事务发布' -Redact
}

try {
    Test-LocalEnvironment
    New-SourceSnapshot
    foreach ($name in $selectedServices) { Build-And-TestService $name }
    New-ReleasePackage
    Invoke-RemoteRelease
    Write-Host "[6/7] release=$($script:ReleaseId) service=$Service dry_run=$($DryRun.IsPresent)"
    Write-Host "source_branch=$($script:SourceMetadata.branch) source_commit=$($script:SourceMetadata.commit) dirty=$($script:SourceMetadata.dirty)"
    if ($DryRun) { Write-Host '结果：dry-run 通过，没有停止服务、替换文件或修改配置/数据库/防火墙。' }
    else { Write-Host '结果：基础发布验收通过；仍需按部署文档完成 PC/Android 双端音视频验收。' }
}
catch {
    Write-Error $_
    exit 1
}
finally {
    Write-Host '[7/7] 清理本地临时快照'
    if ($KeepStaging) { Write-Host "已按要求保留本地暂存目录: $($script:TempRoot)" }
    elseif (Test-Path $script:TempRoot) {
        # 临时目录由系统临时目录和随机 release-id 组成；递归删除前再次校验。
        $resolvedTemp = [IO.Path]::GetFullPath($script:TempRoot)
        $systemTemp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
        if ($resolvedTemp.StartsWith($systemTemp, [StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetFileName($resolvedTemp) -eq "syncrtc-publish-$($script:ReleaseId)") {
            Remove-Item -LiteralPath $resolvedTemp -Recurse -Force
        }
        else { Write-Warning "临时路径校验失败，未自动删除: $resolvedTemp" }
    }
}
