@echo off
echo 设置定时自动备份任务...

REM 创建任务计划
schtasks /create /tn "MetalSlug_Auto_Backup" /tr "f:\ZyyDocument\UE\MetalSlugGet\MetalSlug\auto_backup.bat" /sc minute /mo 30 /f

if %errorlevel% equ 0 (
    echo 定时备份任务创建成功！
    echo 任务名称: MetalSlug_Auto_Backup
    echo 执行频率: 每30分钟检查一次
    echo 备份脚本位置: f:\ZyyDocument\UE\MetalSlugGet\MetalSlug\auto_backup.bat
) else (
    echo 定时任务创建失败，请手动运行备份脚本
)

echo.
echo 手动备份命令:
echo 运行 auto_backup.bat 即可立即备份当前更改
echo.
echo 查看任务状态:
echo schtasks /query /tn "MetalSlug_Auto_Backup"
echo.
echo 删除定时任务:
echo schtasks /delete /tn "MetalSlug_Auto_Backup" /f

pause