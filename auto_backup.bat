@echo off
echo 正在执行自动Git备份...

REM 检查是否有未提交的更改
git status --porcelain >nul
if %errorlevel% equ 0 (
    echo 检测到文件更改，正在自动备份...
    
    REM 添加所有更改
    git add .
    
    REM 创建带时间戳的提交
    for /f "tokens=2 delims==" %%a in ('wmic OS Get localdatetime /value') do set "dt=%%a"
    set "timestamp=%dt:~0,4%-%dt:~4,2%-%dt:~6,2%_%dt:~8,2%-%dt:~10,2%-%dt:~12,2%"
    
    git commit -m "Auto backup: %timestamp%"
    
    if %errorlevel% equ 0 (
        echo 备份成功完成: %timestamp%
        
        REM 推送到远程仓库
        git push origin feature/development
        if %errorlevel% equ 0 (
            echo 远程备份同步成功
        ) else (
            echo 警告: 远程推送失败，但本地备份已保存
        )
    ) else (
        echo 备份失败，请检查Git状态
    )
) else (
    echo 没有检测到文件更改，无需备份
)

timeout /t 2 >nul